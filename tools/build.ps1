# ソリューションを作り、テストとレイヤー検査を走らせる。
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 [-Config Debug|Release] [-NoTest]
# 成否は終了コードだけで決める (ビルドの出力を grep しない: MSBuild は長い行を折り返し、
# PowerShell でパイプすると $LASTEXITCODE を失うことがある - 前のエンジン 4-24)。
param(
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [switch]$NoTest
)

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Write-Host 'Visual Studio is not installed (vswhere.exe not found).'
    exit 2
}
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) {
    Write-Host 'MSBuild was not found. Install the "Desktop development with C++" workload.'
    exit 2
}

New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
$log = Join-Path $root "build\msbuild_$Config.log"
& $msbuild (Join-Path $root 'RM_Engine.sln') /nologo /v:minimal /m "/p:Configuration=$Config" /p:Platform=x64 "/flp:LogFile=$log;Verbosity=normal;Encoding=UTF-8"
$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Host "BUILD FAILED (exit $code). Full log: $log"
    exit $code
}
Write-Host "BUILD OK ($Config)"

if (-not $NoTest) {
    & (Join-Path $root "build\bin\x64\$Config\RMTests.exe")
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Write-Host "TESTS FAILED (exit $code)"
        exit $code
    }
}

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\check_layers.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# 画面に出す文字には必ず英語がある (editor/lang)。
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\check_lang.ps1')
exit $LASTEXITCODE
