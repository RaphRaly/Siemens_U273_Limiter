# run_demo_bench.ps1
#
# Locates the freshest u273_bench.exe under build/, picks a Demo/bench_<stamp>
# output directory, runs the bench, and reports success/failure to the caller.
# Designed for Windows PowerShell 5.1 -- no pwsh-only syntax.

[CmdletBinding()]
param(
    [string] $OutputDir,
    [string] $DatasetDir,
    [double] $SampleRate = 0
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
if (-not $projectRoot) {
    $projectRoot = (Get-Location).Path
}

$buildRoot = Join-Path $projectRoot "build"
if (-not (Test-Path $buildRoot)) {
    Write-Host "Scientific bench skipped: build/ does not exist (build the project first)" -ForegroundColor Yellow
    exit 1
}

$benchCandidates = Get-ChildItem -Path $buildRoot -Filter "u273_bench.exe" -Recurse -ErrorAction SilentlyContinue `
    | Where-Object { -not $_.PSIsContainer }
if (-not $benchCandidates) {
    Write-Host "Scientific bench skipped: no u273_bench.exe found under build/" -ForegroundColor Yellow
    exit 1
}
$benchExe = ($benchCandidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName

if (-not $OutputDir -or $OutputDir -eq "") {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $projectRoot "Demo\bench_$stamp"
}
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$benchArgs = @("--output-dir", $OutputDir)
if ($DatasetDir -and $DatasetDir -ne "") {
    $benchArgs += @("--dataset-dir", $DatasetDir)
}
if ($SampleRate -gt 0) {
    $benchArgs += @("--sample-rate", ([string]$SampleRate))
}

Write-Host "==> Running u273_bench" -ForegroundColor Cyan
Write-Host "    exe : $benchExe"
Write-Host "    out : $OutputDir"

& $benchExe @benchArgs
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    Write-Host "u273_bench failed with exit code $exitCode" -ForegroundColor Red
    exit $exitCode
}

$reportPath = Join-Path $OutputDir "SCIENTIFIC_REPORT.md"
if (Test-Path $reportPath) {
    Write-Host "Scientific bench written to $reportPath" -ForegroundColor Green
} else {
    Write-Host "u273_bench exited 0 but no SCIENTIFIC_REPORT.md was produced" -ForegroundColor Yellow
    exit 1
}
exit 0
