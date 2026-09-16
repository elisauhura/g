param([Parameter(Mandatory = $true)][string]$Runner)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$runnerPath = (Resolve-Path -LiteralPath $Runner).Path
$workPath = Join-Path ([System.IO.Path]::GetTempPath()) ('g-test-' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($workPath) | Out-Null
$reportPath = Join-Path $workPath 'report.json'
$monitorPath = Join-Path $workPath 'monitor.jsonl'
$metadataPath = Join-Path $workPath 'metadata.json'
$missingPath = Join-Path $workPath 'missing/result.json'

function Invoke-Checked([int]$Expected, [string[]]$RunnerArgs) {
    $messages = & $runnerPath @RunnerArgs 2>&1
    if ($LASTEXITCODE -ne $Expected) {
        throw "Expected exit $Expected, got $LASTEXITCODE for $RunnerArgs : $messages"
    }
}
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

try {
    Invoke-Checked 0 @('--output', $reportPath, "--monitor=$monitorPath")
    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    Require ($report.artifacts.Count -eq 2 -and $report.artifacts[0] -eq 'alpha' -and $report.artifacts[1] -eq 'zeta') 'Artifact order differs.'
    Require ($report.tests.Count -eq 1 -and $report.tests[0].passed) 'Top-level test failed.'
    Require ($report.tests[0].tests.Count -eq 1 -and $report.tests[0].tests[0].passed) 'Nested test failed.'
    Require ($report.tests[0].tests[0].parent -eq $report.tests[0].testIndex) 'Nested parent differs.'
    Require ($report.benchmarks[0].runs -eq 10 -and $report.benchmarks[0].warmUpRuns -eq 2) 'Benchmark configuration differs.'
    $events = @(Get-Content -LiteralPath $monitorPath | ForEach-Object { $_ | ConvertFrom-Json })
    Require ($events[0].event -eq 'startSuite' -and $events[-1].event -eq 'stopSuite') 'Suite events missing.'
    Require (@($events | Where-Object event -eq 'registeredArtifact').Count -eq 2) 'Artifact events missing.'
    Require (@($events | Where-Object event -eq 'startTest').Count -eq 2) 'Nested test events missing.'

    Invoke-Checked 0 @('--fixture=metadata', '--emit-metadata', $metadataPath)
    $metadata = Get-Content -Raw -LiteralPath $metadataPath | ConvertFrom-Json
    Require ($metadata.schema -eq 1 -and $metadata.functionCount -eq 4) 'Metadata counts differ.'
    Require ($metadata.artifacts[0] -eq 'TestArtifact_Alpha') 'Metadata is not sorted.'
    Invoke-Checked 0 @('--fixture=metadata', "--emit-metadata=$metadataPath")

    Invoke-Checked 1 @('--fixture=failure', "--output=$reportPath", '--monitor', $monitorPath)
    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    Require (-not $report.tests[0].passed -and -not $report.tests[0].tests[0].passed) 'Nested failure did not propagate.'
    Require ($report.tests[0].tests[0].logs[0].message -eq "line one`n`"quoted`"\path") 'JSON log escaping differs.'

    Invoke-Checked 0 @('--fixture=empty', "--output=$reportPath")
    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    Require ($report.tests.Count -eq 0 -and $report.benchmarks.Count -eq 0) 'Empty suite failed.'
    Invoke-Checked 0 @('--fixture=empty', "--emit-metadata=$metadataPath")

    Invoke-Checked 2 @('--unknown')
    Invoke-Checked 2 @('--output')
    Invoke-Checked 2 @('--output=')
    Invoke-Checked 2 @('--monitor', '--output', $reportPath)
    Invoke-Checked 2 @('--emit-metadata', $metadataPath, '--output', $reportPath)
    Invoke-Checked 2 @('--emit-metadata', $metadataPath, '--emit-metadata', $metadataPath)
    Invoke-Checked 2 @('--output', $reportPath, '--output', $reportPath)
    Invoke-Checked 2 @('--output', $reportPath, '--monitor', $reportPath)
    Invoke-Checked 2 @('--emit-metadata', $missingPath)
    Invoke-Checked 2 @('--monitor', $missingPath)
    Invoke-Checked 1 @('--output', $missingPath)
    Write-Host 'Test runner validation passed.'
} finally {
    foreach ($file in @($reportPath, $monitorPath, $metadataPath)) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
    Remove-Item -LiteralPath $workPath
}
