#!/usr/bin/env bash
# Phase 1 gate P1.5: NVTX range push/pop appears in an nsys trace.
#
# ASCII only in this file on purpose: non-ASCII punctuation gets mangled by
# legacy console codepages.
#
# Usage: scripts/verify_nvtx.sh [path-to-nvtx_probe]
# Exit:  0 pass, 1 fail, 77 skip (no nsys, or binary built without NVTX)

set -u

PROBE="${1:-build/nvtx_probe}"
OUTDIR="${NVTX_OUTDIR:-nvtx_p15}"
SKIP=77

if [ ! -x "$PROBE" ]; then
    echo "SKIP: probe not found at $PROBE (build it first)"
    exit $SKIP
fi

if ! command -v nsys >/dev/null 2>&1; then
    echo "SKIP: nsys not on PATH; install Nsight Systems to verify P1.5"
    exit $SKIP
fi

# The probe self-reports when NVTX was compiled out. Honor that before profiling.
if "$PROBE" | grep -q '^SKIP:'; then
    echo "SKIP: probe built without NVTX support"
    exit $SKIP
fi

rm -f "${OUTDIR}.nsys-rep" "${OUTDIR}.sqlite"

echo "profiling $PROBE with nsys..."
if ! nsys profile --trace=nvtx --force-overwrite=true -o "$OUTDIR" "$PROBE" >/dev/null 2>&1; then
    echo "FAIL P1.5: nsys profile returned non-zero"
    exit 1
fi

REPORT="${OUTDIR}.nsys-rep"
if [ ! -f "$REPORT" ]; then
    echo "FAIL P1.5: nsys produced no report at $REPORT"
    exit 1
fi

# nvtx_pushpop_trace is the report that lists each range by name and duration.
TRACE="$(nsys stats --report nvtx_pushpop_trace --format csv "$REPORT" 2>/dev/null)"
if [ -z "$TRACE" ]; then
    # Older nsys spells it nvtxppsum. Fall back before declaring failure.
    TRACE="$(nsys stats --report nvtxppsum --format csv "$REPORT" 2>/dev/null)"
fi

if [ -z "$TRACE" ]; then
    echo "FAIL P1.5: nsys stats produced no NVTX trace output"
    exit 1
fi

fail=0
for name in gpuseal_p15_outer gpuseal_p15_inner gpuseal_p15_sibling; do
    if echo "$TRACE" | grep -q "$name"; then
        echo "  found range: $name"
    else
        echo "FAIL P1.5: range '$name' missing from trace"
        fail=1
    fi
done

if [ $fail -ne 0 ]; then
    echo
    echo "Trace contents follow, for diagnosis:"
    echo "$TRACE"
    exit 1
fi

echo "PASS P1.5: all three NVTX ranges present in $REPORT"
exit 0
