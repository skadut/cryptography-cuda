# Build Phases — Index and Current State

`gpuseal` is built as a **7-phase sequence with verification gates**. Each phase
has a gate table (`P<n>.x`); every phase re-runs all prior phases' gates before
advancing. Do not reorder or skip phases.

---

## Current state

**Phase 1 code is written. Nothing in this repository has ever been built or
executed.**

The authoring workstation has no NVIDIA GPU, no CUDA toolkit, no CMake, and no
MSVC. Every command in these docs is `declared` — none is `verified`. Treat all
gate results as unknown until someone runs them on real hardware.

| Phase | Docs | Code | Gates run |
|---|---|---|---|
| 1 Foundation | yes | yes | **none — unverified** |
| 2 Naive AES-CTR | yes | no | — |
| 3 Full GCM | yes | no | — |
| 4 Pipeline | yes | no | — |
| 5 Kernel tuning | yes | no | — |
| 6 Benchmarks | yes | no | — |
| 7 Analysis + report | yes | no | — |

---

## Phase index

| # | Doc | Exit condition |
|---|---|---|
| 1 | [phase1-foundation.md](phase1-foundation.md) | Baseline OpenSSL AES-256-GCM numbers recorded on target hardware |
| 1 | [phase1-gpu-verification.md](phase1-gpu-verification.md) | *(companion)* how to run the GPU-dependent gates and prove telemetry is real |
| 2 | [phase2-naive-aes-ctr.md](phase2-naive-aes-ctr.md) | Correctness gates G1, G2 pass |
| 3 | [phase3-full-gcm.md](phase3-full-gcm.md) | Correctness gates G3–G7 pass — **protected phase** |
| 4 | [phase4-pipeline.md](phase4-pipeline.md) | Transfer/compute overlap >= 80% (A2) |
| 5 | [phase5-kernel-tuning.md](phase5-kernel-tuning.md) | Nsight Compute profile clean — **cuttable phase** |
| 6 | [phase6-benchmarks.md](phase6-benchmarks.md) | Complete §7 matrix with tidy CSV output |
| 7 | [phase7-analysis-report.md](phase7-analysis-report.md) | Acceptance criteria A1–A7 satisfied |

Source specification: [`cuda-encryption-module-spec.md`](../../cuda-encryption-module-spec.md)
at the repository root. It is the authority; these phase docs decompose it.

---

## Rules that survive across sessions

Anyone — human or another Claude session — picking this up should know the
following, because none of it is recoverable from the code alone.

### The expected finding is that the GPU does not always win

PCIe Gen4 x16 delivers roughly 24 GB/s in practice. The kernel is capable of
roughly 110 GB/s. **The bus is the bottleneck, by about 4.6x.** For Scenario A
(host RAM → GPU → host RAM) the transfer, not the arithmetic, sets the ceiling.

A result showing "GPU always wins" is a failed analysis, not a success — it means
the CPU baseline was too weak or the numbers were kernel-only. Acceptance
criterion **A7 requires the final report to state when the CPU wins.** That is the
credibility criterion; do not soften it.

### Every throughput figure must be labeled

**end-to-end** (host buffer to host buffer, includes transfer) or **kernel-only**
(device-resident, excludes transfer). Acceptance criterion **A6**. The two are not
comparable, and mixing them unlabeled is how GPU crypto results get overstated.
The CSV writers carry a `throughput_label` column for this reason; keep it.

### The CPU baseline must be strong

OpenSSL 3.x EVP, multi-threaded across all cores, with AES-NI/PCLMULQDQ and
ideally VAES active. A weak baseline is the single most common flaw in published
GPU-crypto literature. Gate P1.7 enforces >= 4x scaling on 8 threads specifically
to catch a baseline that silently ran single-threaded.

### Phase 3 is protected; phase 5 is cuttable

If schedule pressure hits, cut **phase 5** (kernel tuning). Never cut **phase 3**
(full GCM correctness). An optimized kernel that computes the wrong tag is worth
less than nothing.

Phases 4–6 can silently break the correctness established in phase 3 — this is
why every phase re-runs all prior gates rather than trusting an earlier green run.

### A skipped test is not a passed test

GPU-dependent tests exit `77`, which CTest reports as *skipped*, so a GPU-less
host gets a green run without having tested anything GPU-related. Exit gates
require **passed**, not "passed or skipped". See
[phase1-gpu-verification.md](phase1-gpu-verification.md).

### Known traps, already paid for

- **NVML stub linking.** On Linux, linking the toolkit's
  `stubs/libnvidia-ml.so` instead of the driver's `libnvidia-ml.so.1` builds and
  runs fine, and returns well-formed telemetry that is entirely zeros. Gate P1.4
  checks for all-zero power specifically to catch this.
- **WSL2** exposes `nvidia-smi` but usually not power or PCIe counters. P1.4 will
  fail there. Use native Linux or Windows for recorded runs.
- **The 64 KB per-key H-table.** The 8-bit windowed GHASH table is
  16 x 256 x 16 B = 64 KB *per key*. In a multi-tenant design that multiplies by
  tenant count and will exhaust VRAM long before payload does. Phase 3 covers it;
  do not discover it in phase 6.
- **Nonce reuse under sharding.** Splitting a payload across threads or blocks
  must derive a distinct IV per shard. `derive_shard_iv()` in `src/cpu_baseline.c`
  does this on the CPU side; the GPU side needs the same discipline. GCM nonce
  reuse is a total loss of confidentiality *and* authenticity, not a degradation.

### Documentation lives in an Obsidian vault

Architecture, data flow, VRAM budget model, PCIe ceiling analysis, threat model,
and the benchmark matrix are maintained at
`D:/redwine-obsidian-local/Wiki/Projects/CUDA Processing Cryptography/` on the
author's machine — **not in this repo, and not reachable from another machine.**

If you do not have that vault, `cuda-encryption-module-spec.md` at the repo root
plus these phase docs are the complete authority. Nothing in the build depends on
the vault.

---

## Running the gates

```bash
scripts/check_prereqs.sh --require-gpu     # Linux/macOS
scripts\check_prereqs.ps1 -RequireGpu      # Windows

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --build build --target verify-phase1
```

Phase 1 builds with `GPUSEAL_ENABLE_CUDA=OFF`; CUDA turns on from phase 2.

Later phases will register their tests under labels `phase2` … `phase7`, so
`ctest -L phase3` runs one phase's gates and `ctest` alone runs every gate
defined so far. Keep that convention — the re-run-all-prior-gates rule depends
on it.
