# アイコンの絵 (resources/icons/source/*.png) を 1 枚のスプライト画像にまとめる。
#   出力: resources/icons/icons.png  (1 マス 128 px、横 8 マス。名前の順)
#         resources/icons/icons.json (名前 -> マスの位置。色の付いた絵は "color": true)
# 絵を足したり直したりしたら、これを走らせて、できた 2 つのファイルもコミットする (ビルドは走らせない)。
# exe には resources/app.rc で埋め込む。どこにどの絵を使うかは src/editor/widgets/Icons.cpp の表。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_icon_atlas.ps1
param(
    [string]$Source = (Join-Path $PSScriptRoot '..\resources\icons\source'),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\resources\icons')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$cell = 128      # 1 マスの大きさ (px)
$pad = 4         # マスの縁の透明な幅 (縮めて描くとき隣の絵がにじまない)
$columns = 8

$files = @(Get-ChildItem -LiteralPath $Source -Filter *.png | Sort-Object Name)
if ($files.Count -eq 0) { Write-Error "絵がありません: $Source"; exit 1 }
foreach ($f in $files) {
    if ($f.BaseName -notmatch '^[A-Za-z][A-Za-z0-9]*$') { Write-Error "名前は英字と数字だけにしてください: $($f.Name)"; exit 1 }
}
$rows = [Math]::Ceiling($files.Count / $columns)
$atlas = [System.Drawing.Bitmap]::new($columns * $cell, $rows * $cell, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($atlas)
$g.Clear([System.Drawing.Color]::Transparent)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

$entries = @()
for ($i = 0; $i -lt $files.Count; ++$i) {
    $f = $files[$i]
    $img = [System.Drawing.Image]::FromFile($f.FullName)
    try {
        $x = ($i % $columns) * $cell
        $y = [Math]::Floor($i / $columns) * $cell
        $inner = $cell - 2 * $pad
        $scale = [Math]::Min($inner / $img.Width, $inner / $img.Height)
        $w = $img.Width * $scale
        $h = $img.Height * $scale
        $attr = New-Object System.Drawing.Imaging.ImageAttributes
        $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)  # (縁を外の透明で薄めない)
        $dst = [System.Drawing.Rectangle]::new([int]($x + ($cell - $w) / 2), [int]($y + ($cell - $h) / 2), [int][Math]::Round($w), [int][Math]::Round($h))
        $g.DrawImage($img, $dst, 0, 0, $img.Width, $img.Height, [System.Drawing.GraphicsUnit]::Pixel, $attr)
    } finally {
        $img.Dispose()
    }
    $entries += [ordered]@{ name = $f.BaseName; x = [int]$x; y = [int]$y }
}
$g.Dispose()

# 色の付いた絵 (警告・エラー・ボーンの X Y Z): 色を残して描く。ほかは白 1 色の形で、テーマの色で塗る。
$rect = [System.Drawing.Rectangle]::new(0, 0, $atlas.Width, $atlas.Height)
$data = $atlas.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bytes = New-Object byte[] ($data.Stride * $atlas.Height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$stride = $data.Stride
$atlas.UnlockBits($data)
foreach ($e in $entries) {
    $colored = 0
    for ($yy = $e.y; $yy -lt $e.y + $cell -and $colored -lt 40; ++$yy) {
        $row = $yy * $stride
        for ($xx = $e.x; $xx -lt $e.x + $cell; ++$xx) {
            $o = $row + $xx * 4
            if ($bytes[$o + 3] -lt 128) { continue }
            $b = $bytes[$o]; $gg = $bytes[$o + 1]; $r = $bytes[$o + 2]
            $hi = [Math]::Max($r, [Math]::Max($gg, $b)); $lo = [Math]::Min($r, [Math]::Min($gg, $b))
            if ($hi - $lo -gt 60) { ++$colored; if ($colored -ge 40) { break } }
        }
    }
    $e.color = ($colored -ge 40)
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$png = Join-Path $OutDir 'icons.png'
$atlas.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$atlas.Dispose()

$lines = @()
foreach ($e in $entries) {
    $lines += ('    {{ "name": "{0}", "x": {1}, "y": {2}, "color": {3} }}' -f $e.name, $e.x, $e.y, ($(if ($e.color) { 'true' } else { 'false' })))
}
$json = "{`n  `"kind`": `"rm_engine.icons`",`n  `"format`": 1,`n  `"cell`": $cell,`n  `"icons`": [`n" + ($lines -join ",`n") + "`n  ]`n}`n"
[System.IO.File]::WriteAllText((Join-Path $OutDir 'icons.json'), $json, (New-Object System.Text.UTF8Encoding $false))
Write-Output ("アイコン {0} 個を 1 枚にしました: {1} ({2} x {3})" -f $entries.Count, $png, ($columns * $cell), ($rows * $cell))
