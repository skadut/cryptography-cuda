# Phase 4 — Pipeline Optimization

**Exit condition:** Transfer/compute overlap ≥ 80% (acceptance criterion A2).

## Scope

1. **Pinned memory ring** — `cudaHostAlloc` staging buffers replacing pageable `malloc`
2. **Multi-stream pipeline** — K CUDA streams with depth-D pipeline for H2D/compute/D2H overlap
3. **In-place CTR mode** — encrypt in-place to halve VRAM buffer usage
4. **Async transfers** — `cudaMemcpyAsync` on non-default streams
5. **Dispatch policy** — route to GPU or CPU based on payload size (crossover from benchmarks)

## Pinned memory ring

```c
cudaHostAlloc(&staging[i], chunk_size, cudaHostAllocDefault);
```

Ring of `num_streams × pipeline_depth` pinned buffers. Zero before `cudaFreeHost`.

## Stream pipeline

```
Stream 0: [H2D chunk0] [Kernel chunk0] [D2H chunk0]
Stream 1:              [H2D chunk1]    [Kernel chunk1] [D2H chunk1]
Stream 2:                              [H2D chunk2]    [Kernel chunk2] [D2H chunk2]
```

Independent copy engines per direction on modern NVIDIA GPUs enable true H2D + D2H overlap. Compute overlaps with transfers on different streams.

Sweep parameters: streams ∈ {1, 2, 4, 8}, pipeline depth ∈ {1, 2, 3}.

## In-place mode

CTR mode XORs keystream with plaintext — output can overwrite input buffer.

```
VRAM_buffers = chunk_size × num_streams × pipeline_depth × 1   (was ×2)
```

64 MB × 4 × 2 = 512 MB (down from 1024 MB).

## VRAM budget validation

Measure peak VRAM via `nvmlDeviceGetMemoryInfo` and compare to the model:

```
VRAM_total = VRAM_buffers + VRAM_keys + VRAM_context
```

Must be within 10% (acceptance criterion A4).

## Validation

- Nsight Systems timeline: `nsys profile --trace=cuda,nvtx` shows overlap visually
- Programmatic: measure idle gaps between kernel end and next kernel start
- Target: ≥ 80% of kernel time overlaps with transfer time on adjacent streams

## Files modified/added

```
src/host/pinned_pool.c        — pinned memory ring allocator
src/host/stream_scheduler.cu  — multi-stream pipeline scheduler
src/host/chunker.c            — extended with async dispatch
src/gpu_encrypt.cu            — refactored for async, in-place
```

---

## Phase Gate — Verification before Phase 5

Run `ctest -L phase4`. All must pass:

| # | Test | Verifies |
|---|---|---|
| P4.1 | All Phase 1–3 gates still pass (G1–G7 re-run on the pipelined path) | Optimization did not break correctness |
| P4.2 | In-place output == out-of-place output for identical input | In-place mode correct |
| P4.3 | Output identical across streams ∈ {1,2,4,8} and depth ∈ {1,2,3} | Pipeline config independence (G7 extended) |
| P4.4 | **A2**: measured transfer/compute overlap ≥ 80% | Primary exit condition |
| P4.5 | **A4**: measured peak VRAM within 10% of §5 model prediction | VRAM model validated |
| P4.6 | Pinned throughput > pageable throughput for payloads ≥ 64 MB | Pinned memory actually helps |
| P4.7 | Pinned buffers read back as zero before `cudaFreeHost` | §9 pinned memory hygiene |
| P4.8 | No pinned-memory leak across 1000 encrypt cycles (RSS stable) | Ring allocator correct |
| P4.9 | `compute-sanitizer --tool=synccheck` clean on multi-stream path | Stream sync correct |
| P4.10 | Dispatch policy routes small payloads to CPU, large to GPU per configured threshold | Dispatch logic wired |

**Exit gate:** A2 (≥80% overlap) and A4 (VRAM within 10%) met, G1–G7 still green.
