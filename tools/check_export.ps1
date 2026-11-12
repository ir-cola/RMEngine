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
if ($failed -gt 0) { Write-Host "check_export: $failed 個が失敗しました"; exit 1 }
Write-Host 'check_export: すべて書き出して起動できました'
exit 0
