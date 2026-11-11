# Builds and runs the 3D renderer's CPU benchmark (tools/bench3d/Bench3D.cpp). Needs a GPU.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench3d\run.ps1 [-Frames 300]
param([int]$Frames = 300)

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$out = Join-Path $root 'build\bench3d'
New-Item -ItemType Directory -Force $out | Out-Null

$sources = @(
    'tools\bench3d\Bench3D.cpp',
    'plugins\rm_engine\space3d\runtime\Renderer3D.cpp',
    'plugins\rm_engine\space3d\runtime\Lighting.cpp',
    'plugins\rm_engine\space3d\runtime\View.cpp',
    'plugins\rm_engine\space3d\runtime\Transform.cpp',
    'plugins\rm_engine\space3d\runtime\Mesh.cpp',
    'plugins\rm_engine\space3d\runtime\Fbx.cpp',
    'plugins\rm_engine\space3d\runtime\Inflate.cpp',
    'plugins\rm_engine\space3d\runtime\TexturePixels.cpp',
    'src\platform\Image.cpp',
    'src\base\Json.cpp',
    'src\base\Utf.cpp',
    'src\base\Log.cpp'
) | ForEach-Object { '"' + (Join-Path $root $_) + '"' }

$cmd = "`"$vcvars`" >nul && cl /nologo /std:c++20 /O2 /EHsc /MT /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE " +
       "/I`"$root\src`" /I`"$root\plugins`" /Fo`"$out\\`" /Fe`"$out\Bench3D.exe`" $($sources -join ' ') " +
       "/link d3d11.lib dxgi.lib d3dcompiler.lib d2d1.lib windowscodecs.lib ole32.lib"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Host 'BENCH BUILD FAILED'; exit $LASTEXITCODE }
& (Join-Path $out 'Bench3D.exe') $Frames
exit $LASTEXITCODE
