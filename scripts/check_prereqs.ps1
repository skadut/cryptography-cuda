<#
.SYNOPSIS
    gpuseal prerequisites check (Windows / PowerShell).
.DESCRIPTION
    Verifies the toolchain needed to build and test gpuseal.
    Exit 0 = all required present. Exit 1 = at least one required item missing.
    GPU items are reported but only fail the check when -RequireGpu is set.
.EXAMPLE
    .\check_prereqs.ps1
    .\check_prereqs.ps1 -RequireGpu
    .\check_prereqs.ps1 -Json
#>
param(
    [switch]$RequireGpu,
    [switch]$Json
)

# ASCII only in this file on purpose: non-ASCII punctuation gets mangled by
# legacy console codepages and breaks the parser.

$ErrorActionPreference = 'Continue'
$results = @()

function Add-Check {
    param($Name, $Status, $Detail, $Required, $Category)
    $script:results += [pscustomobject]@{
        Name = $Name; Status = $Status; Detail = $Detail
        Required = $Required; Category = $Category
    }
}

function Get-ToolInfo {
    param($Exe, $VersionArgs)
    $cmd = Get-Command $Exe -ErrorAction SilentlyContinue
    if (-not $cmd) { return $null }
    try {
        $out = & $cmd.Source @VersionArgs 2>&1 | Select-Object -First 3
        return @{ Path = $cmd.Source; Output = ($out -join ' ').Trim() }
    } catch {
        return @{ Path = $cmd.Source; Output = '(version query failed)' }
    }
}

function Get-GpuStatus {
    if ($RequireGpu) { return 'FAIL' } else { return 'SKIP' }
}

# --- Build toolchain (required) -------------------------------------------

$cmake = Get-ToolInfo 'cmake' @('--version')
if ($cmake) {
    $v = if ($cmake.Output -match '(\d+\.\d+\.\d+)') { $Matches[1] } else { $null }
    if ($v -and [version]$v -ge [version]'3.20.0') {
        Add-Check 'CMake' 'PASS' "$v at $($cmake.Path)" $true 'Build'
    } else {
        Add-Check 'CMake' 'WARN' "version $v found, need >= 3.20" $true 'Build'
    }
} else {
    Add-Check 'CMake' 'FAIL' 'not on PATH. Get it: https://cmake.org/download/' $true 'Build'
}

$openssl = Get-ToolInfo 'openssl' @('version')
if ($openssl) {
    $v = if ($openssl.Output -match 'OpenSSL\s+(\d+\.\d+\.\d+)') { $Matches[1] } else { $null }
    if ($v -and [version]$v -ge [version]'3.0.0') {
        Add-Check 'OpenSSL' 'PASS' "$v at $($openssl.Path)" $true 'Build'
    } else {
        Add-Check 'OpenSSL' 'WARN' "version $v found, need >= 3.0 for the EVP baseline" $true 'Build'
    }
} else {
    Add-Check 'OpenSSL' 'FAIL' 'not found. Required for CPU baseline and the G2 oracle.' $true 'Build'
}

$compiler = $null
foreach ($c in @('cl', 'gcc', 'clang')) {
    if (Get-Command $c -ErrorAction SilentlyContinue) { $compiler = $c; break }
}
if ($compiler) {
    Add-Check 'C compiler' 'PASS' "$compiler on PATH" $true 'Build'
} else {
    $vsDirs = @(
        'C:\Program Files\Microsoft Visual Studio',
        'C:\Program Files (x86)\Microsoft Visual Studio'
    )
    if ($vsDirs | Where-Object { Test-Path $_ }) {
        Add-Check 'C compiler' 'WARN' 'Visual Studio found but cl.exe not on PATH. Use a Developer Command Prompt.' $true 'Build'
    } else {
        Add-Check 'C compiler' 'FAIL' 'no cl.exe / gcc / clang. Install VS Build Tools with the C++ workload.' $true 'Build'
    }
}

$git = Get-ToolInfo 'git' @('--version')
if ($git) {
    Add-Check 'Git' 'PASS' $git.Output $true 'Build'
} else {
    Add-Check 'Git' 'FAIL' 'not found' $true 'Build'
}

# --- Analysis (required) ---------------------------------------------------

$python = Get-ToolInfo 'python' @('--version')
if (-not $python) { $python = Get-ToolInfo 'python3' @('--version') }
if ($python) {
    $v = if ($python.Output -match '(\d+\.\d+)') { $Matches[1] } else { $null }
    if ($v -and [version]$v -ge [version]'3.9') {
        Add-Check 'Python' 'PASS' "$v at $($python.Path)" $true 'Analysis'
    } else {
        Add-Check 'Python' 'WARN' "version $v found, need >= 3.9" $true 'Analysis'
    }
} else {
    Add-Check 'Python' 'FAIL' 'not found. Required for the D6 analysis notebook.' $true 'Analysis'
}

# --- CUDA (GPU dependent) --------------------------------------------------

$nvcc = Get-ToolInfo 'nvcc' @('--version')
if ($nvcc) {
    $v = if ($nvcc.Output -match 'release (\d+\.\d+)') { $Matches[1] } else { $null }
    if ($v -and [version]$v -ge [version]'11.0') {
        Add-Check 'CUDA nvcc' 'PASS' "$v at $($nvcc.Path)" $RequireGpu 'CUDA'
    } else {
        Add-Check 'CUDA nvcc' 'WARN' "version $v found, need >= 11.0" $RequireGpu 'CUDA'
    }
} else {
    $cudaDir = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA'
    if (Test-Path $cudaDir) {
        $detail = 'toolkit dir exists but nvcc not on PATH. Add the version bin dir under: ' + $cudaDir
    } else {
        $detail = 'not installed. Get it: https://developer.nvidia.com/cuda-downloads'
    }
    Add-Check 'CUDA nvcc' (Get-GpuStatus) $detail $RequireGpu 'CUDA'
}

