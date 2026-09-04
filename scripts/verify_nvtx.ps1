# Phase 1 gate P1.5: NVTX range push/pop appears in an nsys trace.
#
# ASCII only in this file on purpose: non-ASCII punctuation gets mangled by
# legacy console codepages and breaks the parser.
#
# Usage: scripts\verify_nvtx.ps1 [-Probe build\Release\nvtx_probe.exe]
# Exit:  0 pass, 1 fail, 77 skip (no nsys, or binary built without NVTX)

param(
    [string]$Probe = "",
    [string]$OutDir = "nvtx_p15"
)

$SKIP = 77

if (-not $Probe) {
    $candidates = @(
        "build\Release\nvtx_probe.exe",
        "build\nvtx_probe.exe",
        "build\Debug\nvtx_probe.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $Probe = $c; break }
    }
}

if (-not $Probe -or -not (Test-Path $Probe)) {
    Write-Host "SKIP: probe not found (build it first)"
    exit $SKIP
}

$nsys = Get-Command nsys -ErrorAction SilentlyContinue
if (-not $nsys) {
    Write-Host "SKIP: nsys not on PATH; install Nsight Systems to verify P1.5"
    exit $SKIP
}

# The probe self-reports when NVTX was compiled out. Honor that before profiling.
$probeOut = & $Probe 2>&1
if ($probeOut -match '^SKIP:') {
    Write-Host "SKIP: probe built without NVTX support"
    exit $SKIP
}

Remove-Item "$OutDir.nsys-rep","$OutDir.sqlite" -ErrorAction SilentlyContinue

Write-Host "profiling $Probe with nsys..."
& nsys profile --trace=nvtx --force-overwrite=true -o $OutDir $Probe | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAIL P1.5: nsys profile returned $LASTEXITCODE"
    exit 1
}

$report = "$OutDir.nsys-rep"
if (-not (Test-Path $report)) {
    Write-Host "FAIL P1.5: nsys produced no report at $report"
    exit 1
}

# nvtx_pushpop_trace lists each range by name. Older nsys spells it nvtxppsum.
$trace = & nsys stats --report nvtx_pushpop_trace --format csv $report 2>$null
if (-not $trace) {
    $trace = & nsys stats --report nvtxppsum --format csv $report 2>$null
}

if (-not $trace) {
    Write-Host "FAIL P1.5: nsys stats produced no NVTX trace output"
    exit 1
}

$traceText = $trace -join "`n"
$fail = $false
foreach ($name in @("gpuseal_p15_outer", "gpuseal_p15_inner", "gpuseal_p15_sibling")) {
    if ($traceText -match [regex]::Escape($name)) {
        Write-Host "  found range: $name"
    } else {
        Write-Host "FAIL P1.5: range '$name' missing from trace"
        $fail = $true
    }
}

if ($fail) {
    Write-Host ""
    Write-Host "Trace contents follow, for diagnosis:"
    Write-Host $traceText
    exit 1
}

Write-Host "PASS P1.5: all three NVTX ranges present in $report"
exit 0
