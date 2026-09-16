# Usage: .\Setup.ps1 [--force]  (also accepts -Force)
$ErrorActionPreference = 'Stop'
$forceBootstrap = $false
foreach ($argument in $args) {
    if ($argument -ieq '--force' -or $argument -ieq '-Force') { $forceBootstrap = $true }
    else { throw "Unknown setup argument: $argument" }
}
$projectRoot = $PSScriptRoot
. (Join-Path $projectRoot 'scripts\MSVCSetup.ps1')
$gExecutable = Join-Path $projectRoot 'g.exe'
if ((Test-Path -LiteralPath $gExecutable -PathType Leaf) -and -not $forceBootstrap) {
    Write-Host 'g.exe already exists. Use --force to bootstrap again.'
    return
}
$bootstrapDirectory = Join-Path $projectRoot "out\bootstrap\$GArchitecture"
$objectDirectory = Join-Path $bootstrapDirectory 'obj'
[System.IO.Directory]::CreateDirectory($objectDirectory) | Out-Null
$bootstrapExecutable = Join-Path $bootstrapDirectory 'bootstrap-g.exe'
$bootstrapSources = @(
    'cmd/g/main.c',
    'lib/cmdline/cmdline.c',
    'lib/buildbe/buildbe.c',
    'lib/buildbe/host.c',
    'lib/buildfe/buildfe.c',
    'lib/buildfe/cli.c'
) | ForEach-Object { Join-Path $projectRoot $_ }
$compilerArgs = @('/nologo', '/TC', '/std:c11', '/W4', '/O2',
    "/I$(Join-Path $projectRoot 'include')", "/Fo$objectDirectory\", "/Fe$bootstrapExecutable")
Write-Host "Building bootstrap-g for $GArchitecture..."
& cl.exe @compilerArgs @bootstrapSources
if ($LASTEXITCODE -ne 0) { throw "Bootstrap compilation failed (exit $LASTEXITCODE)." }
Write-Host 'Building g with bootstrap-g...'
& $bootstrapExecutable build cmd/g "--project=$projectRoot" '--target=windows/msvc' "--arch=$GArchitecture"
if ($LASTEXITCODE -ne 0) { throw "bootstrap-g failed to build g (exit $LASTEXITCODE)." }
$builtExecutable = Join-Path $projectRoot "out\windows-$GArchitecture-msvc\exe\g.exe"
if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) { throw 'The completed build did not produce g.exe.' }
Copy-Item -LiteralPath $builtExecutable -Destination $gExecutable -Force
Write-Host "Ready: $gExecutable"
