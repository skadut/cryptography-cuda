#include "gpuseal/cpu_baseline.h"

#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <string.h>
#include <stdlib.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

static gpuseal_status one_shot_encrypt(const uint8_t *key, const uint8_t *iv,
                                       const uint8_t *aad, size_t aad_len,
                                       const uint8_t *pt, size_t pt_len,
                                       uint8_t *ct_out, uint8_t *tag_out)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { return GPUSEAL_ERR_ALLOC; }

    gpuseal_status st = GPUSEAL_ERR_CRYPTO;
    int len = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) { goto done; }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GPUSEAL_GCM_IV_BYTES, NULL) != 1) { goto done; }
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) { goto done; }

    if (aad_len > 0 && EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) { goto done; }

    /* EVP takes int lengths, so long buffers are fed in chunks. */
    size_t off = 0;
    while (off < pt_len) {
        size_t n = pt_len - off;
        if (n > (size_t)INT32_MAX / 2) { n = (size_t)INT32_MAX / 2; }
        if (EVP_EncryptUpdate(ctx, ct_out + off, &len, pt + off, (int)n) != 1) { goto done; }
        off += (size_t)len;
    }

    if (EVP_EncryptFinal_ex(ctx, ct_out + off, &len) != 1) { goto done; }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GPUSEAL_GCM_TAG_BYTES, tag_out) != 1) { goto done; }
    st = GPUSEAL_OK;

done:
    EVP_CIPHER_CTX_free(ctx);
    return st;
}

gpuseal_status gpuseal_cpu_gcm_encrypt(const uint8_t key[GPUSEAL_AES256_KEY_BYTES],
                                       const uint8_t iv[GPUSEAL_GCM_IV_BYTES],
                                       const uint8_t *aad, size_t aad_len,
                                       const uint8_t *pt, size_t pt_len,
                                       uint8_t *ct_out,
                                       uint8_t tag_out[GPUSEAL_GCM_TAG_BYTES])
{
    if (!key || !iv || !tag_out) { return GPUSEAL_ERR_ARG; }
    if (pt_len > 0 && (!pt || !ct_out)) { return GPUSEAL_ERR_ARG; }
    if (aad_len > 0 && !aad) { return GPUSEAL_ERR_ARG; }
    return one_shot_encrypt(key, iv, aad, aad_len, pt, pt_len, ct_out, tag_out);
}

gpuseal_status gpuseal_cpu_gcm_decrypt(const uint8_t key[GPUSEAL_AES256_KEY_BYTES],
                                       const uint8_t iv[GPUSEAL_GCM_IV_BYTES],
                                       const uint8_t *aad, size_t aad_len,
                                       const uint8_t *ct, size_t ct_len,
                                       const uint8_t tag[GPUSEAL_GCM_TAG_BYTES],
                                       uint8_t *pt_out)
{
    if (!key || !iv || !tag) { return GPUSEAL_ERR_ARG; }
    if (ct_len > 0 && (!ct || !pt_out)) { return GPUSEAL_ERR_ARG; }
    if (aad_len > 0 && !aad) { return GPUSEAL_ERR_ARG; }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { return GPUSEAL_ERR_ALLOC; }

    gpuseal_status st = GPUSEAL_ERR_CRYPTO;
    int len = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) { goto done; }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GPUSEAL_GCM_IV_BYTES, NULL) != 1) { goto done; }
    if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) { goto done; }

    if (aad_len > 0 && EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) { goto done; }

    size_t off = 0;
    while (off < ct_len) {
        size_t n = ct_len - off;
        if (n > (size_t)INT32_MAX / 2) { n = (size_t)INT32_MAX / 2; }
        if (EVP_DecryptUpdate(ctx, pt_out + off, &len, ct + off, (int)n) != 1) { goto done; }
        off += (size_t)len;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GPUSEAL_GCM_TAG_BYTES, (void *)tag) != 1) { goto done; }

    /* Returns <= 0 on tag mismatch. This is the G3 tamper-rejection path. */
    if (EVP_DecryptFinal_ex(ctx, pt_out + off, &len) != 1) {
        gpuseal_secure_zero(pt_out, ct_len);
        st = GPUSEAL_ERR_CRYPTO;
        goto done;
    }
    st = GPUSEAL_OK;

