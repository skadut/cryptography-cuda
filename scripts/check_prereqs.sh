#!/usr/bin/env bash
# gpuseal prerequisites check (Linux / macOS / Git Bash).
# Exit 0 = all required present. Exit 1 = at least one required item missing.
# GPU items are reported but only fail when REQUIRE_GPU=1.
#
# ASCII only in this file on purpose, to match check_prereqs.ps1.

set -uo pipefail

REQUIRE_GPU="${REQUIRE_GPU:-0}"
[[ "${1:-}" == "--require-gpu" ]] && REQUIRE_GPU=1

RED=$'\033[31m'; GRN=$'\033[32m'; YEL=$'\033[33m'; CYN=$'\033[36m'; GRY=$'\033[90m'; RST=$'\033[0m'
FAILURES=0
SKIPPED=0

hdr() { printf '\n%s[%s]%s\n' "$CYN" "$1" "$RST"; }

check() {  # check <status> <required 0|1> <name> <detail>
    local status="$1" required="$2" name="$3" detail="$4" color mark
    case "$status" in
        PASS) color="$GRN" ;; FAIL) color="$RED" ;; WARN) color="$YEL" ;;
        SKIP) color="$GRY" ;; *) color="$RST" ;;
    esac
    mark=" "; [[ "$required" == "1" ]] && mark="*"
    printf '  %s%-6s%s %-20s %s%s\n' "$color" "$status" "$mark" "$name" "$detail" "$RST"
    [[ "$status" == "FAIL" ]] && FAILURES=$((FAILURES + 1))
    [[ "$status" == "SKIP" ]] && SKIPPED=$((SKIPPED + 1))
    return 0
}

