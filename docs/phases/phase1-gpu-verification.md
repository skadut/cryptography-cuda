# Phase 1 — Verification on NVIDIA CUDA Hardware

**Read this before recording any Phase 1 result.**

Phase 1 contains no CUDA kernels. It is still the phase where the project's
measurement instruments are proven, and four of the ten gates cannot be evaluated
on a machine without an NVIDIA GPU. This document says exactly what must run on
GPU hardware, how to tell a real reading from a stub, and what a `SKIP` actually
means.

The workstation this code was written on has no NVIDIA GPU, no CUDA toolkit, and
no CMake. **Nothing in this repository has ever been built or executed.** Every
command below is `declared`, not `verified`.

---

## Why a skip is not a pass

The test suite is built to degrade honestly. On a GPU-less host the GPU-dependent
tests exit `77`, which CTest reports as *skipped* — the run comes back green
without ever having tested the thing it was named for.

That is deliberate: a red suite on a machine that physically cannot run the test
tells you nothing. But green-with-skips is **not** a cleared gate.

```
Total Tests: 6
Passed:      3
Skipped:     3     <-- P1.3, P1.4, P1.5 NOT VERIFIED
```

The exit gate requires P1.1–P1.10 **passed**, not "passed or skipped".

| Gate | Needs an NVIDIA GPU? | Also needs |
|---|---|---|
| P1.1 prereqs | no (but reports GPU absence) | — |
| P1.2 clean build | no | CMake, C11 compiler |
| P1.3 sampler rate | **yes** | NVML (driver) |
| P1.4 metric completeness | **yes** | NVML (driver) |
| P1.5 NVTX in trace | **yes** | NVTX3 headers + Nsight Systems |
| P1.6 external oracle | no | `openssl` CLI |
| P1.7 thread scaling | no | >= 8 cores, OpenMP |
| P1.8 vector parse | no | — |
| P1.9 vectors vs EVP | no | — |
| P1.10 baseline CSV | no | OpenMP |

---

## Confirming the machine actually has CUDA resources

Do this before building. It takes a minute and it prevents recording a run whose
telemetry was silently stubbed out.

### 1. Driver and device are live

```bash
nvidia-smi
```

Expect a table naming the GPU, the driver version, and a CUDA version. Two
failure modes look similar and are not:

- `command not found` — no NVIDIA driver installed.
- `NVIDIA-SMI has failed because it couldn't communicate with the NVIDIA
  driver` — driver present but not loaded, or the GPU is claimed by another
  container/VM. Fix this before continuing; NVML will fail the same way.

Under WSL2, `nvidia-smi` works but NVML's power and PCIe counters are commonly
unavailable. If you are on WSL2, expect P1.4 to fail on `power_w`, and prefer
native Linux or Windows for the recorded run.

### 2. Toolkit and profiler are on PATH

```bash
nvcc --version     # CUDA toolkit; needed from phase 2, checked now to fail early
nsys --version     # Nsight Systems; required for P1.5
```

`nvcc` is not required to build Phase 1 (`GPUSEAL_ENABLE_CUDA=OFF`), but a
missing toolkit means Phase 2 stops immediately, and `nvcc` ships the NVTX3
headers that P1.5 needs.

### 3. The prerequisites script agrees

```bash
scripts/check_prereqs.sh --require-gpu        # Linux/macOS
scripts\check_prereqs.ps1 -RequireGpu         # Windows
```

`--require-gpu` / `-RequireGpu` turns "no GPU found" from a warning into a
failure. Use it on the benchmark machine; omit it on a laptop where you are only
editing code.

### 4. CMake actually found NVML and NVTX

This is the step people skip, and it is the one that decides whether your
telemetry numbers are real. Read the configure output:

```
-- gpuseal: NVML found, telemetry enabled
-- gpuseal: NVTX3 found, range annotation enabled
```

If instead you see:

```
-- gpuseal: NVML not found, telemetry builds in stub mode
-- gpuseal: NVTX3 not found, gpuseal_nvtx_* compile to no-ops and gate P1.5 will report SKIP
```

then the build is GPU-blind. `gpuseal_telemetry_start()` will return
`GPUSEAL_ERR_UNSUPPORTED`, `gpuseal_nvtx_push()` compiles to an empty function,
and both tests will skip. **Do not record results from such a build.** Point
CMake at the toolkit and reconfigure:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
# if NVML/NVTX were missed, name the toolkit explicitly:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DNVML_INCLUDE_DIR=/usr/local/cuda/include \
      -DNVML_LIBRARY=/usr/lib/x86_64-linux-gnu/libnvidia-ml.so \
      -DNVTX_INCLUDE_DIR=/usr/local/cuda/include
```

On Linux, link against the **driver's** `libnvidia-ml.so.1`, not the toolkit's
`stubs/libnvidia-ml.so`. The stub links but returns no data at runtime — the
build looks correct and every metric comes back zero. P1.4 is written to catch
exactly this.

---

## Full run on the GPU machine

```bash
scripts/check_prereqs.sh --require-gpu

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

cmake --build build --target verify-phase1
# equivalently: ctest --test-dir build -L phase1 --output-on-failure

./build/bench_cpu baseline_results.csv
```

`verify-phase1` runs all six registered tests. Expected on proper hardware:

```
    Start 1: prereqs .............. Passed
    Start 2: cpu_baseline ......... Passed
    Start 3: telemetry ............ Passed
    Start 4: nvtx_trace ........... Passed
    Start 5: openssl_cli_oracle ... Passed
    Start 6: thread_scaling ....... Passed
