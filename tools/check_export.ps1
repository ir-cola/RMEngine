# The export check (design NewEngineBluePrint 11-b-2, rule 5): makes a project from each template,
# exports it (RM_Engine.exe --export-template: no window) and starts the game with --smoke (a few frames,
# exit code 0). Also checks the exported folder: the exe, Game.rmpak, no .pdb, and no path of this PC in
# the exe and the scripts' DLL. Exit code 0 = every game was exported and ran.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\check_export.ps1 [-Config Release]
# Needs Visual Studio (the export links with it) and a built engine (tools\build.ps1).
# Another PC without the engine (Windows Sandbox): docs/HOW_TO_TRY.md, "書き出したゲームを別の PC で".
param([ValidateSet('Debug', 'Release')][string]$Config = 'Release')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$engine = Join-Path $root "build\bin\x64\$Config\RM_Engine.exe"
if (-not (Test-Path $engine)) { Write-Host "check_export: エンジンがありません (先に tools\build.ps1): $engine"; exit 2 }
$work = Join-Path $env:TEMP 'rm_export_check'
$log = Join-Path $env:LOCALAPPDATA 'RM_Engine\Logs\editor.log'

# author plugin id: the templates checked
$templates = @(
    @('rm_engine', 'game_production', 'game'),
    @('rm_engine', 'game_x_2d', 'game_2d'),
    @('rm_engine', 'game_x_3d', 'game_3d'),
    @('rm_engine', 'physics', 'physics_3d')
)
$failed = 0
# A path of this PC (the repository's, the export's folder, the user's) in a file written, as UTF-8 or
# UTF-16: the path, else $null.
function Find-PcPath([string]$file, [string]$dir) {
    $bytes = [IO.File]::ReadAllBytes($file)
    $text8 = [Text.Encoding]::UTF8.GetString($bytes)
    $text16 = [Text.Encoding]::Unicode.GetString($bytes)
    foreach ($path in @($root, $dir, $env:USERPROFILE)) {
        if ($text8.IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or $text16.IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0) { return $path }
    }
    return $null
}