$smi = Get-Command 'nvidia-smi' -ErrorAction SilentlyContinue
if (-not $smi -and (Test-Path 'C:\Windows\System32\nvidia-smi.exe')) {
    $smi = [pscustomobject]@{ Source = 'C:\Windows\System32\nvidia-smi.exe' }
}
if ($smi) {
    try {
        $gpu = & $smi.Source --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>&1 | Select-Object -First 1
        Add-Check 'NVIDIA driver' 'PASS' "$gpu" $RequireGpu 'CUDA'
    } catch {
        Add-Check 'NVIDIA driver' 'WARN' 'nvidia-smi present but the query failed' $RequireGpu 'CUDA'
    }
} else {
    Add-Check 'NVIDIA driver' (Get-GpuStatus) 'nvidia-smi not found, so no NVIDIA driver is installed' $RequireGpu 'CUDA'
}

$nvmlPaths = @(
    'C:\Windows\System32\nvml.dll',
    'C:\Program Files\NVIDIA Corporation\NVSMI\nvml.dll'
)
$nvml = $nvmlPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($nvml) {
    Add-Check 'NVML library' 'PASS' $nvml $RequireGpu 'CUDA'
} else {
    Add-Check 'NVML library' (Get-GpuStatus) 'nvml.dll not found. Telemetry layer will build in stub mode.' $RequireGpu 'CUDA'
}

# --- Hardware --------------------------------------------------------------

try {
    $gpus = @(Get-CimInstance Win32_VideoController -ErrorAction Stop | Select-Object -ExpandProperty Name)
    $nvidiaGpu = @($gpus | Where-Object { $_ -match 'NVIDIA|GeForce|RTX|Quadro|Tesla' })
    if ($nvidiaGpu.Count -gt 0) {
        Add-Check 'NVIDIA GPU' 'PASS' ($nvidiaGpu -join '; ') $RequireGpu 'Hardware'
    } else {
        Add-Check 'NVIDIA GPU' (Get-GpuStatus) ('no NVIDIA GPU detected. Found: ' + ($gpus -join '; ')) $RequireGpu 'Hardware'
    }
} catch {
    Add-Check 'NVIDIA GPU' 'WARN' 'GPU query failed' $RequireGpu 'Hardware'
}

try {
    $cpu = Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1
    Add-Check 'CPU' 'PASS' ("{0} - {1}C/{2}T" -f $cpu.Name.Trim(), $cpu.NumberOfCores, $cpu.NumberOfLogicalProcessors) $false 'Hardware'
    Add-Check 'AES-NI / VAES' 'INFO' 'run: openssl speed -evp aes-256-gcm  (record whether the VAES path is active)' $false 'Hardware'
} catch {
    Add-Check 'CPU' 'WARN' 'CPU query failed' $false 'Hardware'
}

# --- Profiling (optional) --------------------------------------------------

foreach ($t in @(
    @('nsys', 'Nsight Systems'),
    @('ncu', 'Nsight Compute'),
    @('compute-sanitizer', 'compute-sanitizer')
)) {
    $found = Get-Command $t[0] -ErrorAction SilentlyContinue
    if ($found) {
        Add-Check $t[1] 'PASS' $found.Source $false 'Profiling'
    } else {
        Add-Check $t[1] 'SKIP' ($t[0] + ' not on PATH. Needed for phases 4-6 only.') $false 'Profiling'
    }
}

# --- Report ----------------------------------------------------------------

if ($Json) {
    $results | ConvertTo-Json -Depth 3
} else {
    Write-Host ''
    Write-Host 'gpuseal prerequisites check' -ForegroundColor Cyan
    Write-Host ('=' * 78)
    $lastCat = ''
    foreach ($r in $results) {
        if ($r.Category -ne $lastCat) {
            Write-Host ''
            Write-Host ('[' + $r.Category + ']') -ForegroundColor DarkCyan
            $lastCat = $r.Category
        }
        $color = switch ($r.Status) {
            'PASS' { 'Green' }
            'FAIL' { 'Red' }
            'WARN' { 'Yellow' }
            'SKIP' { 'DarkGray' }
            default { 'Gray' }
        }
        $req = if ($r.Required) { '*' } else { ' ' }
        Write-Host ("  {0,-6}{1} {2,-20} {3}" -f $r.Status, $req, $r.Name, $r.Detail) -ForegroundColor $color
    }
    Write-Host ''
    Write-Host ('=' * 78)
    Write-Host '  * = required for this configuration' -ForegroundColor DarkGray
}

$failures = @($results | Where-Object { $_.Status -eq 'FAIL' })
$skipped = @($results | Where-Object { $_.Status -eq 'SKIP' })

if (-not $Json) {
    if ($failures.Count -gt 0) {
        Write-Host ''
        Write-Host ("FAILED: {0} required item(s) missing:" -f $failures.Count) -ForegroundColor Red
        foreach ($f in $failures) {
            Write-Host ("  - {0}: {1}" -f $f.Name, $f.Detail) -ForegroundColor Red
        }
    } else {
        Write-Host ''
        Write-Host 'All required prerequisites present.' -ForegroundColor Green
        if ($skipped.Count -gt 0 -and -not $RequireGpu) {
            Write-Host ("  {0} GPU/profiling item(s) skipped. Phase 1 and the CPU baseline can proceed." -f $skipped.Count) -ForegroundColor Yellow
            Write-Host '  Re-run with -RequireGpu on GPU hardware before starting phase 2.' -ForegroundColor Yellow
        }
    }
}

if ($failures.Count -gt 0) { exit 1 } else { exit 0 }