100% tests passed, 0 tests failed out of 6
```

Any `***Skipped` line means that gate is unverified. Resolve it or record it as
an explicit gap in the final report.

---

## What each GPU-dependent gate actually proves

### P1.3 / P1.4 — telemetry (`test_telemetry`)

Runs the sampler for 5 s at 10 Hz and requires >= 45 rows (10% slack for
scheduler jitter), then checks that `power_w` is not zero in every sample and
that `mem_total` is present in all of them.

The all-zeros check is the point. A stub NVML, a stub-linked driver library, or a
virtualized GPU all produce a well-formed CSV full of zeros. Structure alone
cannot distinguish a working sampler from a broken one; a nonzero power reading
can.

Artifact: `telemetry_p1.csv`.

```bash
./build/test_telemetry
head -3 telemetry_p1.csv
```

Sanity-check by eye: `power_w` should be a plausible idle draw for the card (tens
of watts, not 0 and not the board limit), `mem_total` should match `nvidia-smi`,
and `temp_c` should be near ambient on an idle GPU.

**Cross-check with the independent Python sampler**, which uses pynvml rather
than our C bindings:

```bash
python python/telemetry.py --seconds 5 --hz 10 --out telemetry_py.csv
```

It also exits 77 when pynvml or the driver is missing. Two independent samplers
reporting the same memory total and a similar power figure is good evidence the C
path is reading real hardware.

### P1.5 — NVTX ranges in a trace (`nvtx_trace`)

NVTX annotation is how every later phase reads its own timeline in Nsight. If the
ranges never reach the trace, phases 4–6 lose the tool used to attribute time
between transfer and compute — which is the entire PCIe-ceiling argument.

`tests/nvtx_probe.c` emits three ranges: `gpuseal_p15_outer`, a nested
`gpuseal_p15_inner`, and a sequential `gpuseal_p15_sibling`. The nesting proves
push/pop pairing survives depth; the sibling proves the stack unwound rather than
leaking depth.

The probe proves nothing when run bare — it must be profiled:

```bash
scripts/verify_nvtx.sh build/nvtx_probe        # Linux/macOS
scripts\verify_nvtx.ps1                        # Windows
```

The script runs `nsys profile --trace=nvtx`, then greps the
`nvtx_pushpop_trace` report for all three names. Manual equivalent:

```bash
nsys profile --trace=nvtx -o nvtx_p15 build/nvtx_probe
nsys stats --report nvtx_pushpop_trace nvtx_p15.nsys-rep
```

Common causes of a P1.5 failure:

- Built without NVTX3 headers → reports SKIP, not FAIL. Check the configure line.
- `nsys` present but lacking permission to profile. On Linux this needs
  `perf_event_paranoid <= 2` or the NVIDIA `ProfilingAdminOnly` setting relaxed;
  on Windows, run the shell as Administrator once to set it.
- Ranges present but unnamed in the report — an older `nsys` uses the
  `nvtxppsum` report name instead; the script already falls back to it.

---

## Gates that do not need a GPU but do need the right host

### P1.6 — external oracle (`openssl_cli_oracle`)

`test_cpu_baseline` compares our threaded harness against our own single-shot EVP
call. That catches sharding bugs, but not a shared misuse of EVP — both sides
would be wrong in the same direction and agree perfectly.

`test_openssl_cli` shells out to the `openssl enc` CLI as a **separate process**,
so a match means two independent code paths agree.

Default payload is 64 MiB so `ctest` stays quick. The gate table specifies 1 GiB
for the recorded run:

```bash
GPUSEAL_P16_BYTES=1073741824 ./build/test_openssl_cli          # Linux/macOS
$env:GPUSEAL_P16_BYTES=1073741824; .\build\Release\test_openssl_cli.exe
```

That needs roughly 3 GiB of free RAM and 2 GiB of scratch disk in the working
directory. Some OpenSSL builds refuse AEAD ciphers through `enc`; the test
detects this and skips rather than failing, since it is a property of the CLI
build and not of our code.

### P1.7 — thread scaling (`thread_scaling`)

Requires 8-thread throughput >= 4× 1-thread throughput.

This is the credibility gate. If the CPU baseline silently ran single-threaded,
every GPU speedup number in the final report would be inflated, and the study's
headline finding would be wrong in the flattering direction. A weak CPU baseline
is the most common flaw in published GPU-crypto results; acceptance criterion A7
exists to prevent it.

Skips on hosts with fewer than 8 cores — a skip means UNVERIFIED, and the
benchmark machine must have at least 8. Marked `RUN_SERIAL` so CTest does not
schedule other tests onto the cores being measured.

A failure here is usually one of: OpenMP not linked (look for the CMake warning
*"Do not record P1.10 results from this build"*), thermal/power throttling under
all-core load, or memory-bandwidth saturation at the 256 MiB working set.

---

## Recording the run

Phase 1's exit artifact is `baseline_results.csv` from `bench_cpu`. Alongside it,
record for reproducibility:

- GPU model, driver version, CUDA version (`nvidia-smi`)
- CPU model, core count, and whether AES-NI / VAES / PCLMULQDQ are present
  (the CSV carries these columns)
- OpenSSL version (also a CSV column)
- OS and kernel
- Which gates **skipped**, and why

Every throughput figure in that CSV is labeled `end-to-end` in its own column.
Acceptance criterion A6 requires that label to survive into the final report:
kernel-only numbers and end-to-end numbers are not comparable, and mixing them
unlabeled is how GPU crypto results get overstated.

---

## Exit gate

P1.1–P1.10 all **passed** — not skipped — and `baseline_results.csv` committed.
Do not start Phase 2 otherwise.

See [phase1-foundation.md](phase1-foundation.md) for the gate table and scope.
