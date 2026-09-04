# CUDA Bulk Encryption Module — Engineering Specification

**Codename:** `gpuseal`
**Version:** 0.1 (build spec)
**Audience:** implementing engineers
**Deliverable:** a GPU-accelerated AES-256-GCM bulk encryption service with full throughput, VRAM, and power instrumentation, plus an honest CPU baseline.

---

## 0. Purpose and Non-Goals

### Purpose

Demonstrate, with reproducible numbers, where GPU acceleration of a production cryptographic workload actually pays off — and where it does not. The output is both a working encryption module and a benchmark report that an architect could use to make a real build/buy decision.

### Explicit non-goals

| Not doing | Why |
|---|---|
| Storing long-term key material in VRAM | `cudaMalloc()` does not zero on allocation; no tamper-response; no attestation outside H100 CC mode |
| Hand-rolled constant-time GPU crypto for adversarial settings | GPU side-channel behaviour (cache, warp divergence) is poorly characterised vs. CPU |
| Replacing an HSM | This is a bulk-data coprocessor, not a key custodian |
| Beating published kernel-only records | Kernel-only numbers are misleading for a real service (see §4) |

### The one honest question this project answers

> For a real encryption service where plaintext starts in host RAM and ciphertext must return to host RAM, does a discrete GPU beat a modern server CPU with VAES — and at what data size does the crossover happen?

Most published GPU-crypto papers do not answer this, because they benchmark kernel-only throughput against unoptimised CPU code. This spec requires you to do better.

---

## 1. Real-World Scenario Anchor

Pick **one** of these as the framing. They have different data-movement profiles, which changes the result.

| Scenario | Data path | GPU prospects |
|---|---|---|
| **A. Backup / object-storage encryption pipeline** | Host RAM → GPU → Host RAM → disk/network | PCIe-bound, throughput-oriented, latency-tolerant |
| **B. Database field-level encryption** | Many small records, host-resident | Poor — dominated by launch overhead |
| **C. Encrypted analytics on GPU-resident data** | Data already in VRAM, encrypt in place | Best case — no PCIe round trip |

**Recommendation:** build **A** as the primary (it is the most common industry ask), and include **C** as a secondary configuration, because C is where the GPU genuinely wins and the contrast is the analytically interesting result.

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────┐
│  CONTROL PLANE (CPU / HSM)                              │
│  • Key custody, KEK unwrap, key rotation                │
│  • Per-request DEK derivation (HKDF)                    │
│  • Never sends KEK or long-term keys to device          │
└────────────────────┬────────────────────────────────────┘
                     │  ephemeral DEK + expanded schedule
                     ▼
┌─────────────────────────────────────────────────────────┐
│  ORCHESTRATION LAYER (host)                             │
│  • Chunker: splits stream into N-MB units               │
│  • Pinned-memory staging ring (cudaHostAlloc)           │
│  • Stream scheduler: K CUDA streams, depth-D pipeline   │
│  • Dispatch policy: route to GPU or CPU by size         │
└────────────────────┬────────────────────────────────────┘
                     │  H2D (copy engine 1)
                     ▼
┌─────────────────────────────────────────────────────────┐
│  DEVICE LAYER (CUDA)                                    │
│  • AES-256-CTR keystream kernel  (parallel, per-block)  │
│  • GHASH kernel (Karatsuba / PCLMUL-equivalent reduce)  │
│  • Tag assembly                                         │
│  • Explicit cudaMemset zeroing of key + plaintext bufs  │
└────────────────────┬────────────────────────────────────┘
                     │  D2H (copy engine 2)
                     ▼
