param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('frente', 'costas', 'direita', 'esquerda')]
    [string]$Direction,
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $PSScriptRoot '..\output'
}
Add-Type -AssemblyName System.Drawing

$effectSource = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

public static class BubbleEffectBuilder
{
    private struct Bubble
    {
        public int Birth;
        public float XSpeed;
        public float YSpeed;
        public float Radius;
        public Bubble(int birth, float xSpeed, float ySpeed, float radius)
        {
            Birth = birth; XSpeed = xSpeed; YSpeed = ySpeed; Radius = radius;
        }
    }

    private static void DrawBubble(Graphics g, float x, float y, float radius, int alpha)
    {
        if (alpha <= 0 || radius <= 0) return;
        int fillAlpha = Math.Max(12, alpha / 5);
        using (Brush fill = new SolidBrush(Color.FromArgb(fillAlpha, 105, 220, 245)))
            g.FillEllipse(fill, x - radius, y - radius, radius * 2, radius * 2);

        using (Pen outline = new Pen(Color.FromArgb(alpha, 93, 218, 238), Math.Max(2f, radius * 0.16f)))
            g.DrawEllipse(outline, x - radius, y - radius, radius * 2, radius * 2);

        using (Pen violet = new Pen(Color.FromArgb(Math.Max(40, alpha - 35), 218, 126, 245), Math.Max(1.5f, radius * 0.11f)))
            g.DrawArc(violet, x - radius + 1, y - radius + 1, radius * 2 - 2, radius * 2 - 2, 18, 92);

        using (Pen shine = new Pen(Color.FromArgb(Math.Min(255, alpha + 30), 255, 255, 255), Math.Max(1.5f, radius * 0.12f)))
            g.DrawArc(shine, x - radius * 0.58f, y - radius * 0.62f, radius * 1.15f, radius * 1.15f, 192, 92);
    }

    public static string[] Build(string direction, string frameDirectory, string spritePath)
    {
        float startX, startY, sign, horizontalScale;
        switch (direction)
        {
            case "direita": startX = 362; startY = 226; sign = 1; horizontalScale = 1; break;
            case "esquerda": startX = 150; startY = 226; sign = -1; horizontalScale = 1; break;
            case "costas": startX = 389; startY = 187; sign = 1; horizontalScale = 0.55f; break;
            default: startX = 258; startY = 223; sign = 1; horizontalScale = 1; break;
        }

        Bubble[] bubbles = new Bubble[] {
            new Bubble(6, 12.5f, 10.5f, 5.0f),
            new Bubble(8, 10.0f, 11.5f, 4.3f),
            new Bubble(9, 7.5f, 9.5f, 3.7f)
        };

        Directory.CreateDirectory(frameDirectory);
        List<Bitmap> frames = new List<Bitmap>();
        string[] paths = new string[12];
        try
        {
            for (int frameNumber = 1; frameNumber <= 12; frameNumber++)
            {
                Bitmap frame = new Bitmap(512, 512, PixelFormat.Format32bppArgb);
                using (Graphics g = Graphics.FromImage(frame))
                {
                    g.Clear(Color.Transparent);
                    g.CompositingMode = CompositingMode.SourceOver;
                    g.CompositingQuality = CompositingQuality.HighQuality;
                    g.SmoothingMode = SmoothingMode.AntiAlias;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;

                    foreach (Bubble bubble in bubbles)
                    {
                        int age = frameNumber - bubble.Birth;
                        if (age < 0) continue;
                        float curve = age * age * 0.72f;
                        float x = startX + sign * (bubble.XSpeed * age + curve) * horizontalScale;
                        float y = startY - bubble.YSpeed * age - curve * 0.55f;
                        float radius = bubble.Radius + age * 1.65f;
                        int alpha = Math.Max(48, 232 - age * 30);
                        DrawBubble(g, x, y, radius, alpha);
                    }
                }

                string path = Path.Combine(frameDirectory, String.Format("frame-{0:00}.png", frameNumber));
                frame.Save(path, ImageFormat.Png);
                frames.Add(frame);
                paths[frameNumber - 1] = path;
            }

            using (Bitmap sheet = new Bitmap(512 * 12, 512, PixelFormat.Format32bppArgb))
            using (Graphics g = Graphics.FromImage(sheet))
            {
                g.Clear(Color.Transparent);
                g.CompositingMode = CompositingMode.SourceCopy;
                for (int i = 0; i < frames.Count; i++) g.DrawImageUnscaled(frames[i], i * 512, 0);
                sheet.Save(spritePath, ImageFormat.Png);
            }
        }
        finally
        {
            foreach (Bitmap frame in frames) frame.Dispose();
        }
        return paths;
    }
}
'@

Add-Type -TypeDefinition $effectSource -ReferencedAssemblies System.Drawing

