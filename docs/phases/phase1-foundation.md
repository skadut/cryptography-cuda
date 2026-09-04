# Phase 1 — Foundation

**Exit condition:** Baseline OpenSSL AES-256-GCM numbers recorded on target hardware.

## Scope

1. **Project structure** — CMake build system supporting CUDA + C + OpenSSL linkage
2. **NVML/NVTX telemetry skeleton** — reusable sampler that collects GPU metrics at ≥10 Hz
3. **CPU baseline harness** — OpenSSL 3.x EVP AES-256-GCM, multi-threaded across all cores
4. **NIST CAVP vector loader** — parse and run known-answer test vectors (prepares G1)

## Directory layout

```
cryptography-cuda/
├── CMakeLists.txt
├── include/gpuseal/
│   ├── telemetry.h        # NVML sampler + NVTX wrapper API
│   ├── cpu_baseline.h     # OpenSSL EVP harness API
│   ├── test_vectors.h     # CAVP vector loader API
│   └── common.h           # Shared types, error macros
├── src/
│   ├── telemetry.c        # NVML polling loop, CSV emitter
│   ├── cpu_baseline.c     # OpenSSL EVP multi-threaded harness
│   └── test_vectors.c     # NIST CAVP parser
├── tests/
│   ├── test_cpu_baseline.c
│   └── vectors/           # NIST CAVP .rsp files
├── bench/
│   └── bench_cpu.c        # CPU baseline benchmark entry point
├── python/
│   └── telemetry.py       # pynvml sampler (reference)
└── docs/phases/
```

## Telemetry skeleton

Wraps NVML C API. Samples at configurable rate (default 10 Hz):

| Metric | NVML call |
|---|---|
| GPU utilization | `nvmlDeviceGetUtilizationRates` |
| Memory used/total | `nvmlDeviceGetMemoryInfo` |
| Power draw | `nvmlDeviceGetPowerUsage` |
| SM clock | `nvmlDeviceGetClockInfo` |
| Temperature | `nvmlDeviceGetTemperature` |
| PCIe throughput | `nvmlDeviceGetPcieThroughput` |

Output: tidy CSV keyed by run ID, with columns: `timestamp,run_id,gpu_util,mem_used,mem_total,power_w,sm_clock,temp_c,pcie_tx_mbps,pcie_rx_mbps`

NVTX range helpers: `gpuseal_nvtx_push(name)` / `gpuseal_nvtx_pop()` for phase annotation in Nsight timeline.

## CPU baseline harness

- Uses OpenSSL EVP API (`EVP_EncryptInit_ex` with `EVP_aes_256_gcm()`)
- Multi-threaded: spawns N threads (configurable, default = all cores), each encrypts its share
- Measures wall-clock time for full encrypt (host buffer → host buffer)
- Records: cores used, whether VAES path active (via `openssl speed` output)
- Reports throughput in GB/s labeled **end-to-end**

Payload sizes: 1 KB to 8 GB sweep (same as §7 matrix).

## NIST CAVP vector loader

Parses NIST `.rsp` files for AES-GCM (key, IV, plaintext, AAD, ciphertext, tag).
Runs each vector through OpenSSL EVP to confirm the loader works.
This prepares the G1 gate for Phase 2 — the same vectors will test the CUDA kernel.

## What this phase does NOT include

- No CUDA kernels
- No GPU memory allocation
- No encryption implementation (beyond OpenSSL reference)
- No optimization

## Build and run (declared — not yet executed on any machine)

```bash
scripts/check_prereqs.sh          # or scripts\check_prereqs.ps1 on Windows
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -L phase1 --output-on-failure
./build/bench_cpu baseline_results.csv
```

CUDA is off in Phase 1 (`GPUSEAL_ENABLE_CUDA=OFF`). Without NVML the telemetry
layer compiles as a stub and `test_telemetry` exits 77, which CTest reports as
skipped rather than failed — so a GPU-less workstation can still clear the
CPU-side gates. P1.3–P1.5 remain unverified until the suite runs on GPU hardware.

---

## Phase Gate — Verification before Phase 2

Run `ctest -L phase1` (or `make verify-phase1`). All must pass:

| # | Test | Verifies |
|---|---|---|
| P1.1 | `check_prereqs` exits 0 | Toolchain present (CUDA, OpenSSL, CMake, compiler) |
| P1.2 | Build produces `bench_cpu` and `libgpuseal_telemetry` with zero warnings | CMake wiring correct |
| P1.3 | Telemetry sampler runs 5 s, emits ≥ 45 CSV rows at 10 Hz | NVML polling works at target rate |
| P1.4 | Telemetry CSV has all 10 required columns, no NaN in `power_w`/`mem_used` | Metric collection complete |
| P1.5 | NVTX range push/pop appears in `nsys` trace | Timeline annotation works |
| P1.6 | CPU baseline encrypts 1 GB, output matches OpenSSL `enc` CLI byte-for-byte | Harness is a correct oracle |
| P1.7 | CPU baseline scales: 8-thread throughput ≥ 4× 1-thread | Multi-threading actually parallel |
| P1.8 | CAVP loader parses reference `.rsp`, vector count matches file header | Vector loader correct |
| P1.9 | Every CAVP vector passes through OpenSSL EVP | Loader + reference agree |
| P1.10 | Baseline results CSV written with cores, clock, VAES-active flag | Exit condition artifact exists |

**Exit gate:** P1.1–P1.10 green AND `baseline_results.csv` committed. Do not start Phase 2 otherwise.
