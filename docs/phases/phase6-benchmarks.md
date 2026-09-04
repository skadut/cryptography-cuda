# Phase 6 — Full Benchmark Sweep

**Exit condition:** Complete §7 matrix with tidy CSV output.

## Scope

1. **Benchmark harness** — automated sweep across all dimensions
2. **Additional algorithm kernels** — ChaCha20-Poly1305, SHA-256 batch
3. **Telemetry collection** — NVML sampling concurrent with benchmarks
4. **CSV output** — tidy format keyed by run ID

## Sweep dimensions

| Dimension | Values |
|---|---|
| Payload size | 1 KB, 4 KB, 64 KB, 256 KB, 1 MB, 16 MB, 64 MB, 256 MB, 1 GB, 8 GB |
| Chunk size | 4, 16, 64, 256 MB |
| CUDA streams | 1, 2, 4, 8 |
| Backend | GPU Scenario A, GPU Scenario C (resident), CPU 1-thread, CPU all-threads |
| Algorithm | AES-256-GCM, AES-256-CTR (no auth), ChaCha20-Poly1305, SHA-256 (batch) |
| Memory mode | Pageable, pinned, in-place, out-of-place |

Each cell: ≥ 30 iterations after ≥ 5 warmup. Report median and p99.

## Why ChaCha20 and SHA-256

CPUs have dedicated AES-NI instructions but weaker ChaCha/Keccak acceleration. The GPU's relative advantage should be visibly larger for these algorithms. That contrast is a genuine finding.

## Additional kernels

### ChaCha20-Poly1305
- ChaCha20: 20-round ARX cipher, embarrassingly parallel per 64-byte block
- Poly1305: MAC computation, similar parallelization challenge as GHASH
- No CPU hardware acceleration → GPU should show larger relative gain

### SHA-256 (batch)
- Independent hashes of multiple messages
- Each message maps to a thread/warp
- GPU wins when batch size is large enough to saturate SMs

## Output format

CSV columns:
```
run_id,timestamp,backend,algorithm,payload_bytes,chunk_bytes,
num_streams,memory_mode,iteration,
throughput_gbps,throughput_label,latency_us,
h2d_ms,kernel_ms,d2h_ms,
vram_peak_mb,gpu_util_pct,power_w,pcie_tx_mbps,pcie_rx_mbps
```

`throughput_label` is always `end-to-end` or `kernel-only`. Unlabeled = rejected.

## Files added

```
bench/bench_sweep.cu         — automated sweep harness
bench/bench_config.h         — sweep dimension definitions
src/kernels/chacha20.cu      — ChaCha20 kernel
src/kernels/poly1305.cu      — Poly1305 MAC kernel
src/kernels/sha256.cu        — SHA-256 batch kernel
src/cpu_baseline_chacha.c    — CPU ChaCha20 baseline
src/cpu_baseline_sha256.c    — CPU SHA-256 baseline
```

---

## Phase Gate — Verification before Phase 7

Run `ctest -L phase6`. All must pass:

| # | Test | Verifies |
|---|---|---|
| P6.1 | All Phase 1–5 gates still pass | No regression |
| P6.2 | ChaCha20-Poly1305 matches RFC 8439 test vectors | New kernel correct |
| P6.3 | ChaCha20-Poly1305 bit-exact vs OpenSSL EVP for 10,000 tuples | Broad correctness |
| P6.4 | SHA-256 matches NIST FIPS-180-4 vectors, batch output == sequential | New kernel correct |
| P6.5 | Sweep completes every matrix cell without crash or timeout | Harness robust |
| P6.6 | Every CSV row has `throughput_label` ∈ {`end-to-end`, `kernel-only`} | **A6** — no unlabeled figures |
| P6.7 | Every cell has ≥ 30 post-warmup iterations recorded | Statistical validity |
| P6.8 | Telemetry CSV timestamps align with benchmark run windows | Telemetry correlation works |
| P6.9 | Re-running one cell reproduces median within 5% | Measurement stability |
| P6.10 | Crossover point is present in the data (GPU and CPU curves actually intersect) | **A5** is answerable |

**Exit gate:** Full matrix complete, A6 satisfied on every row, crossover observable in data.
