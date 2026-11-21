# <dir>/RM_Engine.files.json を書く: エンジンが exe の隣に必要とするすべてのファイルを、大きさと
# SHA-256 つきで (Hub > オプション > エディターの整合性 が、ファイルをこの一覧と比べる)。
#   RM_Engine.exe, Lang/**, Help/**, Plugins/**
# ビルドが走らせる (src/RM_Engine.vcxproj、プラグインと言語のファイルをコピーした後)。
param([Parameter(Mandatory = $true)][string]$Dir)

$ErrorActionPreference = 'Stop'
$Dir = (Resolve-Path $Dir).Path.TrimEnd('\')
$files = @(Join-Path $Dir 'RM_Engine.exe')
foreach ($sub in 'Lang', 'Help', 'Plugins') {
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
