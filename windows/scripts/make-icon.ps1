# 从 macOS 侧的 appicon PNG 生成 windows/assets/app.ico（多尺寸，PNG 压缩条目）
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)   # windows/
$src = Join-Path (Split-Path -Parent $root) "macos\TypeTide\Assets.xcassets\AppIcon.appiconset\appicon_512x512.png"
$outDir = Join-Path $root "assets"
$out = Join-Path $outDir "app.ico"
New-Item -ItemType Directory -Force $outDir | Out-Null

$master = [System.Drawing.Image]::FromFile($src)
$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = @()
foreach ($s in $sizes) {
    # Windows does not mask app icons like macOS. Preserve the existing
    # rounded tile and make its black outer corners transparent. Render the
    # curved edge at 4x so the tray sizes have smooth alpha coverage too.
    $renderSize = $s * 4
    $rounded = New-Object System.Drawing.Bitmap $renderSize, $renderSize
    $canvas = [System.Drawing.Graphics]::FromImage($rounded)
    $canvas.Clear([System.Drawing.Color]::Transparent)
    $canvas.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $texture = New-Object System.Drawing.TextureBrush $master
    $texture.ScaleTransform([single]($renderSize / $master.Width), [single]($renderSize / $master.Height))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $inset = [single]($renderSize * 12 / 512)
    $edge = [single]($renderSize - $inset)
    $diameter = [single]($renderSize * 280 / 512)
    $path.AddArc($inset, $inset, $diameter, $diameter, 180, 90)
    $path.AddArc(($edge - $diameter), $inset, $diameter, $diameter, 270, 90)
    $path.AddArc(($edge - $diameter), ($edge - $diameter), $diameter, $diameter, 0, 90)
    $path.AddArc($inset, ($edge - $diameter), $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    $canvas.FillPath($texture, $path)
    $path.Dispose()
    $texture.Dispose()
    $canvas.Dispose()
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $gfx.DrawImage($rounded, 0, 0, $s, $s)
    $gfx.Dispose()
    $rounded.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngs += , @($s, $ms.ToArray())
    $ms.Dispose()
}
$master.Dispose()

$fs = [System.IO.File]::Create($out)
$bw = New-Object System.IO.BinaryWriter $fs
# ICONDIR
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$pngs.Count)
# ICONDIRENTRY ×N
$offset = 6 + 16 * $pngs.Count
foreach ($entry in $pngs) {
    $s = $entry[0]; $data = $entry[1]
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))  # width (0 = 256)
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))  # height
    $bw.Write([byte]0); $bw.Write([byte]0)                    # colors, reserved
    $bw.Write([uint16]1); $bw.Write([uint16]32)               # planes, bpp
    $bw.Write([uint32]$data.Length)
    $bw.Write([uint32]$offset)
    $offset += $data.Length
}
foreach ($entry in $pngs) { $bw.Write($entry[1]) }
$bw.Close(); $fs.Close()
Write-Output "wrote $out ($([System.IO.FileInfo]::new($out).Length) bytes)"
