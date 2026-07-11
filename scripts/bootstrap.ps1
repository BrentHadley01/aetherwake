param([switch]$Build, [switch]$NoDependencies)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeCandidates = @(@(
    (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
    'C:\Program Files\CMake\bin\cmake.exe'
) | Where-Object { $_ -and (Test-Path $_) })
if ($cmakeCandidates.Count -eq 0) { throw 'CMake was not found. Install Kitware.CMake or add cmake to PATH.' }
$cmake = $cmakeCandidates[0]
$generatorArgs = @()
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) { $generatorArgs = @('-G', 'Visual Studio 17 2022', '-A', 'x64') }
}
if ($generatorArgs.Count -eq 0) {
    $localToolchain = Join-Path $projectRoot 'tools\w64devkit\w64devkit\bin'
    if (Test-Path (Join-Path $localToolchain 'g++.exe')) {
        $env:PATH = "$localToolchain;$env:PATH"
        $generatorArgs = @('-G', 'MinGW Makefiles')
    }
}
if ($generatorArgs.Count -eq 0) { throw 'No supported C++ toolchain found. Run the local w64devkit installer or install Visual Studio C++ Build Tools.' }
$dependencyOption = if ($NoDependencies) { 'OFF' } else { 'ON' }
& $cmake -S $projectRoot -B "$projectRoot\build" @generatorArgs "-DAETHERWAKE_FETCH_DEPS=$dependencyOption"
if ($Build) { & $cmake --build "$projectRoot\build" --config Debug }
