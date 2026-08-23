param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\output\andando-cabelo-preso\source-12poses.png'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\output'),
    [string]$AssetDirectoryName = 'andando-cabelo-preso',
    [string]$OutputBaseName = 'lylu-andando-cabelo-preso',
    [switch]$MirrorHorizontal
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$builderSource = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class WalkingSpriteBuilder
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
        return min >= 180 && max - min <= 30;
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

                int next;
                if (x > 0)
                {
                    next = index - 1;
                    if (!visited[next] && ((uint)pixels[next] >> 24) != 0) { visited[next] = true; queue[tail++] = next; }
                }
                if (x + 1 < width)
                {
                    next = index + 1;
                    if (!visited[next] && ((uint)pixels[next] >> 24) != 0) { visited[next] = true; queue[tail++] = next; }
                }
                if (y > 0)
                {
                    next = index - width;
                    if (!visited[next] && ((uint)pixels[next] >> 24) != 0) { visited[next] = true; queue[tail++] = next; }
                }
                if (y + 1 < height)
                {
                    next = index + width;
                    if (!visited[next] && ((uint)pixels[next] >> 24) != 0) { visited[next] = true; queue[tail++] = next; }
                }
            }

            if (count > best.Count)
                best = new Bounds { Left = left, Top = top, Right = right, Bottom = bottom, Count = count };
        }

        if (best.Count == 0) throw new InvalidOperationException("No character pixels found.");
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

    private static void KeepLargestComponent(Bitmap bitmap)
    {
        int width = bitmap.Width;
        int height = bitmap.Height;
        int[] pixels = ReadPixels(bitmap);
        bool[] visited = new bool[pixels.Length];
        int[] queue = new int[pixels.Length];
        List<int> largest = new List<int>();

        for (int start = 0; start < pixels.Length; start++)
        {
            if (visited[start] || ((uint)pixels[start] >> 24) == 0) continue;
            int head = 0, tail = 0;
            List<int> component = new List<int>();
            queue[tail++] = start;
            visited[start] = true;

            while (head < tail)
            {
                int index = queue[head++];
                component.Add(index);
                int x = index % width;
                int y = index / width;

                for (int offsetY = -1; offsetY <= 1; offsetY++)
                {
                    int nextY = y + offsetY;
                    if (nextY < 0 || nextY >= height) continue;
                    for (int offsetX = -1; offsetX <= 1; offsetX++)
                    {
                        if (offsetX == 0 && offsetY == 0) continue;
                        int nextX = x + offsetX;
                        if (nextX < 0 || nextX >= width) continue;
                        int next = nextY * width + nextX;
                        if (!visited[next] && ((uint)pixels[next] >> 24) != 0)
                        {
                            visited[next] = true;
                            queue[tail++] = next;
                        }
                    }
                }
            }

            if (component.Count > largest.Count) largest = component;
        }

        if (largest.Count == 0) throw new InvalidOperationException("No character pixels found.");
        bool[] keep = new bool[pixels.Length];
        foreach (int index in largest) keep[index] = true;
        for (int i = 0; i < pixels.Length; i++)
            if (!keep[i]) pixels[i] = 0;
        WritePixels(bitmap, pixels);
    }

    private static void RemoveFragmentsBelowCharacter(Bitmap bitmap, Bounds characterBounds)
    {
        int[] pixels = ReadPixels(bitmap);
        int firstForbiddenRow = Math.Min(bitmap.Height, characterBounds.Bottom + 7);
        for (int y = firstForbiddenRow; y < bitmap.Height; y++)
        {
            int rowStart = y * bitmap.Width;
            for (int x = 0; x < bitmap.Width; x++) pixels[rowStart + x] = 0;
        }
        WritePixels(bitmap, pixels);
    }

    private static Bitmap ExtractPose(Bitmap source, int sourceX, int sourceY, int cellWidth, int cellHeight)
    {
        Bitmap pose = new Bitmap(cellWidth, cellHeight, PixelFormat.Format32bppArgb);
        using (Graphics graphics = Graphics.FromImage(pose))
        {
            graphics.CompositingMode = CompositingMode.SourceCopy;
            graphics.DrawImage(source,
                new Rectangle(0, 0, cellWidth, cellHeight),
                new Rectangle(sourceX, sourceY, cellWidth, cellHeight),
                GraphicsUnit.Pixel);
        }
        RemoveConnectedBackdrop(pose);
        KeepLargestComponent(pose);
        return pose;
    }

    private static Bitmap ComposeFrame(Bitmap pose, float scale)
    {
        const int frameSize = 512;
        const int safeMargin = 42;
        const float groundY = 462f;
        Bounds content = FindContentBounds(pose);

        float x = 256f - scale * ((content.Left + content.Right + 1) / 2f);
        float y = groundY - scale * (content.Bottom + 1f);
        float left = x + scale * content.Left;
        float right = x + scale * (content.Right + 1);
        float top = y + scale * content.Top;
        float bottom = y + scale * (content.Bottom + 1);

        if (left < safeMargin) x += safeMargin - left;
        if (right > frameSize - safeMargin) x -= right - (frameSize - safeMargin);
        if (top < safeMargin) y += safeMargin - top;
        if (bottom > frameSize - safeMargin) y -= bottom - (frameSize - safeMargin);

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
            Bounds bounds = FindContentBounds(bitmap);
            int[] pixels = ReadPixels(bitmap);
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

    public static string[] Build(string sourcePath, string frameDirectory, string spritePath, bool mirrorHorizontal)
    {
        Directory.CreateDirectory(frameDirectory);
        foreach (string oldFrame in Directory.GetFiles(frameDirectory, "frame-*.png")) File.Delete(oldFrame);

        using (Bitmap source = new Bitmap(sourcePath))
        {
            List<Bitmap> poses = new List<Bitmap>();
            List<Bounds> characterBounds = new List<Bounds>();
            List<int> heights = new List<int>();
            int widestPose = 1;

            for (int row = 0; row < 3; row++)
            {
                for (int column = 0; column < 4; column++)
                {
                    int x0 = (int)Math.Round(column * source.Width / 4.0, MidpointRounding.AwayFromZero);
                    int x1 = (int)Math.Round((column + 1) * source.Width / 4.0, MidpointRounding.AwayFromZero);
                    int y0 = (int)Math.Round(row * source.Height / 3.0, MidpointRounding.AwayFromZero);
                    int y1 = (int)Math.Round((row + 1) * source.Height / 3.0, MidpointRounding.AwayFromZero);
                    Bitmap pose = ExtractPose(source, x0, y0, x1 - x0, y1 - y0);
                    Bounds character = FindLargestComponent(pose);
                    poses.Add(pose);
                    characterBounds.Add(character);
                    heights.Add(character.Height);
                    widestPose = Math.Max(widestPose, character.Width);
                }
            }

            heights.Sort();
            float medianHeight = (heights[5] + heights[6]) / 2f;
            float scale = Math.Min(1.24f, 376f / medianHeight);
            scale = Math.Min(scale, 420f / widestPose);
            List<Bitmap> frames = new List<Bitmap>();
            string[] paths = new string[12];
            try
            {
                for (int i = 0; i < 12; i++)
                {
                    Bitmap frame = ComposeFrame(poses[i], scale);
                    if (mirrorHorizontal)
                        frame.RotateFlip(RotateFlipType.RotateNoneFlipX);
                    frames.Add(frame);
                    string path = Path.Combine(frameDirectory, String.Format("frame-{0:00}.png", i + 1));
                    frame.Save(path, ImageFormat.Png);
                    paths[i] = path;
                }

                using (Bitmap sheet = new Bitmap(512 * 12, 512, PixelFormat.Format32bppArgb))
                using (Graphics graphics = Graphics.FromImage(sheet))
                {
                    graphics.Clear(Color.Transparent);
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    for (int i = 0; i < 12; i++) graphics.DrawImageUnscaled(frames[i], i * 512, 0);
                    sheet.Save(spritePath, ImageFormat.Png);
                }
            }
            finally
            {
                foreach (Bitmap frame in frames) frame.Dispose();
                foreach (Bitmap pose in poses) pose.Dispose();
            }
            return paths;
        }
    }
}
'@