foreach ($t in $templates) {
    $dir = Join-Path $work $t[2]
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    $p = Start-Process -FilePath $engine -ArgumentList @('--export-template', $t[0], $t[1], $t[2], "`"$dir`"") -Wait -PassThru
    if ($p.ExitCode -ne 0) {
        Write-Host "NG  $($t[2]): 書き出せませんでした (終了コード $($p.ExitCode))。$log を見てください"
        $failed++
        continue
    }
    $game = Join-Path $dir 'Game'
    $exe = @(Get-ChildItem $game -Filter *.exe)
    if ($exe.Count -ne 1) { Write-Host "NG  $($t[2]): exe が 1 つではありません"; $failed++; continue }
    if (-not (Test-Path (Join-Path $game 'Game.rmpak'))) { Write-Host "NG  $($t[2]): Game.rmpak がありません"; $failed++; continue }
    if (@(Get-ChildItem $game -Filter *.pdb).Count -gt 0) { Write-Host "NG  $($t[2]): .pdb が入っています"; $failed++; continue }
    # No path of this PC in the exe (the project's, the engine's), as UTF-8 or UTF-16.
    $leak = Find-PcPath $exe[0].FullName $dir
    if ($leak) { Write-Host "NG  $($t[2]): exe にこの PC のパスが残っています: $leak"; $failed++; continue }
    $r = Start-Process -FilePath $exe[0].FullName -ArgumentList '--smoke' -Wait -PassThru
    if ($r.ExitCode -ne 0) {
        Write-Host "NG  $($t[2]): ゲームが --smoke で失敗しました (終了コード $($r.ExitCode))"
        Get-Content (Join-Path $game 'game.log') -Encoding UTF8 -ErrorAction SilentlyContinue | Select-Object -Last 10 | ForEach-Object { Write-Host "    $_" }
        $failed++
        continue
    }
    $size = [Math]::Round($exe[0].Length / 1KB)
    $pak = [Math]::Round((Get-Item (Join-Path $game 'Game.rmpak')).Length / 1KB, 1)
    Write-Host "OK  $($t[2]): $($exe[0].Name) ($size KB)・Game.rmpak ($pak KB) を書き出し、--smoke で起動して閉じました"
}
# A game with a script: the template 「ゲーム」 (its sample script ArrowMover), an entity with it put into
# the scene, the scripts built and the project exported, then the game with --smoke: its game.log must
# show the script's BeginPlay and its EndPlay with the frames it ticked (Scripts.dll read and run).
$dir = Join-Path $work 'game_scripts'
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
$p = Start-Process -FilePath $engine -ArgumentList @('--new-from-template', 'rm_engine', 'game_production', 'game', "`"$dir`"") -Wait -PassThru
$rmpf = Join-Path $dir 'Project\Project.rmpf'
if ($p.ExitCode -ne 0 -or -not (Test-Path $rmpf)) {
    Write-Host "NG  game_scripts: プロジェクトを作れませんでした。$log を見てください"
    $failed++
} else {
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $guid = [Guid]::NewGuid().ToString('N')
    $scenes = Join-Path $dir 'Project\Assets\Scenes'
    New-Item -ItemType Directory -Force $scenes | Out-Null
    $scene = '{"kind": "rm_engine.scene", "format": 1, "nextId": 2, "entities": [{"id": 1, "parent": 0, "name": "Player", "active": true, "components": [' +
             '{"type": "rm::space3d::Transform", "data": {}}, {"type": "ArrowMover", "data": {"speed": 3}}]}]}'
    [IO.File]::WriteAllText((Join-Path $scenes 'Main.rmscene'), $scene, $utf8)
    [IO.File]::WriteAllText((Join-Path $scenes 'Main.rmscene.meta'), "{`"guid`": `"$guid`", `"name`": `"Main.rmscene`"}", $utf8)
    $doc = [IO.File]::ReadAllText($rmpf, $utf8) | ConvertFrom-Json
    $entry = [ordered]@{ author = 'rm_engine'; plugin = 'game_production'; software = 'game';
                         data = [ordered]@{ settings = @{ space = '3d' }; sceneList = [ordered]@{ scenes = @($guid); open = $guid } } }
    $doc | Add-Member -NotePropertyName softwareData -NotePropertyValue @($entry) -Force
    [IO.File]::WriteAllText($rmpf, ($doc | ConvertTo-Json -Depth 20), $utf8)
    $game = Join-Path $dir 'Game'
    $p = Start-Process -FilePath $engine -ArgumentList @('--export', "`"$rmpf`"", "`"$game`"") -Wait -PassThru
    $exe = @(Get-ChildItem $game -Filter *.exe -ErrorAction SilentlyContinue)
    if ($p.ExitCode -ne 0 -or $exe.Count -ne 1) {
        Write-Host "NG  game_scripts: 書き出せませんでした (終了コード $($p.ExitCode))。$log を見てください"
        $failed++
    } elseif (-not (Test-Path (Join-Path $game 'Scripts.dll'))) {
        Write-Host 'NG  game_scripts: Scripts.dll が書き出しに入っていません'
        $failed++
    } elseif (($leak = Find-PcPath (Join-Path $game 'Scripts.dll') $dir) -or ($leak = Find-PcPath $exe[0].FullName $dir)) {
        # (the scripts are built for the export: Release, no debug info - no .pdb path in the DLL)
        Write-Host "NG  game_scripts: Scripts.dll か exe にこの PC のパスが残っています: $leak"
        $failed++
    } elseif (@(Get-ChildItem $game -Filter *.pdb).Count -gt 0) {
        Write-Host 'NG  game_scripts: .pdb が入っています'
        $failed++
    } else {
        $r = Start-Process -FilePath $exe[0].FullName -ArgumentList '--smoke' -Wait -PassThru
        $gameLog = [IO.File]::ReadAllText((Join-Path $game 'game.log'), $utf8)
        $ticked = [regex]::Match($gameLog, '「Player」: (\d+) コマ動きました')
        if ($r.ExitCode -ne 0 -or -not $gameLog.Contains('「Player」を矢印キー・WASD・ゲームパッドで動かせます') -or -not $ticked.Success -or [int]$ticked.Groups[1].Value -lt 1) {
            Write-Host "NG  game_scripts: スクリプトが回っていません (終了コード $($r.ExitCode))"
            $gameLog -split "`n" | Select-Object -Last 12 | ForEach-Object { Write-Host "    $_" }
            $failed++
        } else {
            $size = [Math]::Round($exe[0].Length / 1KB)
            Write-Host "OK  game_scripts: $($exe[0].Name) ($size KB) + Scripts.dll を書き出し、--smoke で BeginPlay と Tick ($($ticked.Groups[1].Value) コマ) が回りました"
        }
    }
}

# A 3D game with a model: the template 「3D のゲーム」 with docs/samples/models/house.gltf (and its two
# pictures) put on an entity. The export bakes it (the engine's form + DDS); the game must put it on the
# GPU as a model with its pictures (game.log), not draw a box.
$dir = Join-Path $work 'game_model'
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
$p = Start-Process -FilePath $engine -ArgumentList @('--new-from-template', 'rm_engine', 'game_x_3d', 'game_3d', "`"$dir`"") -Wait -PassThru
$rmpf = Join-Path $dir 'Project\Project.rmpf'
if ($p.ExitCode -ne 0 -or -not (Test-Path $rmpf)) {
    Write-Host "NG  game_model: プロジェクトを作れませんでした。$log を見てください"
    $failed++
} else {
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $models = Join-Path $dir 'Project\Assets\Models'
    New-Item -ItemType Directory -Force $models | Out-Null
    foreach ($f in 'house.gltf', 'house_walls.png', 'roof_tiles.png') { Copy-Item (Join-Path $root "docs\samples\models\$f") $models }
    $guid = [Guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText((Join-Path $models 'house.gltf.meta'), "{`"guid`": `"$guid`", `"name`": `"house.gltf`"}", $utf8)
    $sceneFile = Join-Path $dir 'Project\Assets\Scenes\Main.rmscene'
    $scene = [IO.File]::ReadAllText($sceneFile, $utf8) | ConvertFrom-Json
    $id = [int]$scene.nextId
    $house = [ordered]@{ id = $id; parent = 0; name = 'House'; active = $true; components = @(
        [ordered]@{ type = 'rm::space3d::Transform'; data = [ordered]@{ position = @(3, 0, 0) } },
        [ordered]@{ type = 'rm::game_x_3d::MeshRenderer'; data = [ordered]@{ model = $guid } }) }
    $scene.entities = @($scene.entities) + @($house)
    $scene.nextId = $id + 1
    [IO.File]::WriteAllText($sceneFile, ($scene | ConvertTo-Json -Depth 20), $utf8)
    $game = Join-Path $dir 'Game'
    $p = Start-Process -FilePath $engine -ArgumentList @('--export', "`"$rmpf`"", "`"$game`"") -Wait -PassThru
    $exe = @(Get-ChildItem $game -Filter *.exe -ErrorAction SilentlyContinue)
    if ($p.ExitCode -ne 0 -or $exe.Count -ne 1) {
        Write-Host "NG  game_model: 書き出せませんでした (終了コード $($p.ExitCode))。$log を見てください"
        $failed++
    } else {
        $r = Start-Process -FilePath $exe[0].FullName -ArgumentList '--smoke' -Wait -PassThru
        $gameLog = [IO.File]::ReadAllText((Join-Path $game 'game.log'), $utf8)
        $up = [regex]::Match($gameLog, "モデルを GPU に上げました: $guid \(三角形 (\d+)・材質 (\d+)・絵 (\d+)\)")
        if ($r.ExitCode -ne 0 -or -not $up.Success -or [int]$up.Groups[1].Value -lt 1 -or [int]$up.Groups[3].Value -lt 1 -or $gameLog.Contains('箱で描きます')) {
            Write-Host "NG  game_model: モデルが形で上がっていません (終了コード $($r.ExitCode))"
            $gameLog -split "`n" | Select-Object -Last 12 | ForEach-Object { Write-Host "    $_" }
            $failed++
        } else {
            $size = [Math]::Round($exe[0].Length / 1KB)
            $pak = [Math]::Round((Get-Item (Join-Path $game 'Game.rmpak')).Length / 1KB)
            Write-Host "OK  game_model: $($exe[0].Name) ($size KB)・Game.rmpak ($pak KB)。モデルを形で GPU に上げました (三角形 $($up.Groups[1].Value)・材質 $($up.Groups[2].Value)・絵 $($up.Groups[3].Value))"
        }
    }
}

# A 2D game with a sprite's image: the file as it is with its size and import settings in front (くっきり
# and 大きさの上限 64 here): the game reads it smaller, くっきり, and knows the file's own size.
$dir = Join-Path $work 'game_sprite'
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
$p = Start-Process -FilePath $engine -ArgumentList @('--new-from-template', 'rm_engine', 'game_x_2d', 'game_2d', "`"$dir`"") -Wait -PassThru
$rmpf = Join-Path $dir 'Project\Project.rmpf'
if ($p.ExitCode -ne 0 -or -not (Test-Path $rmpf)) {
    Write-Host "NG  game_sprite: プロジェクトを作れませんでした。$log を見てください"
    $failed++
} else {
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $images = Join-Path $dir 'Project\Assets\Images'
    New-Item -ItemType Directory -Force $images | Out-Null
    Copy-Item (Join-Path $root 'docs\samples\models\house_walls.png') $images
    $guid = [Guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText((Join-Path $images 'house_walls.png.meta'), "{`"guid`": `"$guid`", `"name`": `"house_walls.png`", `"import`": {`"sharp`": true, `"maxSize`": 64}}", $utf8)
    $sceneFile = Join-Path $dir 'Project\Assets\Scenes\Main.rmscene'
    $scene = [IO.File]::ReadAllText($sceneFile, $utf8) | ConvertFrom-Json
    $id = [int]$scene.nextId
    $wall = [ordered]@{ id = $id; parent = 0; name = 'Wall'; active = $true; components = @(
        [ordered]@{ type = 'rm::space2d::Transform2D'; data = [ordered]@{ position = @(2, 0) } },
        [ordered]@{ type = 'rm::game_x_2d::Sprite'; data = [ordered]@{ image = $guid } }) }
    $scene.entities = @($scene.entities) + @($wall)
    $scene.nextId = $id + 1
    [IO.File]::WriteAllText($sceneFile, ($scene | ConvertTo-Json -Depth 20), $utf8)
    $game = Join-Path $dir 'Game'
    $p = Start-Process -FilePath $engine -ArgumentList @('--export', "`"$rmpf`"", "`"$game`"") -Wait -PassThru
    $exe = @(Get-ChildItem $game -Filter *.exe -ErrorAction SilentlyContinue)
    if ($p.ExitCode -ne 0 -or $exe.Count -ne 1) {
        Write-Host "NG  game_sprite: 書き出せませんでした (終了コード $($p.ExitCode))。$log を見てください"
        $failed++
    } else {
        $r = Start-Process -FilePath $exe[0].FullName -ArgumentList '--smoke' -Wait -PassThru
        $gameLog = [IO.File]::ReadAllText((Join-Path $game 'game.log'), $utf8)
        $read = [regex]::Match($gameLog, "画像を読みました: $guid \((\d+) x (\d+)・くっきり・(\d+) x (\d+) に縮めて\)")
        $fits = $read.Success -and [int]$read.Groups[1].Value -gt 64 -and [Math]::Max([int]$read.Groups[3].Value, [int]$read.Groups[4].Value) -le 64
        if ($r.ExitCode -ne 0 -or -not $fits) {
            Write-Host "NG  game_sprite: スプライトの画像を読めていません (終了コード $($r.ExitCode))"
            $gameLog -split "`n" | Select-Object -Last 10 | ForEach-Object { Write-Host "    $_" }
            $failed++
        } else {
            $size = [Math]::Round($exe[0].Length / 1KB)
            $pak = [Math]::Round((Get-Item (Join-Path $game 'Game.rmpak')).Length / 1KB)
            Write-Host "OK  game_sprite: $($exe[0].Name) ($size KB)・Game.rmpak ($pak KB)。画像 ($($read.Groups[1].Value) x $($read.Groups[2].Value)) を上限 64 に縮めて・くっきりで読みました"
        }
    }
}

# Two scenes (the template 「シーンを切り替える見本」: Title -> Stage by its script GoToScene): both go in,
# the game starts with the first, and the script's Game::LoadScene switches in the exported game (its
# 「自動で切り替える (秒)」 set here, so --smoke switches without a key).
$dir = Join-Path $work 'game_scenes'
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
$p = Start-Process -FilePath $engine -ArgumentList @('--new-from-template', 'rm_engine', 'game_x_2d', 'scenes_2d', "`"$dir`"") -Wait -PassThru
$rmpf = Join-Path $dir 'Project\Project.rmpf'
if ($p.ExitCode -ne 0 -or -not (Test-Path $rmpf)) {
    Write-Host "NG  game_scenes: プロジェクトを作れませんでした。$log を見てください"
    $failed++
} else {
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $sceneFile = Join-Path $dir 'Project\Assets\Scenes\Title.rmscene'
    $text = [IO.File]::ReadAllText($sceneFile, $utf8) -replace '"after": 0', '"after": 0.000001'
    [IO.File]::WriteAllText($sceneFile, $text, $utf8)
    $game = Join-Path $dir 'Game'
    $p = Start-Process -FilePath $engine -ArgumentList @('--export', "`"$rmpf`"", "`"$game`"") -Wait -PassThru
    $exe = @(Get-ChildItem $game -Filter *.exe -ErrorAction SilentlyContinue)
    if ($p.ExitCode -ne 0 -or $exe.Count -ne 1) {
        Write-Host "NG  game_scenes: 書き出せませんでした (終了コード $($p.ExitCode))。$log を見てください"
        $failed++
    } else {
        $r = Start-Process -FilePath $exe[0].FullName -ArgumentList '--smoke' -Wait -PassThru
        $gameLog = [IO.File]::ReadAllText((Join-Path $game 'game.log'), $utf8)
        $ok = $r.ExitCode -eq 0 -and $gameLog.Contains('シーン 2 個。最初のシーン: Title') -and $gameLog.Contains('シーンを切り替えました: Title → Stage') -and
              $gameLog.Contains('シーン「Title」を出ます') -and $gameLog.Contains('シーン「Stage」(2 / 2)')
        if (-not $ok) {
            Write-Host "NG  game_scenes: シーンが切り替わっていません (終了コード $($r.ExitCode))"
            $gameLog -split "`n" | Select-Object -Last 12 | ForEach-Object { Write-Host "    $_" }
            $failed++
        } else {
            Write-Host "OK  game_scenes: 2 つのシーンを書き出し、Title から始まって Game::LoadScene で Stage に切り替わりました (EndPlay → BeginPlay)"
        }
    }
}

if ($failed -gt 0) { Write-Host "check_export: $failed 個が失敗しました"; exit 1 }
Write-Host 'check_export: すべて書き出して起動できました'
exit 0
