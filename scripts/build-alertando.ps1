param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\output\alertando\source-9poses.png'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\output')
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$processorSource = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class AlertSpriteBuilder
{
    private struct Bounds
    {
        public int Left, Top, Right, Bottom, Count;
        public int Width { get { return Right - Left + 1; } }
        public int Height { get { return Bottom - Top + 1; } }
    }

    private static int[] ReadPixels(Bitmap bitmap)
    {
        Rectangle rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
        BitmapData data = bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        try
        {
            int[] pixels = new int[bitmap.Width * bitmap.Height];
            Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);
            return pixels;
        }
        finally { bitmap.UnlockBits(data); }
    }

    private static void WritePixels(Bitmap bitmap, int[] pixels)
    {
        Rectangle rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
        BitmapData data = bitmap.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        try { Marshal.Copy(pixels, 0, data.Scan0, pixels.Length); }
        finally { bitmap.UnlockBits(data); }
    }

    private static bool IsBackdrop(int argb)
    {
        int b = argb & 255;
        int g = (argb >> 8) & 255;
        int r = (argb >> 16) & 255;
        int min = Math.Min(r, Math.Min(g, b));
        int max = Math.Max(r, Math.Max(g, b));
        return min >= 200 && max - min <= 24;
    }

    private static void EnqueueBackdrop(int index, int[] pixels, bool[] visited, int[] queue, ref int tail)
    {
        if (!visited[index] && IsBackdrop(pixels[index]))
        {
            visited[index] = true;
            queue[tail++] = index;
        }
    }

    private static void RemoveConnectedBackdrop(Bitmap bitmap)
    {
        int width = bitmap.Width;
        int height = bitmap.Height;
        int[] pixels = ReadPixels(bitmap);
        bool[] visited = new bool[pixels.Length];
        int[] queue = new int[pixels.Length];
        int head = 0, tail = 0;

        for (int x = 0; x < width; x++)
        {
            EnqueueBackdrop(x, pixels, visited, queue, ref tail);
            EnqueueBackdrop((height - 1) * width + x, pixels, visited, queue, ref tail);
        }
        for (int y = 0; y < height; y++)
        {
            EnqueueBackdrop(y * width, pixels, visited, queue, ref tail);
            EnqueueBackdrop(y * width + width - 1, pixels, visited, queue, ref tail);
        }

        while (head < tail)
        {
            int index = queue[head++];
            int x = index % width;
            int y = index / width;
            if (x > 0) EnqueueBackdrop(index - 1, pixels, visited, queue, ref tail);
            if (x + 1 < width) EnqueueBackdrop(index + 1, pixels, visited, queue, ref tail);
            if (y > 0) EnqueueBackdrop(index - width, pixels, visited, queue, ref tail);
            if (y + 1 < height) EnqueueBackdrop(index + width, pixels, visited, queue, ref tail);
        }

        for (int i = 0; i < pixels.Length; i++)
        {
            if (visited[i]) pixels[i] = 0;
            else pixels[i] = unchecked((int)0xFF000000) | (pixels[i] & 0x00FFFFFF);
        }
        WritePixels(bitmap, pixels);
    }

    private static Bounds FindLargestComponent(Bitmap bitmap)
    {
        int width = bitmap.Width;
        int height = bitmap.Height;
        int[] pixels = ReadPixels(bitmap);
        bool[] visited = new bool[pixels.Length];
        int[] queue = new int[pixels.Length];
        Bounds best = new Bounds { Count = 0 };

        for (int start = 0; start < pixels.Length; start++)
        {
            if (visited[start] || ((uint)pixels[start] >> 24) == 0) continue;
            int head = 0, tail = 0;
            queue[tail++] = start;
            visited[start] = true;
            int left = width, top = height, right = 0, bottom = 0, count = 0;

            while (head < tail)
            {
                int index = queue[head++];
                int x = index % width;
                int y = index / width;
                left = Math.Min(left, x); right = Math.Max(right, x);
                top = Math.Min(top, y); bottom = Math.Max(bottom, y);
                count++;

                int neighbor;
                if (x > 0)
                {
                    neighbor = index - 1;
                    if (!visited[neighbor] && ((uint)pixels[neighbor] >> 24) != 0) { visited[neighbor] = true; queue[tail++] = neighbor; }
                }
                if (x + 1 < width)
                {
                    neighbor = index + 1;
                    if (!visited[neighbor] && ((uint)pixels[neighbor] >> 24) != 0) { visited[neighbor] = true; queue[tail++] = neighbor; }
                }
                if (y > 0)
                {
                    neighbor = index - width;
                    if (!visited[neighbor] && ((uint)pixels[neighbor] >> 24) != 0) { visited[neighbor] = true; queue[tail++] = neighbor; }
                }
                if (y + 1 < height)
                {
                    neighbor = index + width;
                    if (!visited[neighbor] && ((uint)pixels[neighbor] >> 24) != 0) { visited[neighbor] = true; queue[tail++] = neighbor; }
                }
            }

            if (count > best.Count)
                best = new Bounds { Left = left, Top = top, Right = right, Bottom = bottom, Count = count };
        }

        if (best.Count == 0) throw new InvalidOperationException("No character pixels found after background removal.");
        return best;
    }

    private static Bounds FindContentBounds(Bitmap bitmap)
    {
        int width = bitmap.Width;
        int height = bitmap.Height;
        int[] pixels = ReadPixels(bitmap);
        int left = width, top = height, right = -1, bottom = -1, count = 0;
        for (int i = 0; i < pixels.Length; i++)
        {
            if (((uint)pixels[i] >> 24) == 0) continue;
            int x = i % width;
            int y = i / width;
            left = Math.Min(left, x); right = Math.Max(right, x);
            top = Math.Min(top, y); bottom = Math.Max(bottom, y);
            count++;
        }
        return new Bounds { Left = left, Top = top, Right = right, Bottom = bottom, Count = count };
    }

    private static void RemoveFragmentsBelowCharacter(Bitmap bitmap, Bounds characterBounds)
    {
        int[] pixels = ReadPixels(bitmap);
        int firstForbiddenRow = Math.Min(bitmap.Height, characterBounds.Bottom + 7);
        for (int y = firstForbiddenRow; y < bitmap.Height; y++)
        {
            int rowStart = y * bitmap.Width;
            for (int x = 0; x < bitmap.Width; x++)
                pixels[rowStart + x] = 0;
        }
        WritePixels(bitmap, pixels);
    }

    private static Bitmap ExtractPose(Bitmap source, int column, int row, int cellWidth, int cellHeight)
    {
        Bitmap pose = new Bitmap(cellWidth, cellHeight, PixelFormat.Format32bppArgb);
        using (Graphics graphics = Graphics.FromImage(pose))
        {
            graphics.CompositingMode = CompositingMode.SourceCopy;
            graphics.DrawImage(source,
                new Rectangle(0, 0, cellWidth, cellHeight),
                new Rectangle(column * cellWidth, row * cellHeight, cellWidth, cellHeight),
                GraphicsUnit.Pixel);
        }
        RemoveConnectedBackdrop(pose);
        return pose;
    }

    private static Bitmap ComposeFrame(Bitmap pose, Bounds characterBounds)
    {
        const int frameSize = 512;
        const int safeMargin = 38;
        const float scale = 1.08f;
        const float characterGroundY = 466f;

        Bounds content = FindContentBounds(pose);
        float x = 256f - scale * (characterBounds.Left + characterBounds.Width / 2f);
        float y = characterGroundY - scale * (characterBounds.Bottom + 1f);

        float contentLeft = x + scale * content.Left;
        float contentRight = x + scale * (content.Right + 1);
        float contentTop = y + scale * content.Top;
        float contentBottom = y + scale * (content.Bottom + 1);

        if (contentLeft < safeMargin) x += safeMargin - contentLeft;
        if (contentRight > frameSize - safeMargin) x -= contentRight - (frameSize - safeMargin);
        if (contentTop < safeMargin) y += safeMargin - contentTop;
        if (contentBottom > frameSize - safeMargin) y -= contentBottom - (frameSize - safeMargin);

        Bitmap frame = new Bitmap(frameSize, frameSize, PixelFormat.Format32bppArgb);
        using (Graphics graphics = Graphics.FromImage(frame))
        {
            graphics.Clear(Color.Transparent);
            graphics.CompositingMode = CompositingMode.SourceCopy;
            graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
            graphics.PixelOffsetMode = PixelOffsetMode.Half;
            graphics.DrawImage(pose,
                new RectangleF(x, y, pose.Width * scale, pose.Height * scale),
                new RectangleF(0, 0, pose.Width, pose.Height),
                GraphicsUnit.Pixel);
        }
        return frame;
    }

    public static string AnalyzePng(string path)
    {
        using (Bitmap bitmap = new Bitmap(path))
        {
            int[] pixels = ReadPixels(bitmap);
            Bounds bounds = FindContentBounds(bitmap);
            int borderOpaque = 0;
            for (int x = 0; x < bitmap.Width; x++)
            {
                if (((uint)pixels[x] >> 24) != 0) borderOpaque++;
                if (((uint)pixels[(bitmap.Height - 1) * bitmap.Width + x] >> 24) != 0) borderOpaque++;
            }
            for (int y = 1; y < bitmap.Height - 1; y++)
            {
                if (((uint)pixels[y * bitmap.Width] >> 24) != 0) borderOpaque++;
                if (((uint)pixels[y * bitmap.Width + bitmap.Width - 1] >> 24) != 0) borderOpaque++;
            }
            return String.Format(
                "{0}: {1}x{2}; bbox=({3},{4})-({5},{6}); margins={3}/{4}/{7}/{8}; opaque-border={9}",
                Path.GetFileName(path), bitmap.Width, bitmap.Height,
                bounds.Left, bounds.Top, bounds.Right, bounds.Bottom,
                bitmap.Width - 1 - bounds.Right, bitmap.Height - 1 - bounds.Bottom,
                borderOpaque);
        }
    }

    public static string[] Build(string sourcePath, string frameDirectory, string spritePath)
    {
        Directory.CreateDirectory(frameDirectory);
        string[] existingFrames = Directory.GetFiles(frameDirectory, "frame-*.png");
        foreach (string existing in existingFrames) File.Delete(existing);

        using (Bitmap source = new Bitmap(sourcePath))
        {
            if (source.Width % 3 != 0 || source.Height % 3 != 0)
                throw new InvalidOperationException("The generated source must be a 3x3 sheet.");

            int cellWidth = source.Width / 3;
            int cellHeight = source.Height / 3;
            List<Bitmap> poses = new List<Bitmap>();
            List<Bounds> characterBounds = new List<Bounds>();

            for (int row = 0; row < 3; row++)
            {
                for (int column = 0; column < 3; column++)
                {
                    Bitmap pose = ExtractPose(source, column, row, cellWidth, cellHeight);
                    Bounds initialCharacterBounds = FindLargestComponent(pose);
                    RemoveFragmentsBelowCharacter(pose, initialCharacterBounds);
                    poses.Add(pose);
                    characterBounds.Add(FindLargestComponent(pose));
                }
            }

            // Anticipation -> warning peak -> held peak -> recovery -> friendly settle.
            int[] sequence = { 0, 1, 2, 3, 4, 5, 6, 6, 5, 4, 7, 8 };
            string[] framePaths = new string[sequence.Length];
            List<Bitmap> frames = new List<Bitmap>();
            try
            {
                for (int i = 0; i < sequence.Length; i++)
                {
                    int poseIndex = sequence[i];
                    Bitmap frame = ComposeFrame(poses[poseIndex], characterBounds[poseIndex]);
                    frames.Add(frame);
                    string path = Path.Combine(frameDirectory, String.Format("frame-{0:00}.png", i + 1));
                    frame.Save(path, ImageFormat.Png);
                    framePaths[i] = path;
                }

                using (Bitmap sheet = new Bitmap(512 * frames.Count, 512, PixelFormat.Format32bppArgb))
                using (Graphics graphics = Graphics.FromImage(sheet))
                {
                    graphics.Clear(Color.Transparent);
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    for (int i = 0; i < frames.Count; i++)
                        graphics.DrawImageUnscaled(frames[i], i * 512, 0);
                    sheet.Save(spritePath, ImageFormat.Png);
                }
            }
            finally
            {
                foreach (Bitmap frame in frames) frame.Dispose();
                foreach (Bitmap pose in poses) pose.Dispose();
            }
            return framePaths;
        }
    }
}
'@

