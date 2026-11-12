# Checks the include direction between folders of src/ against layers.txt.
#   * a file may include only folders of the same or a lower layer
#   * a folder that ships in games ("yes") may not include one that does not ("no")
# Includes are written from src/, e.g. #include "base/Json.h". Includes without a folder are skipped.
#
# And the plugins (plugins/<author>/<name>/, design RME_PLATFORM 3-b):
#   * src/ (the core) never includes a plugin
#   * plugin code is in runtime/ (ships in games) or editor/ (editor only)
#   * runtime/ includes only src folders that ship in games, and only runtime/ of plugins
#   * a plugin includes only itself and the plugins listed in "requires" of its plugin.json
#   * plugin.json names the folder it is in; "requires" never goes round in a circle
#   * every plugin with an editor part is listed in plugins/BuiltinPlugins.cpp
#   * plugins/RuntimePlugins.cpp (the list a game exe links) includes nothing editor-only
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

# --- plugins and their plugin.json ---
$authors = @{}
$plugins = @{}  # "author/name" -> list of required "author/name"
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
# requirements must not go round in a circle
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

# --- plugin code ---
if (Test-Path $pluginsRoot) {
    $listFile = Join-Path $pluginsRoot 'BuiltinPlugins.cpp'
    $listText = if (Test-Path $listFile) { Get-Content $listFile -Raw -Encoding UTF8 } else { '' }
    foreach ($k in $plugins.Keys) {
        # Listed = its editor part or its id is included there (space3d's editor part is only a library
        # for other plugins' editors - the gizmo - so it has nothing to register).
        $listed = $listText.Contains("`"$k/editor/") -or $listText.Contains("`"$k/runtime/Id.h`"")
        if ((Test-Path (Join-Path $pluginsRoot "$k/editor")) -and -not $listed) {
            Write-Host "plugins/${k}: has an editor part but is not listed in plugins/BuiltinPlugins.cpp"
            $errors++
        }
    }
    Get-ChildItem $pluginsRoot -Recurse -Include *.h, *.cpp | ForEach-Object {
        $rel = $_.FullName.Substring($pluginsRoot.Length + 1) -replace '\\', '/'
        if ($rel -match '(^|/)templates/') { return }  # a template's scripts are a game's (built into its own DLL)
        if ($rel -eq 'BuiltinPlugins.cpp') { return }  # the list of every plugin
        if ($rel -eq 'RuntimePlugins.cpp') {
            # The list of runtime parts: goes into games, so only what ships in games.
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
