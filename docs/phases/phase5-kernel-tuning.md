# Phase 5 — Kernel Tuning

**Exit condition:** Nsight Compute profile is clean (no major stalls or inefficiencies).
**Cuttable phase:** if schedule pressure, cut this — not Phase 3.

## Scope

1. **Shared-memory AES T-tables** — move 4 × 1 KB T-tables from global to shared memory
2. **Bank-conflict removal** — pad or permute T-table layout to avoid 32-way bank conflicts
3. **Occupancy optimization** — tune block size, register usage, shared memory per block
4. **Warp-level optimizations** — minimize divergence, use warp shuffle for reductions

## Shared-memory T-tables

AES T-tables are 4 × 256 × 4 bytes = 4 KB total. Fit comfortably in shared memory (typical limit 48–100 KB per SM).

```cuda
__shared__ uint32_t Te0[256], Te1[256], Te2[256], Te3[256];
// Cooperative load from global at block start
Te0[threadIdx.x] = d_Te0[threadIdx.x];  // 256 threads, 1 load each
__syncthreads();
```

Eliminates global memory latency for the inner AES round loop (14 rounds × 4 lookups = 56 global loads → 56 shared loads per block).

## Bank-conflict analysis

Shared memory has 32 banks (4-byte stride). T-table access pattern:
- `Te0[state_byte]` — state bytes vary across threads → potential 32-way conflict if threads in a warp encrypt adjacent blocks with similar plaintext

Mitigation: XOR table index with `threadIdx.x` or pad table to 257 entries.

## Occupancy tuning

Profile with Nsight Compute:

```bash
ncu --set full --kernel-name aes_ctr_kernel -o kernel_profile ./gpuseal_bench
```

Key metrics:
- Achieved occupancy vs theoretical
- Register pressure (spill to local memory)
- Shared memory occupancy limit
- Stall reasons (memory dependency, execution dependency, barrier)

## GHASH kernel tuning

- Warp shuffle (`__shfl_xor_sync`) for tree reduction within warps
- Minimize GF(2^128) multiplication latency in the reduction tree
- Consider hybrid: warp-level reduction + block-level + grid-level

## Files modified

```
src/kernels/aes_ctr.cu    — shared-memory T-tables, block size tuning
src/kernels/ghash.cu      — warp shuffle reduction
```

---

## Phase Gate — Verification before Phase 6

Run `ctest -L phase5`. All must pass:

| # | Test | Verifies |
|---|---|---|
| P5.1 | All Phase 1–4 gates still pass (full G1–G7 re-run) | Tuning did not break correctness |
| P5.2 | Tuned kernel output bit-identical to Phase 3 naive kernel for 10,000 tuples | Optimization is behavior-preserving |
| P5.3 | Kernel-only throughput improved vs Phase 4 baseline (record delta) | Tuning actually helped |
| P5.4 | Nsight Compute: achieved occupancy ≥ 50% | Occupancy target |
| P5.5 | Nsight Compute: zero shared-memory bank conflicts reported | Bank-conflict removal worked |
| P5.6 | Nsight Compute: no register spilling to local memory | Register pressure acceptable |
| P5.7 | Warp divergence within AES round loop is zero | No secret-dependent branching (§9) |
| P5.8 | `compute-sanitizer` still clean after shared-memory changes | No new OOB or race |

**Exit gate:** Nsight Compute profile clean, P5.2 bit-identical. If this phase is cut for schedule, Phase 4 output ships instead — correctness is unaffected.