Add-Type -TypeDefinition $builderSource -ReferencedAssemblies System.Drawing

$assetDirectory = Join-Path $OutputRoot $AssetDirectoryName
$spriteDirectory = Join-Path $OutputRoot 'sprites'
$gifDirectory = Join-Path $OutputRoot 'gifs-corrigidos'
New-Item -ItemType Directory -Force -Path $assetDirectory, $spriteDirectory, $gifDirectory | Out-Null

$spritePath = Join-Path $spriteDirectory ($OutputBaseName + '-12frames.png')
$framePaths = [WalkingSpriteBuilder]::Build(
    (Resolve-Path -LiteralPath $SourcePath).Path,
    $assetDirectory,
    $spritePath,
    $MirrorHorizontal.IsPresent
)
Copy-Item -LiteralPath $spritePath -Destination (Join-Path $assetDirectory ($OutputBaseName + '-12frames.png')) -Force

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

$gifPath = Join-Path $gifDirectory ($OutputBaseName + '.gif')
$encoder = [System.Windows.Media.Imaging.GifBitmapEncoder]::new()
$delays = @(8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8)

for ($index = 0; $index -lt $framePaths.Count; $index++) {
    $stream = [System.IO.File]::OpenRead($framePaths[$index])
    try {
        $decoder = [System.Windows.Media.Imaging.PngBitmapDecoder]::new(
            $stream,
            [System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,
            [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
        )
        $metadata = [System.Windows.Media.Imaging.BitmapMetadata]::new('gif')
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

# Set per-frame delay/disposal and add infinite looping to the GIF binary.
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
        if ($cursor + 2 -ge $gifBytes.Length) { throw 'Truncated GIF extension.' }
        $extensionLabel = $gifBytes[$cursor + 1]
        if ($extensionLabel -eq 0xF9) {
            if ($gifBytes[$cursor + 2] -ne 0x04) { throw 'Invalid GIF graphic control block.' }
            if ($graphicControlCount -ge $delays.Count) { throw 'GIF has more than 12 real frames.' }
            $gifBytes[$cursor + 3] = [byte](($gifBytes[$cursor + 3] -band 0xE3) -bor 0x08 -bor 0x01)
            $delay = [uint16]$delays[$graphicControlCount]
            $gifBytes[$cursor + 4] = [byte]($delay -band 0xFF)
            $gifBytes[$cursor + 5] = [byte](($delay -shr 8) -band 0xFF)
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
        if ($cursor + 9 -ge $gifBytes.Length) { throw 'Truncated GIF image descriptor.' }
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
if ($graphicControlCount -ne 12) { throw "Expected 12 GIF control blocks, found $graphicControlCount." }

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
Copy-Item -LiteralPath $gifPath -Destination (Join-Path $assetDirectory ($OutputBaseName + '.gif')) -Force

$validation = foreach ($framePath in $framePaths) { [WalkingSpriteBuilder]::AnalyzePng($framePath) }
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
