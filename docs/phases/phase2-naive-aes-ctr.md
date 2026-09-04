# Phase 2 — Naive AES-CTR Kernel

**Exit condition:** Correctness gates G1 and G2 pass.

## Scope

1. **AES-256 key expansion** on host CPU — produces 15-round expanded key schedule (240 bytes)
2. **Naive AES-256-CTR CUDA kernel** — one thread per 16-byte block, T-table lookup in global memory
3. **Host↔device round trip** — pageable memory `cudaMemcpy` (not yet pinned/async)
4. **G1 gate** — NIST CAVP AES-CTR known-answer vectors pass
5. **G2 gate** — bit-exact match against OpenSSL EVP for 10,000 random tuples

## Kernel design (naive, unoptimized)

```
Thread i:
  1. Compute counter block: IV || (initial_counter + i)
  2. AES-256 encrypt counter block using expanded key schedule (14 rounds, T-table)
  3. XOR keystream block with plaintext[i*16 .. (i+1)*16]
  4. Write ciphertext block
```

- Grid: `(num_blocks + threads_per_block - 1) / threads_per_block` blocks
- Block: 256 threads (tunable later in Phase 5)
- AES T-tables (4 × 1 KB = 4 KB) in global memory (moved to shared in Phase 5)
- Key schedule in constant memory (240 bytes fits easily)

## Data flow (Phase 2 — no pipeline)

```
Host: plaintext buffer (malloc)
  → cudaMemcpy H2D (blocking, pageable)
  → kernel launch (single stream)
  → cudaMemcpy D2H (blocking)
Host: ciphertext buffer
```

No overlap, no pinned memory, no multi-stream. Just correctness.

## Correctness gates wired

| Gate | Test |
|---|---|
| G1 | Every NIST CAVP AES-256-CTR vector: kernel output == expected ciphertext |
| G2 | 10,000 random (key, IV, plaintext) tuples: kernel output == OpenSSL EVP output, bit-exact |

G2 uses the CPU baseline from Phase 1 as the reference oracle.

## Files added

```
src/aes_keygen.c          — AES-256 key expansion
src/kernels/aes_ctr.cu    — naive CTR kernel
src/gpu_encrypt.cu        — host-side launch wrapper (alloc, copy, launch, copy back)
tests/test_g1_cavp.c      — G1 CAVP vector test
tests/test_g2_random.c    — G2 random-tuple test
```

## What this phase does NOT include

- No GHASH, no authentication tag, no GCM — that is Phase 3
- No pinned memory, no async copies, no multi-stream
- No performance measurement (correctness only)

---

## Phase Gate — Verification before Phase 3

Run `ctest -L phase2`. All must pass:

| # | Test | Verifies |
|---|---|---|
| P2.1 | All Phase 1 gates still pass | No regression |
| P2.2 | AES-256 key expansion matches FIPS-197 reference schedule | Key expansion correct |
| P2.3 | **G1**: every NIST CAVP AES-256-CTR vector produces expected ciphertext | Known-answer correctness |
| P2.4 | **G2**: 10,000 random (key, IV, plaintext) tuples bit-exact vs OpenSSL EVP | Broad correctness |
| P2.5 | Encrypt→decrypt round trip returns original plaintext (CTR is symmetric) | Involution property |
| P2.6 | Kernel handles non-block-aligned lengths (1, 15, 17 bytes) correctly | Partial block handling |
| P2.7 | `cuda-memcheck` / `compute-sanitizer` reports zero errors | No OOB access or race |
| P2.8 | No CUDA API call returns non-`cudaSuccess` (checked via error macro) | Error handling wired |
| P2.9 | Kernel output identical across block sizes 128/256/512 threads | Launch-config independence |
| P2.10 | Key schedule buffer is zero after `gpuseal_free_key()` (read back and assert) | §9 zeroing constraint honored |

**Exit gate:** G1 and G2 green, sanitizer clean. Do not start Phase 3 otherwise.
