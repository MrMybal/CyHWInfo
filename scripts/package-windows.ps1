[CmdletBinding()]
param(
    [string]$BuildDir = 'out/windows-release',
    [string]$OutputDir = 'out/windows-dist',
    [string]$CyUISourceDir = '',
    [string]$HwInfoSourceDir = '',
    [string]$JsonSourceDir = '',
    [string]$IsccPath = '',
    [string]$ReleaseTag = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$versionMatch = Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern '^project\(CyHWInfo VERSION (\d+\.\d+\.\d+)\s'
if (-not $versionMatch) { throw 'CyHWInfo needs a three-part CMake project version.' }
$version = $versionMatch.Matches[0].Groups[1].Value
if ($ReleaseTag -and $ReleaseTag -cne "v$version") { throw "Release tag $ReleaseTag must match v$version." }

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source }
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 with the C++ workload is required.' }
    $vs = & $vswhere -latest -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio 2022 with the C++ workload is required.' }
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not $IsccPath) {
    $isccCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    $IsccPath = if ($isccCommand) { $isccCommand.Source } else {
        Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
    }
}
if (-not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) { throw 'Install Inno Setup 6 or pass -IsccPath.' }

$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$stage = Join-Path $BuildDir ('install-' + [Guid]::NewGuid().ToString('N'))
$components = 'Cpu;Gpu;GpuTelemetry;GpuProcesses;Memory;Storage;StorageHealth;Motherboard;Battery;Network;LibreHardwareMonitor'
$configure = @('-S', $root, '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DCYHWINFO_BUILD_APP=ON', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', "-DCYHWINFO_COMPONENTS=$components")
if ($CyUISourceDir) { $configure += "-DCYHWINFO_CYUI_SOURCE_DIR=$((Resolve-Path -LiteralPath $CyUISourceDir).Path)" }
if ($HwInfoSourceDir) { $configure += "-DFETCHCONTENT_SOURCE_DIR_LFREIST_HWINFO=$((Resolve-Path -LiteralPath $HwInfoSourceDir).Path)" }
if ($JsonSourceDir) { $configure += "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$((Resolve-Path -LiteralPath $JsonSourceDir).Path)" }

& $cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $BuildDir --config Release --target cyhwinfo_monitor --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'CyHWInfo build failed.' }
& $cmake --install $BuildDir --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'CMake staging failed.' }

$setupScript = Join-Path $root 'packaging/CyHWInfo.iss'
& $IsccPath "/DAppVersion=$version" "/DStageDir=$stage" "/O$OutputDir" $setupScript
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$setup = Join-Path $OutputDir "CyHWInfo-$version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw "Inno Setup did not produce $setup." }
Get-FileHash -LiteralPath $setup -Algorithm SHA256
