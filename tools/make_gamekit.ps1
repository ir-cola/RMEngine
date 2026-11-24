# <exe のフォルダ>/GameKit/ を RMGameKit のオブジェクトから作る (src/RMGameKit.vcxproj が、そのビルドの後に走らせる):
#   kit.json                               ここに何があるか、どう作ったか (C++ のランタイム)、システムライブラリ
#   lib/core.lib  + lib/core/reflect/       ゲームに入る層 (base core platform ui game) と、
#                                           単独で作った外のライブラリ (-ExtraCoreLibs: Jolt Physics)
#   lib/<author>/<name>/runtime.lib  + reflect/   ランタイム部のあるプラグインごとに 1 つ
#   lib/<author>/<name>/licenses/*.txt     そのランタイムが使う外のコードのライセンスの文章
#                                           (プラグインの licenses/ から。ファイル名 = ソフトの名前)。
#                                           書き出しがゲームの横の LICENSES.txt にまとめる
# reflect/ には、lib のヘッダーの rmgen のファイルが入る: 書き出しはそこから反射した型の名前を読み、
# ゲームが生成する main に並べる (editor/export ExportPlan: ReadSymbols)。
# プラグインの "entry" は、そのランタイムのソースに RM_RUNTIME_ENTRY があるかどうか (core/RuntimeRegistry.h)。
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$ObjDir,
    [Parameter(Mandatory = $true)][string]$GenDir,
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(Mandatory = $true)][string]$LibExe,
    [Parameter(Mandatory = $true)][string]$Crt,
    [string[]]$ExtraCoreLibs = @()
)

$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding $false
$Root = (Resolve-Path $Root).Path.TrimEnd('\')
$ObjDir = (Resolve-Path $ObjDir).Path.TrimEnd('\')
$GenDir = (Resolve-Path $GenDir).Path.TrimEnd('\')
if (-not (Test-Path $LibExe)) { Write-Host "make_gamekit: lib.exe が見つかりません: $LibExe"; exit 1 }

$lib = Join-Path $Out 'lib'
if (Test-Path $lib) { Remove-Item -Recurse -Force $lib }
New-Item -ItemType Directory -Force $lib | Out-Null

$generated = @(Get-ChildItem (Join-Path $ObjDir 'generated') -Filter *.obj -ErrorAction SilentlyContinue)

function Make-Lib([string]$outLib, [string[]]$objs, [string]$reflectPrefixes) {
    if ($objs.Count -eq 0) { return $false }
    New-Item -ItemType Directory -Force (Split-Path $outLib) | Out-Null
    $rsp = "$outLib.rsp"
    [IO.File]::WriteAllLines($rsp, ($objs | ForEach-Object { '"' + $_ + '"' }), $utf8)
    & $LibExe /NOLOGO "/OUT:$outLib" "@$rsp" | Out-Host
    $code = $LASTEXITCODE
    Remove-Item $rsp
    if ($code -ne 0) { Write-Host "make_gamekit: lib.exe が失敗しました ($outLib)"; exit $code }
    return $true
}

function Generated-For([string[]]$prefixes) {
    return @($generated | Where-Object { $n = $_.BaseName; ($prefixes | Where-Object { $n.StartsWith($_) }).Count -gt 0 })
}

function Copy-Reflect([string]$dir, [string[]]$prefixes) {
    $to = Join-Path $dir 'reflect'
    New-Item -ItemType Directory -Force $to | Out-Null
    foreach ($f in Get-ChildItem $GenDir -Filter *.cpp) {
        if ($f.Name -eq 'rm_all_types.cpp') { continue }
        foreach ($p in $prefixes) { if ($f.Name.StartsWith($p)) { Copy-Item $f.FullName $to; break } }
    }
}

# 核: base core platform ui game
$corePrefixes = @('base_', 'core_', 'platform_', 'ui_', 'game_')
$coreObjs = @(Get-ChildItem (Join-Path $ObjDir 'k') -Recurse -Filter *.obj | Where-Object { $_.FullName -notlike "*\k\..\plugins\*" } | ForEach-Object FullName)
$coreObjs = @($coreObjs | Where-Object { -not $_.StartsWith((Join-Path $ObjDir 'plugins')) })
$coreObjs += @(Generated-For $corePrefixes | ForEach-Object FullName)
# lib.exe は .lib ファイルも入力に取る: その中身は core.lib に入る (ゲームはそのうち使うものだけをリンクする)
foreach ($x in $ExtraCoreLibs) {
    if (-not (Test-Path $x)) { Write-Host "make_gamekit: $x がありません"; exit 1 }
    $coreObjs += (Resolve-Path $x).Path
}
Make-Lib (Join-Path $lib 'core.lib') $coreObjs | Out-Null
Copy-Reflect (Join-Path $lib 'core') $corePrefixes

# プラグイン
$plugins = @()
foreach ($authorDir in Get-ChildItem (Join-Path $Root 'plugins') -Directory) {
    foreach ($pluginDir in Get-ChildItem $authorDir.FullName -Directory) {
        $author = $authorDir.Name; $name = $pluginDir.Name
        $objFolder = Join-Path $ObjDir "plugins\$author\$name"
        $objs = @()
        if (Test-Path $objFolder) { $objs = @(Get-ChildItem $objFolder -Recurse -Filter *.obj | ForEach-Object FullName) }
        $prefix = "${author}_${name}_runtime_"
        $objs += @(Generated-For @($prefix) | ForEach-Object FullName)
        $dir = Join-Path $lib "$author\$name"
        if (-not (Make-Lib (Join-Path $dir 'runtime.lib') $objs)) { continue }
        Copy-Reflect $dir @($prefix)
        $entry = $false
        $rt = Join-Path $pluginDir.FullName 'runtime'
        if (Test-Path $rt) {
            foreach ($f in Get-ChildItem $rt -Recurse -Filter *.cpp) {
                if ([IO.File]::ReadAllText($f.FullName, $utf8) -match 'RM_RUNTIME_ENTRY\(') { $entry = $true; break }
            }
        }
        $licenses = @()
        $licenseDir = Join-Path $pluginDir.FullName 'licenses'
        if (Test-Path $licenseDir) {
            $to = Join-Path $dir 'licenses'
            New-Item -ItemType Directory -Force $to | Out-Null
            foreach ($f in Get-ChildItem $licenseDir -Filter *.txt -File | Sort-Object Name) {
                Copy-Item $f.FullName $to
                $licenses += "lib/$author/$name/licenses/$($f.Name)"
            }
        }
        $plugins += [ordered]@{ author = $author; name = $name; lib = "lib/$author/$name/runtime.lib"; entry = $entry; licenses = @($licenses) }
    }
}

$kit = [ordered]@{
    kind = 'rm_engine.gamekit'; format = 1
    crt = $Crt
    core = 'lib/core.lib'
    systemLibs = @('kernel32.lib', 'user32.lib', 'gdi32.lib', 'advapi32.lib', 'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib',
                   'comdlg32.lib', 'shlwapi.lib', 'd3d11.lib', 'dxgi.lib', 'd2d1.lib', 'dwrite.lib', 'windowscodecs.lib', 'dwmapi.lib',
                   'imm32.lib', 'shcore.lib', 'bcrypt.lib', 'd3dcompiler.lib', 'dxguid.lib', 'xaudio2.lib', 'mfuuid.lib')
    plugins = @($plugins)
}
[IO.File]::WriteAllText((Join-Path $Out 'kit.json'), ($kit | ConvertTo-Json -Depth 5), $utf8)
Write-Host "make_gamekit: GameKit を作りました (プラグイン $($plugins.Count) 個): $Out"
exit 0