Add-Type -TypeDefinition $processorSource -ReferencedAssemblies System.Drawing

$alertDirectory = Join-Path $OutputRoot 'alertando'
$spriteDirectory = Join-Path $OutputRoot 'sprites'
$gifDirectory = Join-Path $OutputRoot 'gifs-corrigidos'
New-Item -ItemType Directory -Force -Path $alertDirectory, $spriteDirectory, $gifDirectory | Out-Null

$spritePath = Join-Path $spriteDirectory 'lylu-alertando-12frames-v2.png'
$framePaths = [AlertSpriteBuilder]::Build(
    (Resolve-Path -LiteralPath $SourcePath).Path,
    $alertDirectory,
    $spritePath
)
Copy-Item -LiteralPath $spritePath -Destination (Join-Path $spriteDirectory 'lylu-alertando-12frames.png') -Force
Copy-Item -LiteralPath $spritePath -Destination (Join-Path $alertDirectory 'lylu-alertando-12frames-v2.png') -Force

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

$gifPath = Join-Path $gifDirectory 'lylu-alertando-corrigido-v3.gif'
$encoder = [System.Windows.Media.Imaging.GifBitmapEncoder]::new()
$delays = @(14, 9, 9, 9, 8, 8, 16, 14, 8, 8, 12, 24)

