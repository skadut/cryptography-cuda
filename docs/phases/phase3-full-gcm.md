# Phase 3 — Full AES-256-GCM (GHASH + Tag Assembly)

**Exit condition:** Correctness gates G3–G7 pass.
**Protected phase:** if time runs short, cut Phase 5 — never Phase 3.

## Scope

1. **GHASH H-table precomputation** — compute powers of H in GF(2^128), build 8-bit windowed lookup table (64 KB per key)
2. **GHASH parallel reduction kernel** — restructure serial Galois field polynomial chain as tree reduction
3. **Tag assembly** — combine GHASH output with AES-CTR encrypted counter-0 block
4. **Full AES-256-GCM encrypt/decrypt** — CTR (Phase 2) + GHASH + tag
5. **Gates G3–G7** wired into CI

## GHASH parallel reduction — the core engineering work

GHASH is defined as: `X_i = (X_{i-1} ⊕ C_i) · H` in GF(2^128)

This is serial. To parallelize:

1. Precompute H-powers: `H^1, H^2, H^4, H^8, ...` up to `H^n` where n = number of ciphertext blocks
2. Each thread multiplies one ciphertext block by the appropriate H-power
3. Tree reduction (parallel prefix sum) combines partial products
4. GF(2^128) multiplication uses Karatsuba + reduction modulo the GCM polynomial

### H-table structure

8-bit windowed multiplication table:
- 16 bytes per entry × 256 entries per window × 16 windows = **64 KB per key**
- Precomputed on host or device, stored in VRAM for the key's lifetime
- This is the dominant per-key VRAM cost (see Phase 4 VRAM budget validation)

### GF(2^128) multiplication kernel

```
gf128_mul(a, b):
  Karatsuba decomposition into 3 half-width multiplications
  Reduction modulo x^128 + x^7 + x^2 + x + 1
  Equivalent to PCLMULQDQ on CPU
```

## Tag assembly

```
J0 = IV || 0x00000001              (96-bit IV case)
E_K(J0) = AES-256-CTR encrypt of J0
Tag = GHASH(AAD, ciphertext) ⊕ E_K(J0)
```

Tag is 128 bits. Verification: recompute tag, constant-time compare.

## Correctness gates

| Gate | Test |
|---|---|
| G3 | Tag verification **rejects** tampered ciphertext, tampered AAD, tampered tag |
| G4 | Chunk-boundary: payload split across chunks == identical output to unsplit |
| G5 | Counter-block overflow at 2^32 block boundary handled correctly |
| G6 | Fuzzing: lengths 0, 1, 15, 16, 17 bytes and chunk_size ± 1 |
| G7 | Determinism: identical inputs → identical outputs across stream counts and chunk sizes |

G4 requires the chunker (Phase 1 skeleton extended here). The GHASH state must be correctly carried across chunk boundaries.

## Files added

```
src/kernels/ghash.cu           — GHASH parallel reduction kernel
src/kernels/gf128_mul.cu       — GF(2^128) multiplication (Karatsuba)
src/ghash_table.c              — H-table precomputation
src/gcm.cu                     — full GCM orchestration (CTR + GHASH + tag)
tests/test_g3_tamper.c         — G3 tamper rejection
tests/test_g4_chunking.c       — G4 chunk boundary
tests/test_g5_overflow.c       — G5 counter overflow
tests/test_g6_fuzz.c           — G6 edge-case lengths
tests/test_g7_determinism.c    — G7 determinism across configs
```

## Security constraints (from §9, enforced here)

- `cudaMemset` zero key schedule + H-table + plaintext buffers after use
- No secret-dependent branching in kernel control flow
- Nonce generated on host only (CSPRNG or counter)
- Tag comparison must be constant-time (host-side)

---

## Phase Gate — Verification before Phase 4

Run `ctest -L phase3`. This is the strictest gate in the project.

| # | Test | Verifies |
|---|---|---|
| P3.1 | All Phase 1 + Phase 2 gates still pass | No regression |
| P3.2 | GF(2^128) multiply matches reference vectors (incl. carry-heavy operands) | Karatsuba + reduction correct |
| P3.3 | H-table entries match slow reference `gf128_mul(H, i)` for all 16 windows | Table precomputation correct |
| P3.4 | Full GCM output (ciphertext **and** tag) bit-exact vs OpenSSL for CAVP vectors | End-to-end GCM correct |
| P3.5 | **G3**: flipping any single ciphertext bit → tag verification fails | Tamper detection |
| P3.6 | **G3**: flipping any single AAD bit → tag verification fails | AAD is authenticated |
| P3.7 | **G3**: flipping any single tag bit → verification fails | Tag integrity |
| P3.8 | **G4**: 256 MB payload in 1/2/4/16 chunks → identical ciphertext + tag | GHASH state carries across chunks |
| P3.9 | **G5**: payload crossing the 2^32-block counter boundary matches OpenSSL | Counter overflow |
| P3.10 | **G6**: lengths {0, 1, 15, 16, 17, chunk±1} all match OpenSSL | Edge-case lengths |
| P3.11 | **G7**: identical input → identical output across stream counts and chunk sizes | Determinism |
| P3.12 | Empty plaintext with non-empty AAD produces correct tag | Degenerate case |
| P3.13 | Tag comparison is constant-time (no early return on mismatch — code review + timing check) | §9 no secret-dependent branching |
| P3.14 | H-table and key schedule read back as zero after free | §9 zeroing |
| P3.15 | `compute-sanitizer --tool=racecheck` clean on GHASH reduction | Reduction has no races |

**Exit gate:** G3–G7 all green, sanitizer clean, P3.4 bit-exact. This phase is protected — do not cut it or proceed with any failure.