$frameDirectory = Join-Path $OutputRoot ("efeitos-bolhas\" + $Direction)
$spriteDirectory = Join-Path $OutputRoot 'sprites'
$gifDirectory = Join-Path $OutputRoot 'gifs-corrigidos'
New-Item -ItemType Directory -Force -Path $frameDirectory, $spriteDirectory, $gifDirectory | Out-Null

$baseName = 'lylu-bolhas-efeito-' + $Direction
$spritePath = Join-Path $spriteDirectory ($baseName + '-12frames.png')
$gifPath = Join-Path $gifDirectory ($baseName + '.gif')
$framePaths = [BubbleEffectBuilder]::Build($Direction, $frameDirectory, $spritePath)
Copy-Item -LiteralPath $spritePath -Destination (Join-Path $frameDirectory ($baseName + '-12frames.png')) -Force

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase
$encoder = [System.Windows.Media.Imaging.GifBitmapEncoder]::new()
foreach ($framePath in $framePaths) {
    $stream = [System.IO.File]::OpenRead($framePath)
    try {
        $decoder = [System.Windows.Media.Imaging.PngBitmapDecoder]::new(
            $stream,
            [System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,
            [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
        )
        $metadata = [System.Windows.Media.Imaging.BitmapMetadata]::new('gif')
        $frame = [System.Windows.Media.Imaging.BitmapFrame]::Create(
            $decoder.Frames[0], $decoder.Frames[0].Thumbnail, $metadata, $decoder.Frames[0].ColorContexts
        )
        $encoder.Frames.Add($frame)
    }
    finally { $stream.Dispose() }
}

$outputStream = [System.IO.File]::Create($gifPath)
try { $encoder.Save($outputStream) }
finally { $outputStream.Dispose() }

# Set 80 ms per frame, disposal=restore background, transparency on, and infinite loop.
$gifBytes = [System.IO.File]::ReadAllBytes($gifPath)
$packedField = $gifBytes[10]
$globalPaletteLength = if (($packedField -band 0x80) -ne 0) {
    3 * [math]::Pow(2, (($packedField -band 0x07) + 1))
} else { 0 }
$insertAt = 13 + [int]$globalPaletteLength
$graphicControlCount = 0
$cursor = $insertAt
while ($cursor -lt $gifBytes.Length) {
    $marker = $gifBytes[$cursor]
    if ($marker -eq 0x21) {
        $extensionLabel = $gifBytes[$cursor + 1]
        if ($extensionLabel -eq 0xF9) {
            $gifBytes[$cursor + 3] = [byte](($gifBytes[$cursor + 3] -band 0xE3) -bor 0x08 -bor 0x01)
            $gifBytes[$cursor + 4] = 8
            $gifBytes[$cursor + 5] = 0
            $graphicControlCount++
        }
        $cursor += 2
        while ($cursor -lt $gifBytes.Length) {
            $blockLength = [int]$gifBytes[$cursor]
            $cursor++
            if ($blockLength -eq 0) { break }
            $cursor += $blockLength
        }
    }
    elseif ($marker -eq 0x2C) {
        $imagePackedField = $gifBytes[$cursor + 9]
        $cursor += 10
        if (($imagePackedField -band 0x80) -ne 0) {
            $cursor += 3 * [math]::Pow(2, (($imagePackedField -band 0x07) + 1))
        }
        $cursor++
        while ($cursor -lt $gifBytes.Length) {
            $blockLength = [int]$gifBytes[$cursor]
            $cursor++
            if ($blockLength -eq 0) { break }
            $cursor += $blockLength
        }
    }
    elseif ($marker -eq 0x3B) { break }
    else { throw ('Unexpected GIF marker 0x{0:X2} at byte {1}.' -f $marker, $cursor) }
}
if ($graphicControlCount -ne 12) { throw "Expected 12 GIF frames, found $graphicControlCount." }

$loopExtension = [byte[]](
    0x21, 0xFF, 0x0B,
    0x4E, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30,
    0x03, 0x01, 0x00, 0x00, 0x00
)
$loopingGif = [byte[]]::new($gifBytes.Length + $loopExtension.Length)
[Array]::Copy($gifBytes, 0, $loopingGif, 0, $insertAt)
[Array]::Copy($loopExtension, 0, $loopingGif, $insertAt, $loopExtension.Length)
[Array]::Copy($gifBytes, $insertAt, $loopingGif, $insertAt + $loopExtension.Length, $gifBytes.Length - $insertAt)
[System.IO.File]::WriteAllBytes($gifPath, $loopingGif)
Copy-Item -LiteralPath $gifPath -Destination (Join-Path $frameDirectory ($baseName + '.gif')) -Force

[pscustomobject]@{
    Direction = $Direction
    Frames = $framePaths.Count
    SpriteSheet = $spritePath
    AnimatedGif = $gifPath
}
