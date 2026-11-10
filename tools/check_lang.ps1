# Checks that every string shown on screen has English (editor/lang/Lang.h).
#
#   Tr(L"..."), TrF(L"...", ...), TrU8("..."), TrU8F("...", ...)   in src/ and plugins/
#   Label="..." / Tooltip="..." of RM_STRUCT / RM_PROPERTY / RM_ENUM / RM_ITEM in headers
#   "displayName" of plugin.json, "name" of a theme pack's theme files (plugins/<a>/<n>/themes/*.json)
#
# A string from src/ needs an entry in resources/lang/en.json; a string from
# plugins/<author>/<name>/ in that plugin's lang/en.json. The English must take the same {}
# arguments as the Japanese. Exit code 0 = all there, 1 = something missing (listed).
param([string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path)

$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding $false

function Read-Catalog([string]$file) {
    $map = @{}
    if (Test-Path $file) {
        $obj = [IO.File]::ReadAllText($file, $utf8) | ConvertFrom-Json
        foreach ($p in $obj.PSObject.Properties) { $map[$p.Name] = [string]$p.Value }
    }
    return $map
}

# C++ string literal body -> the runtime text (only the escapes the code uses).
function Unescape([string]$s) {
    return $s.Replace('\"', '"').Replace('\n', "`n").Replace('\t', "`t").Replace('\\', '\')
}

function Count-Args([string]$s) { return ([regex]::Matches(($s -replace '\{\{|\}\}', ''), '\{[^{}]*\}')).Count }

$catalogs = @{}
function Catalog-For([string]$rel) {
    if ($rel -match '^plugins/([^/]+)/([^/]+)/') { $file = Join-Path $Root "plugins/$($Matches[1])/$($Matches[2])/lang/en.json" }
    else { $file = Join-Path $Root 'resources/lang/en.json' }
    if (-not $catalogs.ContainsKey($file)) { $catalogs[$file] = Read-Catalog $file }
    return @($file, $catalogs[$file])
}

$missing = New-Object System.Collections.Generic.List[string]
$used = @{}
function Need([string]$rel, [int]$line, [string]$text) {
    if ($text -notmatch '[^\x00-\x7F]') { return }  # nothing Japanese in it
    $c = Catalog-For $rel
    $key = "$($c[0])|$text"
    $used[$key] = $true
    if (-not $c[1].ContainsKey($text)) {
        $missing.Add("${rel}:${line}: no English for: $text")
    } elseif ((Count-Args $text) -ne (Count-Args $c[1][$text])) {
        $missing.Add("${rel}:${line}: the English takes other {} arguments: $text")
    }
}

$lit = '"((?:[^"\\]|\\.)*)"'
$files = Get-ChildItem (Join-Path $Root 'src'), (Join-Path $Root 'plugins') -Recurse -Include *.h, *.cpp
foreach ($f in $files) {
    $rel = $f.FullName.Substring($Root.Length + 1) -replace '\\', '/'
    $lines = [IO.File]::ReadAllLines($f.FullName, $utf8)
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $l = $lines[$i]
        if ($l.TrimStart().StartsWith('//')) { continue }  # examples in comments
        foreach ($m in [regex]::Matches($l, '\bTr(?:U8|Key)?F?\(\s*L?' + $lit)) { Need $rel ($i + 1) (Unescape $m.Groups[1].Value) }
        if ($l -match 'RM_(STRUCT|PROPERTY|ENUM|ITEM|COMPONENT)\(') {
            foreach ($m in [regex]::Matches($l, '\b(?:Label|Tooltip)=' + $lit)) { Need $rel ($i + 1) (Unescape $m.Groups[1].Value) }
        }
    }
}
foreach ($pj in Get-ChildItem (Join-Path $Root 'plugins') -Recurse -Filter plugin.json) {
    $rel = $pj.FullName.Substring($Root.Length + 1) -replace '\\', '/'
    $d = ([IO.File]::ReadAllText($pj.FullName, $utf8) | ConvertFrom-Json).displayName
    if ($d) { Need $rel 1 $d }
}
foreach ($tf in Get-ChildItem (Join-Path $Root 'plugins') -Recurse -Filter *.json | Where-Object { $_.Directory.Name -eq 'themes' }) {
    $rel = $tf.FullName.Substring($Root.Length + 1) -replace '\\', '/'
    $n = ([IO.File]::ReadAllText($tf.FullName, $utf8) | ConvertFrom-Json).name
    if ($n) { Need $rel 1 $n }
}

foreach ($m in $missing) { Write-Host $m }
# Entries nobody uses any more are only reported (not an error).
foreach ($file in $catalogs.Keys) {
    foreach ($k in $catalogs[$file].Keys) {
        if (-not $used.ContainsKey("$file|$k")) { Write-Host "(unused) $($file.Substring($Root.Length + 1)): $k" }
    }
}
if ($missing.Count -gt 0) {
    Write-Host "check_lang: $($missing.Count) string(s) without English"
    exit 1
}
Write-Host 'check_lang: every string on screen has English'
exit 0
