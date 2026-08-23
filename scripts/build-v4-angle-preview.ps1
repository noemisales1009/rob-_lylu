param(
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $PSScriptRoot '..\output'
}

Add-Type -AssemblyName System.Drawing

$actions = @(
    [pscustomobject]@{ Folder = 'dancando'; Label = 'DANCANDO' },
    [pscustomobject]@{ Folder = 'tomando-agua'; Label = 'TOMANDO AGUA' },
    [pscustomobject]@{ Folder = 'dormindo'; Label = 'DORMINDO' },
    [pscustomobject]@{ Folder = 'brava'; Label = 'BRAVA' },
    [pscustomobject]@{ Folder = 'chorando'; Label = 'CHORANDO' },
    [pscustomobject]@{ Folder = 'bocejando'; Label = 'BOCEJANDO' },
    [pscustomobject]@{ Folder = 'lendo'; Label = 'LENDO' },
    [pscustomobject]@{ Folder = 'tomando-cafe'; Label = 'TOMANDO CAFE' }
)
$directions = @(
    [pscustomobject]@{ Folder = 'frente'; Label = 'FRENTE' },
    [pscustomobject]@{ Folder = 'costas'; Label = 'COSTAS' },
    [pscustomobject]@{ Folder = 'direita'; Label = 'DIREITA' },
    [pscustomobject]@{ Folder = 'esquerda'; Label = 'ESQUERDA' }
)

$cellWidth = 384
$cellHeight = 420
$labelWidth = 220
$headerHeight = 70
$canvas = New-Object System.Drawing.Bitmap ($labelWidth + $directions.Count * $cellWidth), ($headerHeight + $actions.Count * $cellHeight), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($canvas)
try {
    $graphics.Clear([System.Drawing.Color]::FromArgb(255, 31, 31, 34))
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

    $headerFont = New-Object System.Drawing.Font 'Arial', 18, ([System.Drawing.FontStyle]::Bold)
    $rowFont = New-Object System.Drawing.Font 'Arial', 16, ([System.Drawing.FontStyle]::Bold)
    $textBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $linePen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 72, 72, 78)), 2
    try {
        for ($column = 0; $column -lt $directions.Count; $column++) {
            $x = $labelWidth + $column * $cellWidth
            $graphics.DrawString($directions[$column].Label, $headerFont, $textBrush, $x + 18, 20)
        }

        for ($row = 0; $row -lt $actions.Count; $row++) {
            $y = $headerHeight + $row * $cellHeight
            $graphics.DrawString($actions[$row].Label, $rowFont, $textBrush, 18, $y + 190)
            $graphics.DrawLine($linePen, 0, $y, $canvas.Width, $y)

            for ($column = 0; $column -lt $directions.Count; $column++) {
                $x = $labelWidth + $column * $cellWidth
                $framePath = Join-Path $OutputRoot "animacoes-direcionais-v4\$($actions[$row].Folder)\$($directions[$column].Folder)\frame-07.png"
                $frame = [System.Drawing.Image]::FromFile($framePath)
                try {
                    $graphics.DrawImage($frame, $x, $y + 18, 384, 384)
                }
                finally {
                    $frame.Dispose()
                }
                $graphics.DrawLine($linePen, $x, $headerHeight, $x, $canvas.Height)
            }
        }
    }
    finally {
        $headerFont.Dispose()
        $rowFont.Dispose()
        $textBrush.Dispose()
        $linePen.Dispose()
    }

    $previewDirectory = Join-Path $OutputRoot 'previews'
    New-Item -ItemType Directory -Force -Path $previewDirectory | Out-Null
    $previewPath = Join-Path $previewDirectory 'lylu-acoes-antigas-4-angulos.png'
    $canvas.Save($previewPath, [System.Drawing.Imaging.ImageFormat]::Png)
    Get-Item -LiteralPath $previewPath | Select-Object FullName, Length
}
finally {
    $graphics.Dispose()
    $canvas.Dispose()
}
