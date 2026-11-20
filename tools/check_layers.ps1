# src/ のフォルダ間の include の向きを layers.txt と照らして調べる。
#   * ファイルは、同じ層か下の層のフォルダだけを include できる
#   * ゲームに入る ("yes") フォルダは、入らない ("no") フォルダを include できない
# include は src/ からの形で書く。例: #include "base/Json.h"。フォルダのない include は飛ばす。
#
# プラグイン (plugins/<author>/<name>/、設計 RME_PLATFORM 3-b) について:
#   * src/ (核) はプラグインを include しない
#   * プラグインのコードは runtime/ (ゲームに入る) か editor/ (エディターだけ) に置く
#   * runtime/ が include できるのは、ゲームに入る src のフォルダと、プラグインの runtime/ だけ
#   * プラグインが include できるのは、自分自身と plugin.json の "requires" に書いたプラグインだけ
#   * plugin.json は、それが入っているフォルダの名前を書く。"requires" は輪にならない
#   * エディター部のあるプラグインは、すべて plugins/BuiltinPlugins.cpp に載っている
#   * plugins/RuntimePlugins.cpp (ゲームの exe がリンクする一覧) は、エディターだけのものを include しない
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
$pluginsRoot = Join-Path $Root 'plugins'
$errors = 0

# --- プラグインと plugin.json ---
$authors = @{}
$plugins = @{}  # "author/name" -> 必要な "author/name" の一覧
if (Test-Path $pluginsRoot) {
    foreach ($a in Get-ChildItem $pluginsRoot -Directory) {
        $authors[$a.Name] = $true
        foreach ($n in Get-ChildItem $a.FullName -Directory) {
            $key = "$($a.Name)/$($n.Name)"
            $mf = Join-Path $n.FullName 'plugin.json'
            if (-not (Test-Path $mf)) {
                Write-Host "plugins/${key}: plugin.json is missing"
                $errors++
                continue
            }
            try {
                $j = Get-Content $mf -Raw -Encoding UTF8 | ConvertFrom-Json
            } catch {
                Write-Host "plugins/${key}/plugin.json: not valid JSON"
                $errors++
                continue
            }
            if ($j.author -cne $a.Name -or $j.name -cne $n.Name) {
                Write-Host "plugins/${key}/plugin.json: says $($j.author) / $($j.name) but is in the folder $key"
                $errors++
            }
            $req = @()
            foreach ($r in @($j.requires)) {
                if ($null -ne $r) { $req += "$($r.author)/$($r.name)" }
            }
            $plugins[$key] = $req
        }
    }
}
# 要るものは輪にならない
$state = @{}
function Visit([string]$k, [string[]]$path) {
    if ($state[$k] -eq 'done') { return }
    if ($state[$k] -eq 'visiting') {
        Write-Host "plugin requirements go round in a circle: $(($path + $k) -join ' -> ')"
        $script:errors++
        return
    }
    $state[$k] = 'visiting'
    foreach ($r in $plugins[$k]) {
        if ($plugins.ContainsKey($r)) { Visit $r ($path + $k) }
    }
    $state[$k] = 'done'
}
foreach ($k in @($plugins.Keys)) { Visit $k @() }
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
        if ($authors.ContainsKey($target)) {
            Write-Host "${rel}:${n}: the core (src/) must not include a plugin: $inc"
            $script:errors++
            continue
        }
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

# --- プラグインのコード ---
if (Test-Path $pluginsRoot) {
    $listFile = Join-Path $pluginsRoot 'BuiltinPlugins.cpp'
    $listText = if (Test-Path $listFile) { Get-Content $listFile -Raw -Encoding UTF8 } else { '' }
    foreach ($k in $plugins.Keys) {
        # 載っている = エディター部か id がそこに include されている (space3d のエディター部は、ほかの
        # プラグインのエディター (ギズモ) が使うライブラリなので、登録するものがない)。
        $listed = $listText.Contains("`"$k/editor/") -or $listText.Contains("`"$k/runtime/Id.h`"")
        if ((Test-Path (Join-Path $pluginsRoot "$k/editor")) -and -not $listed) {
            Write-Host "plugins/${k}: has an editor part but is not listed in plugins/BuiltinPlugins.cpp"
            $errors++
        }
    }
    Get-ChildItem $pluginsRoot -Recurse -Include *.h, *.cpp | ForEach-Object {
        $rel = $_.FullName.Substring($pluginsRoot.Length + 1) -replace '\\', '/'
        if ($rel -match '(^|/)templates/') { return }  # テンプレートのスクリプトはゲームのもの (それ自身の DLL に入る)
        if ($rel -eq 'BuiltinPlugins.cpp') { return }  # すべてのプラグインの一覧
        if ($rel -eq 'RuntimePlugins.cpp') {
            # ランタイム部の一覧: ゲームに入るので、ゲームに入るものだけ。
            $n = 0
            foreach ($text in Get-Content $_.FullName -Encoding UTF8) {
                $n++
                if ($text -notmatch '^\s*#\s*include\s+"([^"]+)"') { continue }
                $inc = $Matches[1]
                $segs = $inc.Split('/')
                if (($layers.ContainsKey($segs[0]) -and -not $ships[$segs[0]]) -or ($authors.ContainsKey($segs[0]) -and ($segs.Count -lt 4 -or $segs[2] -ne 'runtime'))) {
                    Write-Host "plugins/${rel}:${n}: the runtime list ships in games and must include only runtime parts: $inc"
                    $script:errors++
                }
            }
            return
        }
        $parts = $rel.Split('/')
        if ($parts.Count -lt 4 -or ($parts[2] -ne 'runtime' -and $parts[2] -ne 'editor')) {
            Write-Host "plugins/${rel}: plugin code goes in <author>/<name>/runtime/ or <author>/<name>/editor/"
            $script:errors++
            return
        }
        $self = "$($parts[0])/$($parts[1])"
        $part = $parts[2]
        $n = 0
        foreach ($text in Get-Content $_.FullName -Encoding UTF8) {
            $n++
            if ($text -notmatch '^\s*#\s*include\s+"([^"]+)"') { continue }
            $inc = $Matches[1]
            if ($inc -notmatch '/') { continue }
            $segs = $inc.Split('/')
            if ($layers.ContainsKey($segs[0])) {
                if ($part -eq 'runtime' -and -not $ships[$segs[0]]) {
                    Write-Host "plugins/${rel}:${n}: runtime/ ships in games and must not include editor-only $($segs[0]) : $inc"
                    $script:errors++
                }
                continue
            }
            if (-not $authors.ContainsKey($segs[0])) { continue }
            if ($segs.Count -lt 4) {
                Write-Host "plugins/${rel}:${n}: include a plugin as <author>/<name>/runtime|editor/...: $inc"
                $script:errors++
                continue
            }
            $other = "$($segs[0])/$($segs[1])"
            if ($other -ne $self -and -not ($plugins[$self] -contains $other)) {
                Write-Host "plugins/${rel}:${n}: $other is not in the requires of $self (plugin.json): $inc"
                $script:errors++
            }
            if ($part -eq 'runtime' -and $segs[2] -ne 'runtime') {
                Write-Host "plugins/${rel}:${n}: runtime/ must not include an editor/ part: $inc"
                $script:errors++
            }
        }
    }
}

if ($errors -gt 0) {
    Write-Host "LAYER CHECK FAILED ($errors)"
    exit 1
}
Write-Host 'LAYER CHECK OK'
exit 0
