# Checks the include direction between folders of src/ against layers.txt.
#   * a file may include only folders of the same or a lower layer
#   * a folder that ships in games ("yes") may not include one that does not ("no")
# Includes are written from src/, e.g. #include "base/Json.h". Includes without a folder are skipped.
param([string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path)

$layers = @{}
$ships = @{}
foreach ($line in Get-Content (Join-Path $Root 'layers.txt') -Encoding UTF8) {
    $l = ($line -replace '#.*$', '').Trim()
    if ($l -eq '') { continue }
    $parts = $l -split '\s+'
    $layers[$parts[0]] = [int]$parts[1]
    $ships[$parts[0]] = ($parts[2] -eq 'yes')
}

$src = Join-Path $Root 'src'
$errors = 0
Get-ChildItem $src -Recurse -Include *.h, *.cpp | ForEach-Object {
    $rel = $_.FullName.Substring($src.Length + 1) -replace '\\', '/'
    $folder = $rel.Split('/')[0]
    if (-not $layers.ContainsKey($folder)) {
        Write-Host "unregistered folder (add it to layers.txt): $rel"
        $script:errors++
        return
    }
    $n = 0
    foreach ($text in Get-Content $_.FullName -Encoding UTF8) {
        $n++
        if ($text -notmatch '^\s*#\s*include\s+"([^"]+)"') { continue }
        $inc = $Matches[1]
        if ($inc -notmatch '/') { continue }
        $target = $inc.Split('/')[0]
        if (-not $layers.ContainsKey($target)) { continue }
        if ($layers[$target] -gt $layers[$folder]) {
            Write-Host "${rel}:${n}: $folder (L$($layers[$folder])) must not include $target (L$($layers[$target])): $inc"
            $script:errors++
        }
        if ($ships[$folder] -and -not $ships[$target]) {
            Write-Host "${rel}:${n}: $folder ships in games and must not include editor-only $target : $inc"
            $script:errors++
        }
    }
}

if ($errors -gt 0) {
    Write-Host "LAYER CHECK FAILED ($errors)"
    exit 1
}
Write-Host 'LAYER CHECK OK'
exit 0
