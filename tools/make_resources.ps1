# Generates resources/app.ico and resources/logo_hub.png from the artwork in the repository root.
#   RM_Engine_icon.png -> resources/app.ico      (16..256 px, PNG frames)
#   RM_Engine_logo.png -> resources/logo_hub.png (cropped to the opaque area, 960 px wide)
# Run with Windows PowerShell:  powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_resources.ps1
param([string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class RmImg {
    public static Rectangle OpaqueBounds(Bitmap bmp, int alphaThreshold) {
        var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
        var data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int stride = data.Stride;
        byte[] buf = new byte[stride * bmp.Height];
        Marshal.Copy(data.Scan0, buf, 0, buf.Length);
        bmp.UnlockBits(data);
        int minX = bmp.Width, minY = bmp.Height, maxX = -1, maxY = -1;
        for (int y = 0; y < bmp.Height; y++)
            for (int x = 0; x < bmp.Width; x++)
                if (buf[y * stride + x * 4 + 3] > alphaThreshold) {
                    if (x < minX) minX = x; if (x > maxX) maxX = x;
                    if (y < minY) minY = y; if (y > maxY) maxY = y;
                }
        if (maxX < 0) return rect;
        return Rectangle.FromLTRB(minX, minY, maxX + 1, maxY + 1);
    }
}
'@

function Resize-Image([System.Drawing.Image]$img, [System.Drawing.Rectangle]$srcRect, [int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $attr = New-Object System.Drawing.Imaging.ImageAttributes
    $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
    $dst = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $g.DrawImage($img, $dst, $srcRect.X, $srcRect.Y, $srcRect.Width, $srcRect.Height, [System.Drawing.GraphicsUnit]::Pixel, $attr)
    $g.Dispose()
    return $bmp
}

$outDir = Join-Path $Root 'resources'
New-Item -ItemType Directory -Force $outDir | Out-Null

# --- Hub logo -------------------------------------------------------------
$logo = New-Object System.Drawing.Bitmap (Join-Path $Root 'RM_Engine_logo.png')
$b = [RmImg]::OpaqueBounds($logo, 8)
$pad = 8
$crop = [System.Drawing.Rectangle]::FromLTRB([Math]::Max(0, $b.Left - $pad), [Math]::Max(0, $b.Top - $pad),
    [Math]::Min($logo.Width, $b.Right + $pad), [Math]::Min($logo.Height, $b.Bottom + $pad))
$lw = 960
$lh = [int][Math]::Round($lw * $crop.Height / $crop.Width)
$logoOut = Resize-Image $logo $crop $lw $lh
$logoOut.Save((Join-Path $outDir 'logo_hub.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$logoOut.Dispose(); $logo.Dispose()
Write-Host "logo_hub.png  crop=$crop  size=${lw}x${lh}"

# --- App icon (PNG frames inside an .ico) ---------------------------------
$icon = New-Object System.Drawing.Bitmap (Join-Path $Root 'RM_Engine_icon.png')
$full = New-Object System.Drawing.Rectangle 0, 0, $icon.Width, $icon.Height
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$frames = @()
foreach ($s in $sizes) {
    $bmp = Resize-Image $icon $full $s $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += , @($s, $ms.ToArray())
    $bmp.Dispose(); $ms.Dispose()
}
$icon.Dispose()

$fs = [System.IO.File]::Create((Join-Path $outDir 'app.ico'))
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $s = $f[0]; $data = $f[1]
    $dim = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$data.Length); $bw.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($f in $frames) { $bw.Write([byte[]]$f[1]) }
$bw.Close()
Write-Host "app.ico  frames=$($sizes -join ',')"
