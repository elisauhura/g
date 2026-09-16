param([string]$GPath = (Join-Path $PSScriptRoot '..\g.exe'))
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
. (Join-Path $PSScriptRoot 'MSVCSetup.ps1')
$gCommand = (Resolve-Path -LiteralPath $GPath).Path
$tempParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$fixtureName = 'g build tests ' + [guid]::NewGuid().ToString('N')
$fixtureRoot = Join-Path $tempParent $fixtureName
[System.IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
$targetName = "windows-$GArchitecture-msvc"
function Write-Fixture([string]$Relative, [string]$Text) {
    $path = Join-Path $fixtureRoot $Relative
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($path)) | Out-Null
    [System.IO.File]::WriteAllText($path, $Text)
}
function Build([int]$Expected, [string[]]$BuildArgs) {
    $messages = @(& $gCommand build "--project=$fixtureRoot" "--arch=$GArchitecture" '--target=windows/msvc' @BuildArgs 2>&1)
    if ($LASTEXITCODE -ne $Expected) { throw "Expected $Expected for $BuildArgs, got $LASTEXITCODE : $messages" }
    return $messages -join "`n"
}
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
try {
    Write-Fixture 'project.g' '// fixture project'
    Write-Fixture 'include/api.h' "#pragma once`nint leaf(void);`nint math_value(void);`nvoid test_marker(void);`n"
    Write-Fixture 'lib/leaf/dep.g' 'leaf := lib { .dep: {} }'
    Write-Fixture 'lib/leaf/nested/value.h' "#define LEAF_VALUE 7`n"
    Write-Fixture 'lib/leaf/leaf.c' "#include `"nested/value.h`"`nint leaf(void) { return LEAF_VALUE; }`n//a`n"
    Write-Fixture 'lib/math/dep.g' "/* comment */ math := lib { .dep: {`"leaf`", `"leaf`",}, };"
    Write-Fixture 'lib/math/math.c' "#include `"api.h`"`nint math_value(void) { return leaf(); }`n"
    Write-Fixture 'lib/test/dep.g' 'test := lib {}'
    Write-Fixture 'lib/test/test.c' "void test_marker(void) {}`n"
    Write-Fixture 'cmd/demo/dep.g' 'demo := cmd { .dep: {"math"} }'
    Write-Fixture 'cmd/demo/main.c' "#include `"api.h`"`nint main(void) { return math_value() == 7 ? 0 : 1; }`n"
    Write-Fixture 'tests/smoke/dep.g' 'smoke := test { .dep: {"math"} }'
    Write-Fixture 'tests/smoke/test.c' "#include `"api.h`"`nint main(void) { test_marker(); return math_value() == 7 ? 0 : 1; }`n"
    $first = Build 0 @('all')
    $outputRoot = Join-Path $fixtureRoot "out/$targetName"
    & (Join-Path $outputRoot 'exe/demo.exe')
    Require ($LASTEXITCODE -eq 0) 'Transitive dependency executable failed.'
    & (Join-Path $outputRoot 'exe/test-smoke.exe')
    Require ($LASTEXITCODE -eq 0) 'Implicit test-library executable failed.'
    $staged = Join-Path $outputRoot 'lib/leaf/leaf.c'
    $original = Join-Path $fixtureRoot 'lib/leaf/leaf.c'
    Require (Test-Path -LiteralPath (Join-Path $outputRoot 'lib/leaf/nested/value.h')) 'Local header was not staged.'
    Require (([System.IO.File]::ReadAllText($staged)) -eq ([System.IO.File]::ReadAllText($original))) 'C copy differs.'
    $before = [System.IO.File]::GetLastWriteTimeUtc($staged)
    $second = Build 0 @('lib/leaf')
    Require ($second -match '0 files copied') 'Unchanged copies were not skipped.'
    Require ([System.IO.File]::GetLastWriteTimeUtc($staged) -eq $before) 'Skipped copy changed its timestamp.'
    [System.IO.File]::AppendAllText($original, "// changed`n")
    $changed = Build 0 @('lib/leaf')
    Require ($changed -match '1 files copied') 'Changed source was not recopied.'
    Require ([System.IO.File]::ReadAllText($staged).Contains('// changed')) 'Changed C source was not staged.'

    $stamp = [System.IO.File]::GetLastWriteTimeUtc($original)
    $content = [System.IO.File]::ReadAllText($original).Replace('//a', '//b')
    [System.IO.File]::WriteAllText($original, $content)
    [System.IO.File]::SetLastWriteTimeUtc($original, $stamp)
    $forced = Build 0 @('lib/leaf', '--force')
    Require ([System.IO.File]::ReadAllText($staged).Contains('//b')) '--force failed to refresh equal-metadata content.'

    $null = Build 1 @('cmd/unknown')
    Write-Fixture 'lib/math/dep.g' 'math := lib { .dep: {"missing"} }'
    $null = Build 1 @('cmd/demo')
    Write-Fixture 'lib/math/dep.g' 'math := lib { .dep: {"leaf"} }'
    Write-Fixture 'lib/leaf/dep.g' 'leaf := lib { .dep: {"math"} }'
    $null = Build 1 @('cmd/demo')
    Write-Fixture 'lib/leaf/dep.g' 'leaf := lib { .dep: {} }'
    Write-Fixture 'cmd/demo/dep.g' 'demo := cmd { .unexpected: {} }'
    $null = Build 1 @('cmd/demo')
    Write-Fixture 'cmd/demo/dep.g' 'demo := cmd { .dep: {"math"} }'
    Write-Fixture 'cmd/demo/dep.g' "demo := cmd { .dep: {`"math`n`"} }"
    $null = Build 1 @('cmd/demo')
    Write-Fixture 'cmd/demo/dep.g' 'demo := cmd { .dep: {"math"} }'
    Write-Fixture 'cmd/demo/extra.g' 'export unsupported \() int { return 0 }'
    $null = Build 1 @('cmd/demo')
    Require (-not (Test-Path -LiteralPath (Join-Path $outputRoot 'cmd/demo/extra.c'))) 'G source was copied as C.'
    Remove-Item -LiteralPath (Join-Path $fixtureRoot 'cmd/demo/extra.g')
    Write-Fixture 'cmd/demo/main.c' 'this is not valid C'
    $null = Build 1 @('cmd/demo')
    Write-Fixture 'cmd/demo/main.c' 'extern int missing(void); int main(void) { return missing(); }'
    $null = Build 1 @('cmd/demo')
    Write-Host 'Build integration tests passed (dependencies, staging, timestamps, paths with spaces, and failure propagation).'
} finally {
    $resolved = (Resolve-Path -LiteralPath $fixtureRoot).ProviderPath
    if (-not $resolved.StartsWith($tempParent, [System.StringComparison]::OrdinalIgnoreCase) -or
        [System.IO.Path]::GetFileName($resolved) -ne $fixtureName) {
        throw "Refusing to remove unexpected fixture path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
