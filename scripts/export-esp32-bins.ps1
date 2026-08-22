param(
    [Parameter(Mandatory = $true)]
    [string]$FrameDirectory,
    [Parameter(Mandatory = $true)]
    [string]$Prefix,
    [Parameter(Mandatory = $true)]
    [int]$BackgroundR,
    [Parameter(Mandatory = $true)]
    [int]$BackgroundG,
    [Parameter(Mandatory = $true)]
    [int]$BackgroundB,
    [string]$DataDirectory = '',
    [string]$PreviewDirectory = ''
)

$ErrorActionPreference = 'Stop'
if (-not $DataDirectory) { $DataDirectory = Join-Path $PSScriptRoot '..\lylu_esp32\data' }
if (-not $PreviewDirectory) { $PreviewDirectory = Join-Path $PSScriptRoot '..\output\esp32-previews' }
Add-Type -AssemblyName System.Drawing

$converterSource = @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

public static class Esp32FrameExporter
{
    private static ushort ToRgb565(Color color)
    {
        int r = (color.R * 31 + 127) / 255;
        int g = (color.G * 63 + 127) / 255;
        int b = (color.B * 31 + 127) / 255;
        return (ushort)((r << 11) | (g << 5) | b);
    }

    public static void Export(string inputPath, string outputPath, string previewPath, int r, int g, int b)
    {
        using (Bitmap source = new Bitmap(inputPath))
        using (Bitmap target = new Bitmap(150, 150, PixelFormat.Format24bppRgb))
        {
            using (Graphics graphics = Graphics.FromImage(target))
            {
                graphics.Clear(Color.FromArgb(r, g, b));
                graphics.CompositingMode = CompositingMode.SourceOver;
                graphics.CompositingQuality = CompositingQuality.HighQuality;
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                graphics.SmoothingMode = SmoothingMode.None;
                graphics.DrawImage(source, new Rectangle(0, 0, 150, 150), new Rectangle(0, 0, source.Width, source.Height), GraphicsUnit.Pixel);
            }

            // Fundo magenta = cor-chave de transparencia. A suavizacao de borda
            // cria pixels QUASE magenta ao redor da personagem que escapam da
            // chave e viram um halo roxo na tela. Aqui, qualquer pixel bem mais
            // magenta que qualquer cor da personagem vira magenta puro.
            bool chaveMagenta = (r == 255 && g == 0 && b == 255);

            byte[] bytes = new byte[150 * 150 * 2];
            int offset = 0;
            for (int y = 0; y < 150; y++)
            {
                for (int x = 0; x < 150; x++)
                {
                    Color px = target.GetPixel(x, y);
                    if (chaveMagenta)
                    {
                        int forcaMagenta = Math.Min(px.R, px.B) - px.G;
                        if (forcaMagenta >= 100) px = Color.FromArgb(255, 0, 255);
                    }
                    ushort value = ToRgb565(px);
                    bytes[offset++] = (byte)(value >> 8);
                    bytes[offset++] = (byte)(value & 255);
                }
            }
            File.WriteAllBytes(outputPath, bytes);
            target.Save(previewPath, ImageFormat.Png);
        }
    }
}
'@

Add-Type -TypeDefinition $converterSource -ReferencedAssemblies System.Drawing

$resolvedFrames = (Resolve-Path -LiteralPath $FrameDirectory).Path
$resolvedData = [System.IO.Path]::GetFullPath($DataDirectory)
$prefixPreview = Join-Path ([System.IO.Path]::GetFullPath($PreviewDirectory)) $Prefix
New-Item -ItemType Directory -Force -Path $resolvedData, $prefixPreview | Out-Null

$frames = Get-ChildItem -LiteralPath $resolvedFrames -Filter 'frame-*.png' | Sort-Object Name
if ($frames.Count -ne 12) {
    throw "Expected exactly 12 PNG frames in $resolvedFrames; found $($frames.Count)."
}

for ($index = 0; $index -lt 12; $index++) {
    $binPath = Join-Path $resolvedData ('{0}{1:00}.bin' -f $Prefix, $index)
    $previewPath = Join-Path $prefixPreview ('{0}{1:00}.png' -f $Prefix, $index)
    [Esp32FrameExporter]::Export(
        $frames[$index].FullName,
        $binPath,
        $previewPath,
        $BackgroundR,
        $BackgroundG,
        $BackgroundB
    )
}

$binFiles = Get-ChildItem -LiteralPath $resolvedData -Filter ($Prefix + '*.bin') | Sort-Object Name
$invalid = @($binFiles | Where-Object Length -ne 45000)
[pscustomobject]@{
    Prefix = $Prefix
    Frames = $binFiles.Count
    InvalidSizes = $invalid.Count
    DataDirectory = $resolvedData
    PreviewDirectory = $prefixPreview
}