ver_ge() {  # ver_ge <have> <want>  -> 0 if have >= want
    [[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" == "$2" ]]
}

printf '\n%sgpuseal prerequisites check%s\n' "$CYN" "$RST"
printf '==============================================================================\n'

# --- Build toolchain (required) ------------------------------------------
hdr Build

if command -v cmake >/dev/null 2>&1; then
    v=$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    if ver_ge "$v" "3.20.0"; then check PASS 1 CMake "$v at $(command -v cmake)"
    else check WARN 1 CMake "$v found, need >= 3.20"; fi
else
    check FAIL 1 CMake "not found. Get it: https://cmake.org/download/"
fi

if command -v openssl >/dev/null 2>&1; then
    v=$(openssl version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    if ver_ge "$v" "3.0.0"; then check PASS 1 OpenSSL "$v at $(command -v openssl)"
    else check WARN 1 OpenSSL "$v found, need >= 3.0 for the EVP baseline"; fi
else
    check FAIL 1 OpenSSL "not found. Required for CPU baseline and the G2 oracle."
fi

if pkg-config --exists libcrypto 2>/dev/null; then
    check PASS 1 "OpenSSL headers" "$(pkg-config --modversion libcrypto)"
elif [[ -f /usr/include/openssl/evp.h ]] || [[ -f /usr/local/include/openssl/evp.h ]]; then
    check PASS 1 "OpenSSL headers" "found in system include path"
else
    check WARN 1 "OpenSSL headers" "evp.h not located. Install libssl-dev / openssl-devel."
fi

CC_FOUND=""
for c in cc gcc clang; do
    if command -v "$c" >/dev/null 2>&1; then CC_FOUND="$c"; break; fi
done
if [[ -n "$CC_FOUND" ]]; then
    check PASS 1 "C compiler" "$CC_FOUND: $($CC_FOUND --version 2>&1 | head -1)"
else
    check FAIL 1 "C compiler" "no cc/gcc/clang. Install build-essential."
fi

if command -v git >/dev/null 2>&1; then
    check PASS 1 Git "$(git --version)"
else
    check FAIL 1 Git "not found"
fi

# --- Analysis (required) --------------------------------------------------
hdr Analysis

PY=""
for p in python3 python; do
    if command -v "$p" >/dev/null 2>&1; then PY="$p"; break; fi
done
if [[ -n "$PY" ]]; then
    v=$($PY --version 2>&1 | grep -oE '[0-9]+\.[0-9]+' | head -1)
    if ver_ge "$v" "3.9"; then check PASS 1 Python "$v at $(command -v $PY)"
    else check WARN 1 Python "$v found, need >= 3.9"; fi
else
    check FAIL 1 Python "not found. Required for the D6 analysis notebook."
fi

# --- CUDA (GPU-dependent) -------------------------------------------------
hdr CUDA

gpu_status() { [[ "$REQUIRE_GPU" == "1" ]] && echo FAIL || echo SKIP; }

if command -v nvcc >/dev/null 2>&1; then
    v=$(nvcc --version | grep -oE 'release [0-9]+\.[0-9]+' | grep -oE '[0-9]+\.[0-9]+')
    if ver_ge "$v" "11.0"; then check PASS "$REQUIRE_GPU" "CUDA nvcc" "$v at $(command -v nvcc)"
    else check WARN "$REQUIRE_GPU" "CUDA nvcc" "$v found, need >= 11.0"; fi
else
    if [[ -d /usr/local/cuda ]]; then
        check "$(gpu_status)" "$REQUIRE_GPU" "CUDA nvcc" "/usr/local/cuda exists but nvcc not on PATH. Add /usr/local/cuda/bin."
    else
        check "$(gpu_status)" "$REQUIRE_GPU" "CUDA nvcc" "not installed. Get it: https://developer.nvidia.com/cuda-downloads"
    fi
fi

if command -v nvidia-smi >/dev/null 2>&1; then
    gpu=$(nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>/dev/null | head -1)
    if [[ -n "$gpu" ]]; then check PASS "$REQUIRE_GPU" "NVIDIA driver" "$gpu"
    else check WARN "$REQUIRE_GPU" "NVIDIA driver" "nvidia-smi present but the query returned nothing"; fi
else
    check "$(gpu_status)" "$REQUIRE_GPU" "NVIDIA driver" "nvidia-smi not found"
fi

NVML=""
for p in /usr/lib/x86_64-linux-gnu/libnvidia-ml.so /usr/lib64/libnvidia-ml.so /usr/local/cuda/lib64/stubs/libnvidia-ml.so; do
    [[ -f "$p" ]] && { NVML="$p"; break; }
done
if [[ -n "$NVML" ]]; then
    check PASS "$REQUIRE_GPU" "NVML library" "$NVML"
else
    check "$(gpu_status)" "$REQUIRE_GPU" "NVML library" "libnvidia-ml.so not found. Telemetry builds in stub mode."
fi

# --- Profiling (optional) -------------------------------------------------
hdr Profiling

for pair in "nsys:Nsight Systems" "ncu:Nsight Compute" "compute-sanitizer:compute-sanitizer"; do
    bin="${pair%%:*}"; label="${pair#*:}"
    if command -v "$bin" >/dev/null 2>&1; then
        check PASS 0 "$label" "$(command -v $bin)"
    else
        check SKIP 0 "$label" "$bin not on PATH. Needed for phases 4-6 only."
    fi
done

# --- Hardware -------------------------------------------------------------
hdr Hardware

if [[ -f /proc/cpuinfo ]]; then
    model=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ *//')
    cores=$(grep -c ^processor /proc/cpuinfo)
    check PASS 0 CPU "$model, ${cores} logical"
    flags=$(grep -m1 ^flags /proc/cpuinfo)
    if grep -q ' vaes' <<< "$flags"; then check PASS 0 "VAES" "present: strong CPU baseline (section 3 requirement)"
    elif grep -q ' aes' <<< "$flags"; then check WARN 0 "AES-NI" "AES-NI present, VAES absent. Baseline weaker than spec prefers."
    else check WARN 0 "AES-NI" "no AES-NI. CPU baseline would be unfairly slow, violating section 3."; fi
    if grep -q ' pclmulqdq' <<< "$flags"; then check PASS 0 "PCLMULQDQ" "present: needed for fast GHASH on CPU"
    else check WARN 0 "PCLMULQDQ" "absent. CPU GHASH will be slow."; fi
else
    check WARN 0 CPU "cannot read /proc/cpuinfo on this platform"
fi

# --- Summary --------------------------------------------------------------
printf '\n==============================================================================\n'
printf '%s  * = required for this configuration%s\n' "$GRY" "$RST"

if (( FAILURES > 0 )); then
    printf '\n%sFAILED: %d required item(s) missing.%s\n' "$RED" "$FAILURES" "$RST"
    exit 1
fi

printf '\n%sAll required prerequisites present.%s\n' "$GRN" "$RST"
if (( SKIPPED > 0 )) && [[ "$REQUIRE_GPU" != "1" ]]; then
    printf '%s  (%d GPU/profiling item(s) skipped. Phase 1 and the CPU baseline can proceed;%s\n' "$YEL" "$SKIPPED" "$RST"
    printf '%s   re-run with --require-gpu on GPU hardware before phase 2.)%s\n' "$YEL" "$RST"
fi
exit 0
