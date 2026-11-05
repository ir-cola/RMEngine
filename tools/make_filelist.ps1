# Writes <dir>/RM_Engine.files.json: every file the engine needs next to its exe, with its size and
# SHA-256 (Hub > オプション > エディターの整合性 compares the files with this list).
#   RM_Engine.exe, Lang/**, Plugins/**
# Run by the build (src/RM_Engine.vcxproj, after the plugin and language files are copied).
param([Parameter(Mandatory = $true)][string]$Dir)

$ErrorActionPreference = 'Stop'
$Dir = (Resolve-Path $Dir).Path.TrimEnd('\')
$files = @(Join-Path $Dir 'RM_Engine.exe')
foreach ($sub in 'Lang', 'Plugins') {
    $p = Join-Path $Dir $sub
    if (Test-Path $p) { $files += (Get-ChildItem $p -Recurse -File | Sort-Object FullName | ForEach-Object FullName) }
}
$entries = foreach ($f in $files) {
    if (-not (Test-Path $f)) { continue }
    $rel = $f.Substring($Dir.Length + 1) -replace '\\', '/'
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash.ToLowerInvariant()
    [ordered]@{ path = $rel; size = (Get-Item -LiteralPath $f).Length; sha256 = $hash }
}
$doc = [ordered]@{ kind = 'rm_engine.files'; format = 1; files = @($entries) }
$json = $doc | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $Dir 'RM_Engine.files.json'), $json, (New-Object System.Text.UTF8Encoding $false))
exit 0
