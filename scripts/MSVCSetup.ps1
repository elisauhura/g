# Dot-source this script to retain the compiler environment.
param([ValidateSet('amd64', 'arm64')][string]$Architecture)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'MSVCSetup.ps1 requires Windows.' }
if (-not $Architecture) {
    $Architecture = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'amd64' }
}
$targetArch = if ($Architecture -eq 'amd64') { 'x64' } else { 'arm64' }
$ready = $env:VSCMD_ARG_TGT_ARCH -eq $targetArch
foreach ($tool in @('cl.exe', 'lib.exe', 'link.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { $ready = $false }
}
if (-not $ready) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Install Visual Studio C++ build tools (vswhere.exe was not found).' }
    $component = if ($Architecture -eq 'arm64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' } else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
    $installation = & $vswherePath -latest -prerelease -products '*' -requires $component -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw "No Visual Studio installation with $Architecture C++ tools was found." }
    $launch = Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1'
    $savedLocation = Get-Location
    try { & $launch -HostArch amd64 -Arch $targetArch -SkipAutomaticLocation }
    finally { Set-Location -LiteralPath $savedLocation.Path }
}
if ($env:VSCMD_ARG_TGT_ARCH -ne $targetArch) { throw "MSVC target is not $targetArch." }
foreach ($tool in @('cl.exe', 'lib.exe', 'link.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "MSVC setup did not provide $tool." }
}
$GArchitecture = $Architecture
