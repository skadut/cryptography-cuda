# gpuseal

GPU-accelerated AES-256-GCM bulk encryption service with full throughput, VRAM,
and power instrumentation, plus an honest multi-threaded CPU baseline comparison.

The deliverable is a **credible benchmark study**, not a shipping product. The
expected headline finding is that PCIe transfer, not AES arithmetic, sets the
ceiling for host-to-host workloads — the kernel is roughly 4.6x faster than the
bus can feed it.

## Status

**Phase 1 of 7. Code is written; nothing has ever been built or run.**

The authoring machine has no NVIDIA GPU, no CUDA toolkit, and no CMake. Every
documented command is `declared`, never `verified`. All gate results are unknown
until someone runs them on real hardware.

## Start here

| If you want to | Read |
|---|---|
| Continue the build | [`docs/phases/README.md`](docs/phases/README.md) — index, current state, and the rules that survive across sessions |
| Understand the design | [`cuda-encryption-module-spec.md`](cuda-encryption-module-spec.md) — the source specification and final authority |
| Run Phase 1 on a GPU box | [`docs/phases/phase1-gpu-verification.md`](docs/phases/phase1-gpu-verification.md) |

## Build

```bash
scripts/check_prereqs.sh --require-gpu     # Linux/macOS
scripts\check_prereqs.ps1 -RequireGpu      # Windows

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --build build --target verify-phase1
./build/bench_cpu baseline_results.csv
```

Requires OpenSSL 3.x. NVML and NVTX3 are optional but their absence downgrades
telemetry to a stub — watch the CMake configure output for `NVML found` and
`NVTX3 found`, and do not record results from a build missing either.

## Extended documentation

Architecture notes, the VRAM budget model, PCIe ceiling analysis, and the threat
model are maintained in an Obsidian vault on the author's machine:

```
D:/redwine-obsidian-local/Wiki/Projects/CUDA Processing Cryptography/
```

That path is local and not reachable from anywhere else. Nothing in the build
depends on it — the spec and the phase docs in this repository are self-contained.
