param(
    [string]$OutputRoot = '',
    [ValidateSet('v4', 'v5')]
    [string]$AnimationVersion = 'v5',
    [string]$ActionList = 'jogando-nintendo,brincando,concentrada,rindo-carinho,soprando-bolhas',
    [switch]$SkipEffects
)

$ErrorActionPreference = 'Stop'
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $PSScriptRoot '..\output'
}
$actions = @($ActionList -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })

Add-Type -AssemblyName System.Drawing

$alphaInspectorSource = @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public sealed class AlphaInspection
{
    public int VisiblePixels;
    public int OpaqueDarkPixels;
    public int MinimumMargin;
    public bool BorderIsTransparent;
}

public static class AlphaInspectorV5
{
    public static AlphaInspection Inspect(string path)
    {
        using (var original = new Bitmap(path))
        using (var bitmap = new Bitmap(original.Width, original.Height, PixelFormat.Format32bppArgb))
        {
            using (var graphics = Graphics.FromImage(bitmap))
                graphics.DrawImageUnscaled(original, 0, 0);

            int minX = bitmap.Width;
            int minY = bitmap.Height;
            int maxX = -1;
            int maxY = -1;
            int visible = 0;
            int opaqueDark = 0;
            bool borderClear = true;

            var rectangle = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
            var data = bitmap.LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                int byteCount = Math.Abs(data.Stride) * bitmap.Height;
                byte[] pixels = new byte[byteCount];
                Marshal.Copy(data.Scan0, pixels, 0, byteCount);
                for (int y = 0; y < bitmap.Height; y++)
                {
                    int rowOffset = y * Math.Abs(data.Stride);
                    for (int x = 0; x < bitmap.Width; x++)
                    {
                        byte alpha = pixels[rowOffset + x * 4 + 3];
                        if (alpha == 0) continue;
                        visible++;
                        byte blue = pixels[rowOffset + x * 4];
                        byte green = pixels[rowOffset + x * 4 + 1];
                        byte red = pixels[rowOffset + x * 4 + 2];
                        if (alpha >= 240 && red <= 16 && green <= 16 && blue <= 16)
                            opaqueDark++;
                        if (x < minX) minX = x;
                        if (y < minY) minY = y;
                        if (x > maxX) maxX = x;
                        if (y > maxY) maxY = y;
                        if (x == 0 || y == 0 || x == bitmap.Width - 1 || y == bitmap.Height - 1)
                            borderClear = false;
                    }
                }
            }
            finally
            {
                bitmap.UnlockBits(data);
            }

            int margin = visible == 0
                ? Math.Min(bitmap.Width, bitmap.Height)
                : Math.Min(Math.Min(minX, bitmap.Width - 1 - maxX), Math.Min(minY, bitmap.Height - 1 - maxY));

            return new AlphaInspection
            {
                VisiblePixels = visible,
                OpaqueDarkPixels = opaqueDark,
                MinimumMargin = margin,
                BorderIsTransparent = borderClear
            };
        }
    }
}
'@

Add-Type -TypeDefinition $alphaInspectorSource -ReferencedAssemblies System.Drawing

$directions = @('frente', 'costas', 'direita', 'esquerda')
$errors = [System.Collections.Generic.List[string]]::new()
$characterFrames = 0
$effectFrames = 0
$minimumCharacterMargin = 512
$minimumEffectMargin = 512
$checkedGifs = 0
$checkedSheets = 0
$effectSets = 0
$darkPixelRecords = [System.Collections.Generic.List[object]]::new()

function Test-Gif {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        $script:errors.Add("GIF ausente: $Label")
        return
    }

    $gif = [System.Drawing.Image]::FromFile($Path)
    try {
        if ($gif.Width -ne 512 -or $gif.Height -ne 512) {
            $script:errors.Add("GIF com tamanho incorreto: $Label ($($gif.Width)x$($gif.Height))")
        }

        $frameDimension = New-Object System.Drawing.Imaging.FrameDimension($gif.FrameDimensionsList[0])
        $frameCount = $gif.GetFrameCount($frameDimension)
        if ($frameCount -ne 12) {
            $script:errors.Add("GIF sem 12 frames: $Label ($frameCount)")
        }

        try {
            $delayItem = $gif.GetPropertyItem(0x5100)
            if ($delayItem.Len -ne 48) {
                $script:errors.Add("Tabela de timing inválida: $Label ($($delayItem.Len) bytes)")
            }
            else {
                for ($index = 0; $index -lt 12; $index++) {
                    $delay = [System.BitConverter]::ToInt32($delayItem.Value, $index * 4)
                    if ($delay -ne 8) {
                        $script:errors.Add("Timing diferente de 80 ms: $Label, frame $($index + 1) ($delay cs)")
                    }
                }
            }
        }
        catch {
            $script:errors.Add("GIF sem metadado de timing: $Label")
        }

        try {
            $loopItem = $gif.GetPropertyItem(0x5101)
            $loop = [System.BitConverter]::ToUInt16($loopItem.Value, 0)
            if ($loop -ne 0) {
                $script:errors.Add("GIF sem loop infinito: $Label ($loop)")
            }
        }
        catch {
            $script:errors.Add("GIF sem metadado de loop: $Label")
        }

        for ($index = 0; $index -lt $frameCount; $index++) {
            [void]$gif.SelectActiveFrame($frameDimension, $index)
            $frameBitmap = New-Object System.Drawing.Bitmap $gif
            try {
                if ($frameBitmap.GetPixel(0, 0).A -ne 0 -or
                    $frameBitmap.GetPixel(511, 0).A -ne 0 -or
                    $frameBitmap.GetPixel(0, 511).A -ne 0 -or
                    $frameBitmap.GetPixel(511, 511).A -ne 0) {
                    $script:errors.Add("Canto opaco no GIF: $Label, frame $($index + 1)")
                }
            }
            finally {
                $frameBitmap.Dispose()
            }
        }
    }
    finally {
        $gif.Dispose()
    }

    $script:checkedGifs++
}

