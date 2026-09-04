#ifndef GPUSEAL_CPU_BASELINE_H
#define GPUSEAL_CPU_BASELINE_H

#include "gpuseal/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single-shot AES-256-GCM via OpenSSL EVP. This is the correctness oracle for
   gate G2 as well as the performance baseline for A5/A7. */
gpuseal_status gpuseal_cpu_gcm_encrypt(const uint8_t key[GPUSEAL_AES256_KEY_BYTES],
                                       const uint8_t iv[GPUSEAL_GCM_IV_BYTES],
                                       const uint8_t *aad, size_t aad_len,
                                       const uint8_t *pt, size_t pt_len,
                                       uint8_t *ct_out,
                                       uint8_t tag_out[GPUSEAL_GCM_TAG_BYTES]);

/* Returns GPUSEAL_ERR_CRYPTO on tag mismatch. Plaintext is scrubbed before
   returning on failure, so a caller cannot use unauthenticated output. */
gpuseal_status gpuseal_cpu_gcm_decrypt(const uint8_t key[GPUSEAL_AES256_KEY_BYTES],
                                       const uint8_t iv[GPUSEAL_GCM_IV_BYTES],
                                       const uint8_t *aad, size_t aad_len,
                                       const uint8_t *ct, size_t ct_len,
                                       const uint8_t tag[GPUSEAL_GCM_TAG_BYTES],
                                       uint8_t *pt_out);

typedef struct {
    int    threads;
    size_t bytes;
    double seconds;
    double gbps;          /* always end-to-end: host buffer to host buffer */
    const char *label;    /* "end-to-end" */
} gpuseal_baseline_result;

/* Splits pt_len across `threads` independent GCM contexts, one per thread.
   Each thread gets a distinct IV derived from the base IV counter field, so the
   split never reuses a nonce. Not a single logical GCM message: this measures
   aggregate CPU throughput, which is what the A5 crossover comparison needs. */
gpuseal_status gpuseal_cpu_baseline_run(const uint8_t key[GPUSEAL_AES256_KEY_BYTES],
                                        const uint8_t *pt, size_t pt_len,
                                        uint8_t *ct_out,
                                        int threads,
                                        gpuseal_baseline_result *out);

/* Reports whether OpenSSL selected an AES-NI/VAES path. Best effort: OpenSSL
   exposes no direct query, so this reads the CPUID capability vector.
   ponytail: capability, not proof of the active code path. Confirm with
   `openssl speed -evp aes-256-gcm` when recording P1.10 results. */
typedef struct {
    int aesni;
    int vaes;
    int pclmulqdq;
    const char *openssl_version;
} gpuseal_cpu_features;

void gpuseal_cpu_features_probe(gpuseal_cpu_features *out);

#ifdef __cplusplus
}
#endif
#endif
