# NIST CAVP AES-GCM vectors

Not committed. Download from the NIST CAVP GCM test vector archive and place the
`.rsp` files here.

The loader (`src/test_vectors.c`) keeps only vectors with `[Keylen = 256]` and
`[IVlen = 96]`; everything else is outside the scope declared in the spec.

Files used by the gates:

| File | Used by |
|---|---|
| `gcmEncryptExtIV256.rsp` | G1 known-answer encrypt |
| `gcmDecrypt256.rsp` | G1 decrypt, plus the expected-`FAIL` tamper vectors for G3 |

`test_cpu_baseline` does not require these: it generates a small `.rsp` inline
and carries one embedded known-answer vector, so P1.8 and P1.9 are meaningful on
a fresh clone. Add the real files before the Phase 2 G1 gate.