function Test-Sheet {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        $script:errors.Add("Sprite sheet ausente: $Label")
        return
    }

    $sheet = [System.Drawing.Image]::FromFile($Path)
    try {
        if ($sheet.Width -ne 6144 -or $sheet.Height -ne 512) {
            $script:errors.Add("Sprite sheet incorreto: $Label ($($sheet.Width)x$($sheet.Height))")
        }
    }
    finally {
        $sheet.Dispose()
    }
    $script:checkedSheets++
}

foreach ($action in $actions) {
    foreach ($direction in $directions) {
        $label = "$action/$direction"
        $directory = Join-Path $OutputRoot "animacoes-direcionais-$AnimationVersion\$action\$direction"
        $frames = @(Get-ChildItem -LiteralPath $directory -Filter 'frame-*.png' | Sort-Object Name)
        if ($frames.Count -ne 12) {
            $errors.Add("Quantidade incorreta de PNGs: $label ($($frames.Count))")
        }

        foreach ($frame in $frames) {
            $bitmap = [System.Drawing.Image]::FromFile($frame.FullName)
            try {
                if ($bitmap.Width -ne 512 -or $bitmap.Height -ne 512) {
                    $errors.Add("Frame com tamanho incorreto: $label/$($frame.Name) ($($bitmap.Width)x$($bitmap.Height))")
                }
            }
            finally {
                $bitmap.Dispose()
            }

            $inspection = [AlphaInspectorV5]::Inspect($frame.FullName)
            if ($inspection.VisiblePixels -eq 0) {
                $errors.Add("Frame da personagem vazio: $label/$($frame.Name)")
            }
            if (-not $inspection.BorderIsTransparent) {
                $errors.Add("Personagem encostando na borda: $label/$($frame.Name)")
            }
            if ($inspection.MinimumMargin -lt $minimumCharacterMargin) {
                $minimumCharacterMargin = $inspection.MinimumMargin
            }
            $darkPixelRecords.Add([pscustomobject]@{
                Frame = "$label/$($frame.Name)"
                OpaqueDarkPixels = $inspection.OpaqueDarkPixels
            })
            $characterFrames++
        }

        Test-Sheet -Path (Join-Path $OutputRoot "sprites\lylu-$action-$direction-12frames.png") -Label $label
        Test-Gif -Path (Join-Path $OutputRoot "gifs-corrigidos\lylu-$action-$direction.gif") -Label $label
    }
}

if (-not $SkipEffects) {
    foreach ($direction in $directions) {
        $label = "bolhas-efeito/$direction"
        $directory = Join-Path $OutputRoot "efeitos-bolhas\$direction"
        $frames = @(Get-ChildItem -LiteralPath $directory -Filter 'frame-*.png' | Sort-Object Name)
        if ($frames.Count -ne 12) {
            $errors.Add("Quantidade incorreta de PNGs: $label ($($frames.Count))")
        }

        for ($index = 0; $index -lt $frames.Count; $index++) {
            $frame = $frames[$index]
            $bitmap = [System.Drawing.Image]::FromFile($frame.FullName)
            try {
                if ($bitmap.Width -ne 512 -or $bitmap.Height -ne 512) {
                    $errors.Add("Frame com tamanho incorreto: $label/$($frame.Name) ($($bitmap.Width)x$($bitmap.Height))")
                }
            }
            finally {
                $bitmap.Dispose()
            }

            $inspection = [AlphaInspectorV5]::Inspect($frame.FullName)
            if ($index -lt 5 -and $inspection.VisiblePixels -ne 0) {
                $errors.Add("Efeito apareceu antes do sopro: $label/$($frame.Name)")
            }
            if ($index -ge 5 -and $inspection.VisiblePixels -eq 0) {
                $errors.Add("Efeito ausente após o sopro: $label/$($frame.Name)")
            }
            if (-not $inspection.BorderIsTransparent) {
                $errors.Add("Bolha encostando na borda: $label/$($frame.Name)")
            }
            if ($inspection.VisiblePixels -gt 0 -and $inspection.MinimumMargin -lt $minimumEffectMargin) {
                $minimumEffectMargin = $inspection.MinimumMargin
            }
            $effectFrames++
        }

        Test-Sheet -Path (Join-Path $OutputRoot "sprites\lylu-bolhas-efeito-$direction-12frames.png") -Label $label
        Test-Gif -Path (Join-Path $OutputRoot "gifs-corrigidos\lylu-bolhas-efeito-$direction.gif") -Label $label
        $effectSets++
    }
}

$result = [pscustomobject]@{
    AnimationVersion = $AnimationVersion
    CharacterSets = $actions.Count * $directions.Count
    CharacterFrames = $characterFrames
    EffectSets = $effectSets
    EffectFrames = $effectFrames
    TotalFrames = $characterFrames + $effectFrames
    SpriteSheets = $checkedSheets
    Gifs = $checkedGifs
    MinimumCharacterMargin = $minimumCharacterMargin
    MinimumEffectMargin = $minimumEffectMargin
    Errors = $errors.Count
}

$result | Format-List
$suspiciousDarkFrames = @($darkPixelRecords | Where-Object { $_.OpaqueDarkPixels -ge 12000 } | Sort-Object OpaqueDarkPixels -Descending)
if ($suspiciousDarkFrames.Count -gt 0) {
    Write-Host 'Quadros suspeitos com grande área preta opaca:'
    $suspiciousDarkFrames | Format-Table -AutoSize
}
if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}
