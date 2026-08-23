param(
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $PSScriptRoot '..\output'
}

Add-Type -AssemblyName System.Drawing

$previewDirectory = Join-Path $OutputRoot 'previews'
New-Item -ItemType Directory -Force -Path $previewDirectory | Out-Null

foreach ($direction in @('frente', 'costas', 'direita', 'esquerda')) {
    $characterPath = Join-Path $OutputRoot "sprites\lylu-soprando-bolhas-$direction-12frames.png"
    $effectPath = Join-Path $OutputRoot "sprites\lylu-bolhas-efeito-$direction-12frames.png"
    $previewPath = Join-Path $previewDirectory "lylu-soprando-bolhas-$direction-composicao-12frames.png"

    $character = [System.Drawing.Image]::FromFile($characterPath)
    $effect = [System.Drawing.Image]::FromFile($effectPath)
    try {
        if ($character.Width -ne 6144 -or $character.Height -ne 512 -or
            $effect.Width -ne 6144 -or $effect.Height -ne 512) {
            throw "Sprite sheet inválido em $direction."
        }

        $preview = New-Object System.Drawing.Bitmap 6144, 512, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($preview)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.DrawImageUnscaled($character, 0, 0)
                $graphics.DrawImageUnscaled($effect, 0, 0)
            }
            finally {
                $graphics.Dispose()
            }
            $preview.Save($previewPath, [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $preview.Dispose()
        }
    }
    finally {
        $character.Dispose()
        $effect.Dispose()
    }
}

Get-ChildItem -LiteralPath $previewDirectory -Filter 'lylu-soprando-bolhas-*-composicao-12frames.png' |
    Select-Object Name, Length
