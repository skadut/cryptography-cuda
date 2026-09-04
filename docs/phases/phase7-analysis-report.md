# Phase 7 — Analysis, Charts, Threat Model, Report

**Exit condition:** All acceptance criteria A1–A7 satisfied.

## Scope

1. **Analysis notebook** (D6) — reproducible charts from benchmark CSV
2. **Threat model** (D7) — what the module does NOT defend against
3. **Findings report** (D8) — crossover analysis + PCIe ceiling argument
4. **Reproduction instructions** (D9) — exact versions of everything

## Required charts

1. **Throughput vs payload size** — GPU-A, GPU-C, CPU-1T, CPU-nT on one plot, log x-scale, crossover point annotated
2. **Latency CDF** at three representative payload sizes (small/medium/large)
3. **Stacked time breakdown** per request: H2D / kernel / D2H / host overhead
4. **VRAM occupancy** vs configuration (chunk_size × streams × active_keys)
5. **Gbps/W** by backend and algorithm — the energy efficiency comparison
6. **Nsight timeline screenshot** showing achieved transfer/compute overlap

## Threat model (D7) — a deliverable, not an appendix

Must explicitly state what the module does NOT defend against:

- GPU cache timing and warp-divergence side channels
- Physical PCIe bus sniffing
- Co-tenant VRAM remnant attacks (LeftoverLocals class)
- Nonce reuse (relies on host-side CSPRNG discipline)
- Key compromise at the HSM/control plane level
- Unified Memory page eviction to swap
- What H100 Confidential Computing adds (and its limits)
- The boundary: this is a bulk coprocessor, not a key custodian

## Findings report (D8)

### The crossover analysis (headline finding)
- At what payload size does GPU end-to-end throughput exceed CPU all-threads?
- Stated with confidence interval
- Broken down by algorithm (AES-GCM vs ChaCha20 vs SHA-256)

### The PCIe ceiling argument
- Measured PCIe throughput vs theoretical
- Kernel-only vs end-to-end comparison proving the bottleneck
- Where the GPU genuinely wins (Scenario C, non-AES algorithms, CPU offload, power efficiency)

### The multi-tenant finding (if in scope)
- VRAM exhaustion curve from H-tables
- At what tenant count does the GPU become impractical?

### Credibility criterion A7
The report must state, in plain language, the conditions under which the CPU is the better choice. A report that concludes "GPU always wins" fails review.

## Reproduction instructions (D9)

Record exact:
- GPU model + driver version + CUDA toolkit version
- CPU model + microcode + base/boost clock + core count
- OpenSSL version + whether VAES path active
- OS + kernel version
- Compiler version + flags
- Memory: host RAM size/speed, GPU VRAM

## Files added

```
analysis/notebook.ipynb      — all 6 charts, reproducible from bench CSV
docs/threat-model.md         — D7
docs/findings-report.md      — D8
docs/reproduction.md         — D9
```

## Acceptance criteria checklist

| ID | Criterion | Verified by |
|---|---|---|
| A1 | G1–G7 pass in CI | CI pipeline |
| A2 | Transfer/compute overlap ≥ 80% | Nsight Systems timeline |
| A3 | End-to-end throughput within 15% of PCIe ceiling for ≥ 256 MB | Chart 1 |
| A4 | Peak VRAM within 10% of model prediction | Chart 4 |
| A5 | Crossover size identified with confidence interval | Chart 1 annotation |
| A6 | Every throughput labeled end-to-end or kernel-only | CSV `throughput_label` column |
| A7 | Report states when CPU wins | Findings report conclusion |

---

## Phase Gate — Project completion

Run `ctest -L phase7` plus manual review. All must pass:

| # | Test | Verifies |
|---|---|---|
| P7.1 | All Phase 1–6 gates pass in a clean CI run | **A1** |
| P7.2 | Notebook executes end-to-end from committed CSV with zero manual steps | D6 reproducible |
| P7.3 | All 6 required charts render without error | Chart deliverables complete |
| P7.4 | Chart 1 has the crossover point annotated with a confidence interval | **A5** |
| P7.5 | Chart 4 overlays measured VRAM on the §5 model line, deviation ≤ 10% | **A4** |
| P7.6 | Automated grep: no throughput figure in report lacks a label | **A6** |
| P7.7 | End-to-end throughput ≥ 256 MB is within 15% of measured PCIe ceiling | **A3** |
| P7.8 | Threat model (D7) exists and names every non-defended threat from §9 | D7 complete |
| P7.9 | Findings report contains an explicit "when the CPU is the better choice" section | **A7** — credibility criterion |
| P7.10 | Reproduction doc (D9) lists exact GPU, driver, CUDA, CPU, OpenSSL, OS, compiler versions | D9 complete |
| P7.11 | Fresh clone + `check_prereqs` + build + `ctest` passes on a second machine | Genuinely reproducible |

**Exit gate:** A1–A7 all satisfied. P7.9 is non-negotiable — a report concluding "GPU always wins" fails review.