for ($index = 0; $index -lt $framePaths.Count; $index++) {
    $stream = [System.IO.File]::OpenRead($framePaths[$index])
    try {
        $decoder = [System.Windows.Media.Imaging.PngBitmapDecoder]::new(
            $stream,
            [System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,
            [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
        )
        $metadata = [System.Windows.Media.Imaging.BitmapMetadata]::new('gif')
        $metadata.SetQuery('/grctlext/Delay', [uint16]$delays[$index])
        $metadata.SetQuery('/grctlext/Disposal', [byte]2)
        $frame = [System.Windows.Media.Imaging.BitmapFrame]::Create(
            $decoder.Frames[0],
            $decoder.Frames[0].Thumbnail,
            $metadata,
            $decoder.Frames[0].ColorContexts
        )
        $encoder.Frames.Add($frame)
    }
    finally { $stream.Dispose() }
}

$outputStream = [System.IO.File]::Create($gifPath)
try { $encoder.Save($outputStream) }
finally { $outputStream.Dispose() }

# WPF writes all frames correctly but does not support global GIF metadata.
# Insert the standard NETSCAPE2.0 loop extension immediately after the global palette.
$gifBytes = [System.IO.File]::ReadAllBytes($gifPath)
$graphicControlCount = 0
for ($offset = 0; $offset -le $gifBytes.Length - 8; $offset++) {
    if ($gifBytes[$offset] -eq 0x21 -and
        $gifBytes[$offset + 1] -eq 0xF9 -and
        $gifBytes[$offset + 2] -eq 0x04) {
        if ($graphicControlCount -ge $delays.Count) {
            throw 'The GIF contains more graphic-control blocks than expected.'
        }
        # Disposal 2 clears the previous full-size frame before the next one is drawn.
        $gifBytes[$offset + 3] = [byte](($gifBytes[$offset + 3] -band 0xE3) -bor 0x08 -bor 0x01)
        $delay = [uint16]$delays[$graphicControlCount]
        $gifBytes[$offset + 4] = [byte]($delay -band 0xFF)
        $gifBytes[$offset + 5] = [byte](($delay -shr 8) -band 0xFF)
        $graphicControlCount++
        $offset += 7
    }
}
if ($graphicControlCount -ne $framePaths.Count) {
    throw "Expected $($framePaths.Count) graphic-control blocks, found $graphicControlCount."
}
$packedField = $gifBytes[10]
$globalPaletteLength = if (($packedField -band 0x80) -ne 0) {
    3 * [math]::Pow(2, (($packedField -band 0x07) + 1))
} else { 0 }
$insertAt = 13 + [int]$globalPaletteLength
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

Copy-Item -LiteralPath $gifPath -Destination (Join-Path $gifDirectory 'lylu-alertando.gif') -Force
Copy-Item -LiteralPath $gifPath -Destination (Join-Path $alertDirectory 'lylu-alertando-corrigido-v3.gif') -Force
Copy-Item -LiteralPath $gifPath -Destination (Join-Path $alertDirectory 'lylu-alertando.gif') -Force

$validation = foreach ($framePath in $framePaths) {
    [AlertSpriteBuilder]::AnalyzePng($framePath)
}
$gifImage = [System.Drawing.Image]::FromFile($gifPath)
try {
    $gifDimension = [System.Drawing.Imaging.FrameDimension]::new($gifImage.FrameDimensionsList[0])
    $gifFrameCount = $gifImage.GetFrameCount($gifDimension)
}
finally { $gifImage.Dispose() }

[pscustomobject]@{
    SpriteSheet = $spritePath
    AnimatedGif = $gifPath
    Frames = $framePaths.Count
    GifFrames = $gifFrameCount
    Validation = ($validation -join [Environment]::NewLine)
}
