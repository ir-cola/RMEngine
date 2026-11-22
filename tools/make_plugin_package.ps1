# プラグインのパッケージ (zip) と一覧 index.json を作る (docs/PLUGIN_DISTRIBUTION.md 段階 1)。
# 出来たフォルダーを、ポートフォリオ (https://ir-cola.github.io/) のリポジトリの rm_engine/ に入れて
# プッシュすると、エンジンの Hub が https://ir-cola.github.io/rm_engine/index.json から読む。
# GitHub Actions は使わない (手元で作ってプッシュするだけ)。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_plugin_package.ps1 [-Out <フォルダー>] [-Plugins rm_engine/theme_paper,...]
#
#   -Out         出す先 (既定: build\site\rm_engine)。中に index.json と plugins\<作者>.<名前>-<版>.zip
#   -Plugins     作る物 (既定: plugin.json が "code": false の物すべて)。コードのある物はまだ配れない
#   -EngineMin   その版が動く一番古いエンジン (既定: 今のエンジンの版。ダウンロードはこの版から)
#   -SetVersion  zip の中の plugin.json の版をこれにする (試すとき: エンジンの写しより新しい版を作る)
#
# zip に入れるのは plugin.json・lang・themes・templates・help だけ (runtime\ の C++ は入れない)。区切りは /。
# 前に作った index.json があれば、同じプラグインの別の版はそのまま残す (同じ版は作り直す)。
param(
    [string]$Out = '',
    [string[]]$Plugins = @(),
    [string]$EngineMin = '',
    [string]$SetVersion = ''
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$utf8 = New-Object System.Text.UTF8Encoding $false
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not $Out) { $Out = Join-Path $root 'build\site\rm_engine' }
$versionText = [IO.File]::ReadAllText((Join-Path $root 'src\base\Version.h'), $utf8)
if ($versionText -notmatch '#define RM_ENGINE_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"') { throw 'src\base\Version.h に RM_ENGINE_VERSION がありません' }
$engineVersion = $Matches[1]
if (-not $EngineMin) { $EngineMin = $engineVersion }
foreach ($v in @($EngineMin, $SetVersion)) {
    if ($v -and $v -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw "版は数 3 つで書いてください: $v" }
}

# 作る物
$all = Get-ChildItem (Join-Path $root 'plugins') -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory } |
    Where-Object { Test-Path (Join-Path $_.FullName 'plugin.json') }
$targets = @()
foreach ($dir in $all) {
    $manifest = [IO.File]::ReadAllText((Join-Path $dir.FullName 'plugin.json'), $utf8) | ConvertFrom-Json
    $id = "$($manifest.author)/$($manifest.name)"
    if ($Plugins.Count -gt 0 -and -not ($Plugins -contains $id)) { continue }
    if ($manifest.code -ne $false) {
        if ($Plugins -contains $id) { throw "$id はコードのあるプラグインなので、まだ配れません (plugin.json の code が false の物だけ)" }
        continue
    }
    $targets += [pscustomobject]@{ Dir = $dir; Manifest = $manifest; Id = $id }
}
if ($targets.Count -eq 0) { throw '作るプラグインがありません' }

$pluginsOut = Join-Path $Out 'plugins'
New-Item -ItemType Directory -Force $pluginsOut | Out-Null
$indexFile = Join-Path $Out 'index.json'
$old = $null
if (Test-Path $indexFile) { $old = [IO.File]::ReadAllText($indexFile, $utf8) | ConvertFrom-Json }

$entries = @()
foreach ($t in $targets) {
    $m = $t.Manifest
    $version = if ($SetVersion) { $SetVersion } else { [string]$m.version }
    if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw "$($t.Id) の plugin.json に版 (version: 0.0.1 の形) がありません" }
    $zipName = "$($m.author).$($m.name)-$version.zip"
    $zipPath = Join-Path $pluginsOut $zipName
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    $zip = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        # plugin.json (版を変えるときは書き直した物)
        $manifestText = [IO.File]::ReadAllText((Join-Path $t.Dir.FullName 'plugin.json'), $utf8)
        if ($SetVersion) { $manifestText = [regex]::Replace($manifestText, '"version"\s*:\s*"[^"]*"', "`"version`": `"$SetVersion`"") }
        $entry = $zip.CreateEntry('plugin.json', 'Optimal')
        $w = New-Object IO.StreamWriter($entry.Open(), $utf8)
        $w.Write($manifestText)
        $w.Dispose()
        foreach ($sub in @('lang', 'themes', 'templates', 'help')) {
            $subDir = Join-Path $t.Dir.FullName $sub
            if (-not (Test-Path $subDir)) { continue }
            foreach ($f in Get-ChildItem $subDir -Recurse -File) {
                $rel = $f.FullName.Substring($t.Dir.FullName.Length + 1) -replace '\\', '/'
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $rel, 'Optimal') | Out-Null
            }
        }
    } finally {
        $zip.Dispose()
    }
    $size = (Get-Item $zipPath).Length
    $sha = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $requires = @($m.requires | ForEach-Object { [ordered]@{ author = $_.author; name = $_.name } })
    $ver = [ordered]@{ version = $version; engine = [ordered]@{ min = $EngineMin }; code = $false; url = "plugins/$zipName"; size = $size; sha256 = $sha; requires = $requires }
    # 前の一覧の、同じプラグインの別の版は残す
    $versions = @($ver)
    if ($old) {
        $prev = $old.plugins | Where-Object { $_.author -eq $m.author -and $_.name -eq $m.name } | Select-Object -First 1
        if ($prev) { $versions += @($prev.versions | Where-Object { $_.version -ne $version }) }
    }
    $entries += [ordered]@{ author = $m.author; name = $m.name; displayName = $m.displayName; versions = $versions }
    Write-Host "作りました: $zipName ($size バイト, sha256 $sha)"
}
# 前の一覧にあって、今回作らなかったプラグインも残す
if ($old) {
    foreach ($p in $old.plugins) {
        if (-not ($entries | Where-Object { $_.author -eq $p.author -and $_.name -eq $p.name })) { $entries += $p }
    }
}
$index = [ordered]@{
    kind = 'rm_engine.plugin_index'
    formatVersion = 1
    updated = (Get-Date -Format 'yyyy-MM-dd')
    plugins = @($entries)
    engine = [ordered]@{ version = $engineVersion; url = ''; size = 0; sha256 = '' }
}
[IO.File]::WriteAllText($indexFile, ($index | ConvertTo-Json -Depth 10), $utf8)
Write-Host "一覧: $indexFile ($($entries.Count) 個のプラグイン、エンジン $engineVersion)"
exit 0
