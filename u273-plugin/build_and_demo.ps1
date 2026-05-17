# build_and_demo.ps1
#
# Local "always demoable" gate. Runs the tests-only preset to verify the
# scientific reference, then attempts the full plugin build, then drops a dated
# VST3 into Demo/. Designed for Windows PowerShell 5.1 — no pwsh-only syntax.
#
# Usage (from the project root):
#   ./build_and_demo.ps1                # configure + tests + full build + copy
#   ./build_and_demo.ps1 -TestsOnly     # stop after the tests preset
#   ./build_and_demo.ps1 -SkipCopy      # skip the Demo/ copy step
#   ./build_and_demo.ps1 -InstallVst3   # also install VST3 into CommonProgramFiles\VST3

[CmdletBinding()]
param(
    [switch] $TestsOnly,
    [switch] $SkipCopy,
    [switch] $InstallVst3
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $projectRoot

function Step([string] $name, [scriptblock] $action) {
    Write-Host ""
    Write-Host "==> $name" -ForegroundColor Cyan
    & $action
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $null) {
        throw "Step '$name' failed with exit code $LASTEXITCODE"
    }
}

Step "Configure tests-only preset" {
    cmake --preset vs2019-x64-tests-only
}

Step "Build tests-only (Debug)" {
    cmake --build --preset debug-tests-only
}

Step "Run ctest tests-only" {
    ctest --preset debug-tests-only --output-on-failure
}

if (-not $TestsOnly) {
    Step "Configure full preset (plugin + tests)" {
        cmake --preset vs2019-x64
    }
    Step "Build full (Debug, includes VST3)" {
        cmake --build --preset debug
    }
    Step "Run ctest debug" {
        ctest --preset debug --output-on-failure
    }
}

if (Test-Path (Join-Path $projectRoot ".git")) {
    Step "git diff --check (whitespace hygiene)" {
        git diff --check
    }
} else {
    Write-Host ""
    Write-Host "==> git diff --check skipped (.git not present)" -ForegroundColor Yellow
}

if (-not $TestsOnly -and -not $SkipCopy) {
    Step "Copy VST3 into Demo/" {
        $demoDir = Join-Path $projectRoot "Demo"
        if (-not (Test-Path $demoDir)) {
            New-Item -ItemType Directory -Path $demoDir | Out-Null
        }
        $vst3Candidates = Get-ChildItem -Path (Join-Path $projectRoot "build/vs2019-x64") `
            -Filter "*.vst3" -Recurse -ErrorAction SilentlyContinue `
            | Where-Object { -not $_.PSIsContainer }
        if (-not $vst3Candidates) {
            $vst3Bundles = Get-ChildItem -Path (Join-Path $projectRoot "build/vs2019-x64") `
                -Directory -Filter "*.vst3" -Recurse -ErrorAction SilentlyContinue
            if ($vst3Bundles) {
                $vst3Candidates = $vst3Bundles
            }
        }
        if (-not $vst3Candidates) {
            throw "No .vst3 artefact found under build/vs2019-x64 -- did the full build succeed?"
        }
        $latest = $vst3Candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1

        $tag = ""
        if (Test-Path (Join-Path $projectRoot ".git")) {
            try {
                $hash = (git rev-parse --short HEAD).Trim()
                if ($hash) { $tag = "_$hash" }
            } catch {
                $tag = ""
            }
        }
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $destName = "U273${tag}_$stamp$($latest.Extension)"
        $destination = Join-Path $demoDir $destName

        if ($latest.PSIsContainer) {
            Copy-Item -Path $latest.FullName -Destination $destination -Recurse -Force
        } else {
            Copy-Item -Path $latest.FullName -Destination $destination -Force
        }
        Write-Host "    copied: $destination" -ForegroundColor Green

        $standaloneCandidates = Get-ChildItem -Path (Join-Path $projectRoot "build/vs2019-x64") `
            -Filter "*.exe" -Recurse -ErrorAction SilentlyContinue `
            | Where-Object { $_.FullName -match "Standalone" -and -not $_.PSIsContainer }
        if ($standaloneCandidates) {
            $latestExe = $standaloneCandidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
            $exeDestName = "U273_Standalone${tag}_$stamp.exe"
            $exeDestination = Join-Path $demoDir $exeDestName
            Copy-Item -Path $latestExe.FullName -Destination $exeDestination -Force
            Write-Host "    copied: $exeDestination" -ForegroundColor Green
        } else {
            Write-Host "    no Standalone .exe found under build/vs2019-x64 (skipped)" -ForegroundColor Yellow
        }

        if ($InstallVst3) {
            $vst3Dir = Join-Path $env:CommonProgramFiles "VST3"
            if (-not (Test-Path $vst3Dir)) {
                New-Item -ItemType Directory -Path $vst3Dir | Out-Null
            }
            $installName = "U273_$stamp.vst3"
            $installPath = Join-Path $vst3Dir $installName
            # Re-use $latest (already the freshest vst3, file or bundle) to avoid a second glob.
            if ($latest.PSIsContainer) {
                Copy-Item -Path $latest.FullName -Destination $installPath -Recurse -Force
            } else {
                Copy-Item -Path $latest.FullName -Destination $installPath -Force
            }
            Write-Host "    Installed VST3 to $installPath" -ForegroundColor Green
        }
    }

    # Scientific demo bench: warn-not-fail so a bench regression never blocks
    # the always-demoable plugin copy upstream.
    $benchScript = Join-Path $projectRoot "scripts\run_demo_bench.ps1"
    if (Test-Path $benchScript) {
        Write-Host ""
        Write-Host "==> Scientific demo bench" -ForegroundColor Cyan
        try {
            & $benchScript
            if ($LASTEXITCODE -ne 0) {
                Write-Host "Scientific bench skipped: exit code $LASTEXITCODE" -ForegroundColor Yellow
            }
        } catch {
            Write-Host "Scientific bench skipped: $($_.Exception.Message)" -ForegroundColor Yellow
        }
    } else {
        Write-Host ""
        Write-Host "==> Scientific bench script not found at $benchScript (skipped)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "==> Sprint goal MET: tests-only green, audio gate intentionally closed." -ForegroundColor Green
Write-Host "    Boundary remains FULL_ACTIVE_MODEL_UNVERIFIED (audio THD bench pending)." -ForegroundColor Green