done:
    EVP_CIPHER_CTX_free(ctx);
    return st;
}

/* Derives a per-shard IV by adding `shard` to the big-endian counter in the
   last 4 bytes of the base IV. Shards must stay under 2^32 for this to be
   collision-free, which the thread count trivially satisfies. */
static void derive_shard_iv(const uint8_t base[GPUSEAL_GCM_IV_BYTES],
                            uint32_t shard, uint8_t out[GPUSEAL_GCM_IV_BYTES])
{
    memcpy(out, base, GPUSEAL_GCM_IV_BYTES);
    uint32_t ctr = ((uint32_t)out[8] << 24) | ((uint32_t)out[9] << 16) |
                   ((uint32_t)out[10] << 8) | (uint32_t)out[11];
    ctr += shard;
    out[8]  = (uint8_t)(ctr >> 24);
    out[9]  = (uint8_t)(ctr >> 16);
    out[10] = (uint8_t)(ctr >> 8);
    out[11] = (uint8_t)ctr;
}

gpuseal_status gpuseal_cpu_baseline_run(const uint8_t key[GPUSEAL_AES256_KEY_BYTES],
                                        const uint8_t *pt, size_t pt_len,
                                        uint8_t *ct_out,
                                        int threads,
                                        gpuseal_baseline_result *out)
{
    if (!key || !pt || !ct_out || !out || pt_len == 0) { return GPUSEAL_ERR_ARG; }
    if (threads < 1) { threads = gpuseal_cpu_count(); }

    static const uint8_t base_iv[GPUSEAL_GCM_IV_BYTES] = {0};
    const size_t shard = (pt_len + (size_t)threads - 1) / (size_t)threads;

    /* Tags are produced and discarded: this measures throughput, and the
       correctness oracle is gpuseal_cpu_gcm_encrypt, tested separately. */
    uint8_t *tags = (uint8_t *)malloc((size_t)threads * GPUSEAL_GCM_TAG_BYTES);
    if (!tags) { return GPUSEAL_ERR_ALLOC; }

    volatile int failed = 0;
    const double t0 = gpuseal_now();

#ifdef _OPENMP
#pragma omp parallel for num_threads(threads) schedule(static)
#endif
    for (int t = 0; t < threads; t++) {
        const size_t off = (size_t)t * shard;
        if (off >= pt_len) { continue; }
        size_t n = pt_len - off;
        if (n > shard) { n = shard; }

        uint8_t iv[GPUSEAL_GCM_IV_BYTES];
        derive_shard_iv(base_iv, (uint32_t)t, iv);

        if (one_shot_encrypt(key, iv, NULL, 0, pt + off, n,
                             ct_out + off, tags + (size_t)t * GPUSEAL_GCM_TAG_BYTES) != GPUSEAL_OK) {
            failed = 1;
        }
    }

    const double dt = gpuseal_now() - t0;
    gpuseal_secure_zero(tags, (size_t)threads * GPUSEAL_GCM_TAG_BYTES);
    free(tags);
    if (failed) { return GPUSEAL_ERR_CRYPTO; }

    out->threads = threads;
    out->bytes   = pt_len;
    out->seconds = dt;
    out->gbps    = dt > 0.0 ? ((double)pt_len * 8.0) / (dt * 1e9) : 0.0;
    out->label   = "end-to-end";
    return GPUSEAL_OK;
}

void gpuseal_cpu_features_probe(gpuseal_cpu_features *out)
{
    if (!out) { return; }
    out->aesni = 0;
    out->vaes = 0;
    out->pclmulqdq = 0;
    out->openssl_version = OpenSSL_version(OPENSSL_VERSION);

#if defined(__x86_64__) || defined(_M_X64)
    uint32_t r[4] = {0, 0, 0, 0};
#if defined(_MSC_VER)
    __cpuid((int *)r, 1);
#else
    __asm__ __volatile__("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(1), "c"(0));
#endif
    out->aesni     = (r[2] >> 25) & 1;
    out->pclmulqdq = (r[2] >> 1) & 1;

#if defined(_MSC_VER)
    __cpuidex((int *)r, 7, 0);
#else
    __asm__ __volatile__("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(7), "c"(0));
#endif
    out->vaes = (r[2] >> 9) & 1;
#endif
}
