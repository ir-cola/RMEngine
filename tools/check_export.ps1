# The export check (design NewEngineBluePrint 11-b-2, rule 5): makes a project from each template,
# exports it (RM_Engine.exe --export-template: no window) and starts the game with --smoke (a few frames,
# exit code 0). Also checks the exported folder: the exe, Game.rmpak, no .pdb, and no path of this PC in
# the exe. Exit code 0 = every game was exported and ran.
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
    @('rm_engine', 'game_x_3d', 'game_3d')
)
$failed = 0
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
    $bytes = [IO.File]::ReadAllBytes($exe[0].FullName)
    $text8 = [Text.Encoding]::UTF8.GetString($bytes)
    $text16 = [Text.Encoding]::Unicode.GetString($bytes)
    $leak = $null
    foreach ($path in @($root, $dir, $env:USERPROFILE)) {
        if ($text8.IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or $text16.IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $leak = $path }
    }
    if ($leak) { Write-Host "NG  $($t[2]): exe にこの PC のパスが残っています: $leak"; $failed++; continue }
    $r = Start-Process -FilePath $exe[0].FullName -ArgumentList '--smoke' -Wait -PassThru
    if ($r.ExitCode -ne 0) {
        Write-Host "NG  $($t[2]): ゲームが --smoke で失敗しました (終了コード $($r.ExitCode))"
        Get-Content (Join-Path $game 'game.log') -Encoding UTF8 -ErrorAction SilentlyContinue | Select-Object -Last 10 | ForEach-Object { Write-Host "    $_" }
        $failed++
        continue
    }
    $size = [Math]::Round($exe[0].Length / 1KB)
    Write-Host "OK  $($t[2]): $($exe[0].Name) ($size KB) を書き出し、--smoke で起動して閉じました"
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
    } else {
        $r = Start-Process -FilePath $exe[0].FullName -ArgumentList '--smoke' -Wait -PassThru
        $gameLog = [IO.File]::ReadAllText((Join-Path $game 'game.log'), $utf8)
        $ticked = [regex]::Match($gameLog, '「Player」: (\d+) コマ動きました')
        if ($r.ExitCode -ne 0 -or -not $gameLog.Contains('「Player」を矢印キーで動かせます') -or -not $ticked.Success -or [int]$ticked.Groups[1].Value -lt 1) {
            Write-Host "NG  game_scripts: スクリプトが回っていません (終了コード $($r.ExitCode))"
            $gameLog -split "`n" | Select-Object -Last 12 | ForEach-Object { Write-Host "    $_" }
            $failed++
        } else {
            $size = [Math]::Round($exe[0].Length / 1KB)
            Write-Host "OK  game_scripts: $($exe[0].Name) ($size KB) + Scripts.dll を書き出し、--smoke で BeginPlay と Tick ($($ticked.Groups[1].Value) コマ) が回りました"
        }
    }
}

if ($failed -gt 0) { Write-Host "check_export: $failed 個が失敗しました"; exit 1 }
Write-Host 'check_export: すべて書き出して起動できました'
exit 0