┌─────────────────────────────────────────────────────────┐
│  TELEMETRY LAYER                                        │
│  • CUDA events (kernel + transfer timing)               │
│  • NVML poll (VRAM, util, power, PCIe throughput)       │
│  • Per-request latency histogram → CSV / Parquet        │
└─────────────────────────────────────────────────────────┘
```

**Why CTR + separate GHASH:** AES-GCM's counter mode is embarrassingly parallel (each 16-byte block independent). GHASH is a serial chain over the ciphertext, so it must be restructured as a parallel reduction over precomputed powers of H. This split is the core engineering work.

---

## 3. Hardware Specification Matrix

Build and benchmark on at least two tiers so the report shows scaling behaviour.

| Tier | Example GPU | VRAM | Mem BW | PCIe | Purpose |
|---|---|---|---|---|---|
| Consumer | RTX 4090 / 5090 | 24–32 GB GDDR6X/7 | ~1.0–1.8 TB/s | Gen4 x16 | Cost-sensitive baseline |
| Datacenter | A100 / L40S | 40–48 GB | ~2.0 TB/s | Gen4 x16 | Standard server deployment |
| Current-gen | H100 / H200 | 80–141 GB HBM3(e) | 3.3–4.8 TB/s | Gen5 x16 | Confidential Computing mode available |

**CPU baseline (mandatory, non-negotiable):**
- Modern server CPU with **AES-NI + PCLMULQDQ**, and ideally **VAES** (Ice Lake-SP or later, Zen 4 or later)
- Benchmark against **OpenSSL 3.x EVP AES-256-GCM**, multi-threaded across all cores — not a textbook AES implementation
- Record: cores used, base/boost clock, whether VAES path is active (`openssl speed -evp aes-256-gcm`)

> Benchmarking a CUDA kernel against single-threaded, non-AES-NI CPU code is the single most common flaw in the published literature. Do not reproduce it.

---

## 4. The PCIe Ceiling — Read This Before Setting Targets

This is the analytical heart of the project.

**Kernel-only capability.** Published optimised AES-128 on an RTX 2070 Super reaches ~878 Gbps ≈ **110 GB/s**. Modern cards do better.

**Transport capability.**

| PCIe generation | Theoretical x16 | Practical (per direction) |
|---|---|---|
| Gen3 x16 | 15.75 GB/s | ~12 GB/s |
| Gen4 x16 | 31.5 GB/s | ~24–26 GB/s |
| Gen5 x16 | 63 GB/s | ~50–55 GB/s |

PCIe is full-duplex and modern NVIDIA cards have independent copy engines per direction, so H2D and D2H can overlap. For a pass-through encryption service the effective ceiling is therefore roughly the **single-direction practical rate**, assuming perfect overlap.

**The consequence:**

```
Gen4 x16 ceiling      ≈  24 GB/s   ( ~190 Gbps )
AES kernel capability ≈ 110 GB/s   ( ~880 Gbps )
                         ─────────
Kernel is ~4.5x faster than the pipe feeding it.
```

The GPU kernel is **not** the bottleneck in Scenario A. PCIe is. Any design that optimises the kernel further without addressing transport is optimising the wrong thing.

**And the uncomfortable comparison:** a 32-core server CPU with VAES doing ~10–15 GB/s per core aggregates well past 100 GB/s with zero transfer cost. For plain AES-GCM on host-resident data, a modern CPU may simply win.

**Where the GPU wins anyway — state these explicitly in the report:**
1. Data is already GPU-resident (Scenario C) — no PCIe round trip
2. The CPU has no hardware instruction for the algorithm (Keccak/SHA-3, ChaCha20, lattice-based PQC polynomial multiplication, zk-proof MSM/NTT)
3. CPU cores are needed for other work and encryption must be offloaded
4. Power efficiency per Gbps at sustained high load

---

## 5. VRAM Budget Model

Engineers consistently underestimate this. Specify it as a formula, then validate against NVML.

```
VRAM_total = VRAM_buffers + VRAM_keys + VRAM_context
```

### Data buffers

```
VRAM_buffers = chunk_size × num_streams × pipeline_depth × (in_place ? 1 : 2)
```

Worked example — 64 MB chunks, 4 streams, depth 2, out-of-place:

```
64 MB × 4 × 2 × 2 = 1024 MB
```

CTR mode permits **in-place** encryption, halving this to 512 MB. Do it.

### Key material — the multi-tenant trap

| Item | Size per key |
|---|---|
| AES-256 expanded key schedule | 240 B |
| GCM H-table (8-bit windowed, 16 × 256 × 16 B) | 64 KB |

```
VRAM_keys = num_active_keys × ~64 KB
```

| Active tenant keys | VRAM for tables |
|---|---|
| 100 | 6.4 MB |
| 1,000 | 64 MB |
| 10,000 | 640 MB |
| 100,000 | 6.4 GB |

**This is the finding worth publishing:** for a multi-tenant crypto service, VRAM is consumed by *per-key GHASH tables*, not by the data being encrypted. At 100k tenants you exhaust a 24 GB consumer card on lookup tables alone. Mitigations: 4-bit tables (4 KB/key, slower), LRU key-table cache in VRAM with host-side spill, or shared-key batching.

### Required output

An **occupancy chart** plotting VRAM used vs. (chunk size × streams × active keys), with the card's capacity as a horizontal line. This is exactly the kind of visualisation that makes the spec land.

---

## 6. Instrumentation Specification

### 6.1 Metrics — mandatory set

| Metric | Unit | Collection | Why |
|---|---|---|---|
| End-to-end throughput | GB/s | wall clock, host buffer → host buffer | The only number that matters for Scenario A |
| Kernel-only throughput | GB/s | `cudaEventElapsedTime` around kernel | For comparison to literature |
| H2D / D2H transfer time | ms | CUDA events around `cudaMemcpyAsync` | Proves the PCIe ceiling |
| Transfer/compute overlap | % | Nsight Systems timeline | Validates the stream pipeline |
| Latency p50 / p95 / p99 / p99.9 | µs | per-request histogram | Tail latency kills SLAs |
| Peak VRAM | MB | `nvmlDeviceGetMemoryInfo` | Validates §5 model |
| GPU utilisation | % | `nvmlDeviceGetUtilizationRates` | Detects starvation |
| PCIe throughput | MB/s | `nvmlDeviceGetPcieThroughput` | Direct ceiling measurement |
| Power draw | W | `nvmlDeviceGetPowerUsage` | For Gbps/W |
| **Energy efficiency** | **Gbps/W** | derived | The metric the literature reports; makes you comparable |
| SM occupancy | % | Nsight Compute | Kernel tuning signal |
| DRAM throughput | % of peak | Nsight Compute | Memory- vs compute-bound diagnosis |
| **Crossover size** | **bytes** | swept benchmark | The headline finding |

### 6.2 Collection tooling

```bash
# Continuous telemetry during a run
nvidia-smi --query-gpu=timestamp,utilization.gpu,utilization.memory,\
memory.used,memory.total,power.draw,clocks.sm,temperature.gpu \
  --format=csv -l 1 > telemetry.csv

