# 画面に出す文字がすべて英語を持つか調べる (editor/lang/Lang.h)。
#
#   Tr(L"..."), TrF(L"...", ...), TrU8("..."), TrU8F("...", ...)   (src/ と plugins/ の中)
#   ヘッダーの RM_STRUCT / RM_PROPERTY / RM_ENUM / RM_ITEM の Label="..." / Tooltip="..."
#   plugin.json の "displayName"、テーマパックのテーマファイルの "name" (plugins/<a>/<n>/themes/*.json)、
#   テンプレートの "name" / "description" (plugins/<a>/<n>/templates/<id>/template.json)
#
# src/ の文字は resources/lang/en.json に、plugins/<author>/<name>/ の文字はそのプラグインの
# lang/en.json に項目が要る。英語は日本語と同じ {} の引数を取らなければならない。
# 終了コード 0 = すべてある、1 = 足りないものがある (一覧に出す)。
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

# C++ の文字列リテラルの中身 -> 実行時の文字 (コードが使うエスケープだけ)。
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
    if ($text -notmatch '[^\x00-\x7F]') { return }  # 日本語が入っていない
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
# (テンプレートのスクリプトではない: それはゲームのもので、ゲーム自身の言葉で書く)
$files = Get-ChildItem (Join-Path $Root 'src'), (Join-Path $Root 'plugins') -Recurse -Include *.h, *.cpp | Where-Object { $_.FullName -notmatch '[\\/]templates[\\/]' }
foreach ($f in $files) {
    $rel = $f.FullName.Substring($Root.Length + 1) -replace '\\', '/'
    $lines = [IO.File]::ReadAllLines($f.FullName, $utf8)
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $l = $lines[$i]
        if ($l.TrimStart().StartsWith('//')) { continue }  # コメントの中の例
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
foreach ($tj in Get-ChildItem (Join-Path $Root 'plugins') -Recurse -Filter template.json | Where-Object { $_.Directory.Parent.Name -eq 'templates' }) {
    $rel = $tj.FullName.Substring($Root.Length + 1) -replace '\\', '/'
    $t = [IO.File]::ReadAllText($tj.FullName, $utf8) | ConvertFrom-Json
    if ($t.name) { Need $rel 1 $t.name }
    if ($t.description) { Need $rel 1 $t.description }
}
foreach ($tf in Get-ChildItem (Join-Path $Root 'plugins') -Recurse -Filter *.json | Where-Object { $_.Directory.Name -eq 'themes' }) {
    $rel = $tf.FullName.Substring($Root.Length + 1) -replace '\\', '/'
    $n = ([IO.File]::ReadAllText($tf.FullName, $utf8) | ConvertFrom-Json).name
    if ($n) { Need $rel 1 $n }
}

# 英語の表は実行時に 1 つにまとめる (核のと、すべてのプラグインの): 同じ日本語に 2 つの違う
# 英語があると、どこかで違うほうが出る (ウィンドウ メニューが表示モードの言葉の "Windowed" に
# なった)。意味が違うなら日本語も変える。
$firstOf = @{}
$tableFiles = @((Join-Path $Root 'resources/lang/en.json')) + @(Get-ChildItem (Join-Path $Root 'plugins') -Recurse -Filter en.json | Where-Object { $_.Directory.Name -eq 'lang' } | ForEach-Object { $_.FullName })
foreach ($tf in $tableFiles) {
    $table = Read-Catalog $tf
    foreach ($k in $table.Keys) {
        if (-not $firstOf.ContainsKey($k)) { $firstOf[$k] = @($tf, $table[$k]); continue }
        if ($firstOf[$k][1] -cne $table[$k]) {
            $missing.Add("${tf}: 「$k」の英語が $($firstOf[$k][0]) と違います (`"$($table[$k])`" / `"$($firstOf[$k][1])`")。意味が違うなら日本語を変えてください")
        }
    }
}

foreach ($m in $missing) { Write-Host $m }
# もう誰も使わない項目は知らせるだけ (エラーにはしない)。
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