# Timeline: is transfer actually overlapping compute?
nsys profile -o gpuseal_timeline --trace=cuda,nvtx ./gpuseal_bench

# Kernel internals: occupancy, stalls, memory throughput
ncu --set full --kernel-name aes_ctr_kernel -o kernel_profile ./gpuseal_bench
```

Programmatic NVML (Python via `pynvml`, or C via `nvml.h`) should sample at ≥10 Hz during the run and emit a tidy CSV keyed by run ID. Annotate phases with **NVTX ranges** so the Nsight timeline is readable.

### 6.3 Reporting rule

Every throughput figure in the report must be labelled **end-to-end** or **kernel-only**. Unlabelled figures are rejected in review. This one rule is what separates this project from the papers it cites.

---

## 7. Benchmark Matrix

Sweep these dimensions; each cell runs ≥30 iterations after ≥5 warmup iterations, reporting median and p99.

| Dimension | Values |
|---|---|
| Payload size | 1 KB, 4 KB, 64 KB, 256 KB, 1 MB, 16 MB, 64 MB, 256 MB, 1 GB, 8 GB |
| Chunk size | 4, 16, 64, 256 MB |
| CUDA streams | 1, 2, 4, 8 |
| Backend | GPU (Scenario A), GPU (Scenario C, resident), CPU 1-thread, CPU all-threads |
| Algorithm | AES-256-GCM, AES-256-CTR (no auth), ChaCha20-Poly1305, SHA-256 (batch) |
| Memory mode | pageable, pinned, in-place, out-of-place |

**Include ChaCha20 and SHA-256 deliberately.** CPUs have dedicated AES instructions but weaker ChaCha/Keccak acceleration, so the GPU's relative advantage should be visibly larger there. That contrast is a genuine result, not filler.

### Required charts

1. Throughput (GB/s) vs. payload size — GPU-A, GPU-C, CPU-1T, CPU-nT on one axis, **log x-scale**, with the crossover point annotated
2. Latency CDF at three representative payload sizes
3. Stacked time breakdown per request: H2D / kernel / D2H / host overhead
4. VRAM occupancy vs. configuration (from §5)
5. Gbps/W by backend and algorithm
6. Nsight timeline screenshot showing achieved overlap

---

## 8. Correctness Gates — Before Any Optimisation

Non-negotiable ordering: **nothing in §7 runs until §8 passes.** Cut optimisation scope before shipping an unverified implementation.

| Gate | Requirement |
|---|---|
| G1 | NIST CAVP AES-GCM known-answer vectors pass, all key sizes and IV lengths in scope |
| G2 | Bit-exact match against OpenSSL EVP output for 10,000 random (key, IV, plaintext, AAD) tuples |
| G3 | Tag verification correctly **rejects** tampered ciphertext, tampered AAD, and tampered tag |
| G4 | Chunk-boundary correctness: a payload split across chunks produces identical output to the unsplit payload |
| G5 | Counter-block overflow handled correctly at the 2^32 block boundary |
| G6 | Fuzzing: random-length inputs including 0, 1, 15, 16, 17 bytes and chunk_size ± 1 |
| G7 | Determinism: identical inputs produce identical outputs across stream counts and chunk sizes |

Wire G1–G7 into CI. A performance regression is an annoyance; a correctness regression in an encryption module is a vulnerability.

---

## 9. Security Constraints

| Constraint | Implementation requirement |
|---|---|
| No long-term keys in VRAM | Only ephemeral DEKs, derived per-request, crossing the PCIe bus |
| Explicit zeroing | `cudaMemset` key schedules and plaintext buffers immediately after use — never rely on allocation-time clearing |
| No Unified Memory for sensitive buffers | UM pages can be evicted to host memory or swap |
| Pinned memory hygiene | Zero pinned staging buffers before `cudaFreeHost` |
| IV/nonce discipline | Nonce generation on host (CSPRNG or deterministic counter); never derive nonces on device |
| No secret-dependent branching | Kernel control flow must not depend on key or plaintext bytes; document known limits on GPU constant-time guarantees |
| Multi-tenancy isolation | Document the co-residency risk (LeftoverLocals class, CVE-2023-4969). Either enforce exclusive GPU access per tenant, or require H100+ Confidential Computing with attestation |
| Threat model doc | Ship a written threat model stating what this module does **not** defend against |

The threat model document is a deliverable, not an appendix. It is also the part that signals security engineering maturity rather than benchmark chasing.

---

## 10. Deliverables

| # | Artifact | Format |
|---|---|---|
| D1 | Encryption module source | C/CUDA core + Python (CuPy/Numba) reference implementation |
| D2 | CPU baseline harness | OpenSSL EVP, multi-threaded |
| D3 | Correctness test suite | CI-wired, gates G1–G7 |
| D4 | Benchmark harness | Sweeps §7 matrix, emits tidy CSV |
| D5 | Telemetry collector | NVML sampler + NVTX annotations |
| D6 | Analysis notebook | Charts 1–6, reproducible from D4/D5 output |
| D7 | Threat model | Markdown |
| D8 | Findings report | Includes the crossover analysis and the PCIe ceiling argument |
| D9 | Reproduction instructions | Exact hardware, driver, CUDA, OpenSSL versions |

---

## 11. Acceptance Criteria

| ID | Criterion |
|---|---|
| A1 | All correctness gates G1–G7 pass in CI |
| A2 | Transfer/compute overlap ≥ 80% measured in Nsight Systems |
| A3 | End-to-end throughput within 15% of the measured PCIe practical ceiling for payloads ≥ 256 MB |
| A4 | Measured peak VRAM within 10% of the §5 model prediction |
| A5 | The crossover payload size is identified with a stated confidence interval |
| A6 | Every reported throughput number is labelled end-to-end or kernel-only |
| A7 | The report states, in plain language, the conditions under which the CPU is the better choice |

**A7 is the credibility criterion.** A benchmark report that concludes "GPU always wins" is a marketing document. One that identifies exactly where the GPU loses is an engineering document.

---

## 12. Build Sequence

| Phase | Work | Exit condition |
|---|---|---|
| 1 | Environment, NVML/NVTX telemetry skeleton, CPU baseline harness | Baseline OpenSSL numbers recorded |
| 2 | Naive AES-CTR kernel, host↔device round trip, no optimisation | G1, G2 pass |
| 3 | GHASH parallel reduction, tag assembly, full GCM | G3–G7 pass |
| 4 | Pinned memory, multi-stream pipeline, in-place mode | A2 overlap target met |
| 5 | Kernel tuning: shared-memory T-tables, bank-conflict removal, occupancy | Nsight Compute clean |
| 6 | Full benchmark sweep, telemetry collection | §7 matrix complete |
| 7 | Analysis, charts, threat model, report | A1–A7 satisfied |

Phase 3 is protected from schedule pressure. If time runs short, cut phase 5 — not phase 3.

---

## 13. Open Questions to Resolve Before Phase 1

1. **Scenario commitment** — A only, or A + C? C requires a plausible GPU-resident data source (an analytics workload) to be realistic.
2. **Target hardware access** — which tier from §3 is actually available? Gen4 vs Gen5 changes every target number in §4.
3. **Multi-tenancy in scope?** If yes, §5 key-table analysis becomes a primary finding and needs its own benchmark axis.
4. **Confidential Computing** — is H100 CC mode available for testing? It would let the report cover the only configuration with HSM-adjacent guarantees.
5. **PQC extension** — worth adding Kyber/Dilithium polynomial multiplication as a fifth algorithm? It is where the GPU advantage is largest and least contested, but it expands scope meaningfully.
