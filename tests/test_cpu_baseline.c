/* Phase 1 gate tests P1.6, P1.7, P1.9.
   Assert-based, no framework: one binary, non-zero exit on failure. */

#include "gpuseal/cpu_baseline.h"
#include "gpuseal/test_vectors.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...) do {                                  \
    if (!(cond)) {                                             \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);   \
        fprintf(stderr, __VA_ARGS__);                          \
        fprintf(stderr, "\n");                                 \
        failures++;                                            \
    }                                                          \
} while (0)

/* NIST CAVP gcmEncryptExtIV256, Keylen=256 IVlen=96 PTlen=0 AADlen=0, Count=0.
   Embedded so P1.9 has a known-answer test even before .rsp files are fetched. */
static void test_known_answer(void)
{
    static const char *key_hex = "b52c505a37d78eda5dd34f20c22540ea1b58963cf8e5bf8ffa85f9f2492505b4";
    static const char *iv_hex  = "516c33929df5a3284ff463d7";
    static const char *tag_hex = "bdc1ac884d332457a1d2664f168c76f0";

    uint8_t *key = NULL, *iv = NULL, *want_tag = NULL;
    size_t key_len = 0, iv_len = 0, tag_len = 0;

    CHECK(gpuseal_hex_decode(key_hex, &key, &key_len) == GPUSEAL_OK, "key decode");
    CHECK(gpuseal_hex_decode(iv_hex, &iv, &iv_len) == GPUSEAL_OK, "iv decode");
    CHECK(gpuseal_hex_decode(tag_hex, &want_tag, &tag_len) == GPUSEAL_OK, "tag decode");
    CHECK(key_len == 32 && iv_len == 12 && tag_len == 16, "unexpected field lengths");

    uint8_t got_tag[GPUSEAL_GCM_TAG_BYTES];
    const gpuseal_status st =
        gpuseal_cpu_gcm_encrypt(key, iv, NULL, 0, NULL, 0, NULL, got_tag);
    CHECK(st == GPUSEAL_OK, "encrypt returned %s", gpuseal_strerror(st));
    CHECK(memcmp(got_tag, want_tag, GPUSEAL_GCM_TAG_BYTES) == 0,
          "CAVP known-answer tag mismatch");

    free(key); free(iv); free(want_tag);
}

/* P1.6 shape: encrypt then decrypt recovers the plaintext exactly. */
static void test_roundtrip(void)
{
    const size_t n = 1u << 20;
    uint8_t key[32], iv[12];
    for (size_t i = 0; i < sizeof(key); i++) { key[i] = (uint8_t)(i * 7 + 1); }
    for (size_t i = 0; i < sizeof(iv); i++)  { iv[i]  = (uint8_t)(i * 13 + 3); }

    uint8_t *pt = (uint8_t *)malloc(n);
    uint8_t *ct = (uint8_t *)malloc(n);
    uint8_t *rt = (uint8_t *)malloc(n);
    assert(pt && ct && rt);
    for (size_t i = 0; i < n; i++) { pt[i] = (uint8_t)(i * 31 + (i >> 8)); }

    uint8_t tag[GPUSEAL_GCM_TAG_BYTES];
    CHECK(gpuseal_cpu_gcm_encrypt(key, iv, NULL, 0, pt, n, ct, tag) == GPUSEAL_OK, "encrypt");
    CHECK(gpuseal_cpu_gcm_decrypt(key, iv, NULL, 0, ct, n, tag, rt) == GPUSEAL_OK, "decrypt");
    CHECK(memcmp(pt, rt, n) == 0, "roundtrip mismatch");

    /* Tamper rejection: flip one ciphertext bit, decrypt must fail. */
    ct[n / 2] ^= 0x01;
    CHECK(gpuseal_cpu_gcm_decrypt(key, iv, NULL, 0, ct, n, tag, rt) == GPUSEAL_ERR_CRYPTO,
          "tampered ciphertext was accepted");

    free(pt); free(ct); free(rt);
}

/* P1.7: multi-threaded harness must produce correct output and actually run
   in parallel. The scaling assertion itself lives in bench_cpu, since a test
   machine under load cannot guarantee a speedup ratio. */
static void test_parallel_harness(void)
{
    const size_t n = 4u << 20;
    uint8_t key[32] = {0};
    uint8_t *pt = (uint8_t *)malloc(n);
    uint8_t *ct1 = (uint8_t *)malloc(n);
    uint8_t *ctN = (uint8_t *)malloc(n);
    assert(pt && ct1 && ctN);
    for (size_t i = 0; i < n; i++) { pt[i] = (uint8_t)i; }

    gpuseal_baseline_result r1, rN;
    CHECK(gpuseal_cpu_baseline_run(key, pt, n, ct1, 1, &r1) == GPUSEAL_OK, "1-thread run");
    CHECK(gpuseal_cpu_baseline_run(key, pt, n, ctN, 4, &rN) == GPUSEAL_OK, "4-thread run");

    CHECK(r1.bytes == n && rN.bytes == n, "byte count wrong");
    CHECK(strcmp(r1.label, "end-to-end") == 0, "throughput must be labeled end-to-end");
    CHECK(r1.gbps > 0.0 && rN.gbps > 0.0, "throughput not computed");

    /* Different thread counts shard differently and use different derived IVs,
       so ciphertexts intentionally differ. Only the 1-thread case is a single
       GCM message and comparable to the oracle. */
    uint8_t tag[GPUSEAL_GCM_TAG_BYTES];
    uint8_t *ref = (uint8_t *)malloc(n);
    assert(ref);
    static const uint8_t zero_iv[12] = {0};
    CHECK(gpuseal_cpu_gcm_encrypt(key, zero_iv, NULL, 0, pt, n, ref, tag) == GPUSEAL_OK, "oracle");
    CHECK(memcmp(ref, ct1, n) == 0, "1-thread output differs from single-shot oracle");

    free(pt); free(ct1); free(ctN); free(ref);
}

/* P1.8: loader round-trips a small .rsp written on the fly, so the test needs
   no committed fixture to be meaningful. */
static void test_vector_loader(void)
{
    const char *path = "gpuseal_test_vectors.rsp";
    FILE *f = fopen(path, "w");
    if (!f) { CHECK(0, "cannot write temp rsp"); return; }
    fputs("# generated by test_cpu_baseline\n"
          "[Keylen = 128]\n[IVlen = 96]\n[PTlen = 0]\n[AADlen = 0]\n[Taglen = 128]\n\n"
          "Count = 0\n"
          "Key = 11111111111111111111111111111111\n"
          "IV = 222222222222222222222222\n"
          "PT = \nAAD = \n"
          "CT = \nTag = 33333333333333333333333333333333\n\n"
          "[Keylen = 256]\n[IVlen = 96]\n[PTlen = 128]\n[AADlen = 0]\n[Taglen = 128]\n\n"
          "Count = 0\n"
          "Key = b52c505a37d78eda5dd34f20c22540ea1b58963cf8e5bf8ffa85f9f2492505b4\n"
          "IV = 516c33929df5a3284ff463d7\n"
          "PT = \nAAD = \n"
          "CT = \nTag = bdc1ac884d332457a1d2664f168c76f0\n\n"
          "Count = 1\n"
          "Key = b52c505a37d78eda5dd34f20c22540ea1b58963cf8e5bf8ffa85f9f2492505b4\n"
          "IV = 516c33929df5a3284ff463d7\n"
          "PT = \nAAD = \n"
          "CT = \nTag = bdc1ac884d332457a1d2664f168c76f0\n", f);
    fclose(f);

    gpuseal_vector_set set;
    const gpuseal_status st = gpuseal_vectors_load(path, &set);
    CHECK(st == GPUSEAL_OK, "load returned %s", gpuseal_strerror(st));

    /* Only the two 256-bit vectors survive the Keylen filter. */
    CHECK(set.n == 2, "expected 2 retained vectors, got %zu", set.n);
    if (set.n == 2) {
        CHECK(set.v[0].key_len == 32, "key len %zu", set.v[0].key_len);
        CHECK(set.v[0].iv_len == 12, "iv len %zu", set.v[0].iv_len);
        CHECK(set.v[0].tag_len == 16, "tag len %zu", set.v[0].tag_len);
        CHECK(set.v[0].count == 0 && set.v[1].count == 1, "count fields wrong");

        /* P1.9: every retained vector must verify against OpenSSL EVP. */
        for (size_t i = 0; i < set.n; i++) {
            const gpuseal_vector *v = &set.v[i];
            uint8_t tag[GPUSEAL_GCM_TAG_BYTES];
            uint8_t *ct = v->pt_len ? (uint8_t *)malloc(v->pt_len) : NULL;
            const gpuseal_status e = gpuseal_cpu_gcm_encrypt(
                v->key, v->iv, v->aad, v->aad_len, v->pt, v->pt_len, ct, tag);
            CHECK(e == GPUSEAL_OK, "vector %zu encrypt failed", i);
            CHECK(memcmp(tag, v->tag, GPUSEAL_GCM_TAG_BYTES) == 0,
                  "vector %zu tag mismatch", i);
            free(ct);
        }
    }

    gpuseal_vectors_free(&set);
    remove(path);
}

int main(void)
{
    gpuseal_cpu_features f;
    gpuseal_cpu_features_probe(&f);
    printf("openssl: %s\n", f.openssl_version ? f.openssl_version : "unknown");
    printf("cpu: aesni=%d vaes=%d pclmulqdq=%d cores=%d\n",
           f.aesni, f.vaes, f.pclmulqdq, gpuseal_cpu_count());

    test_known_answer();
    test_roundtrip();
    test_parallel_harness();
    test_vector_loader();

    if (failures) {
        fprintf(stderr, "\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("all phase 1 checks passed\n");
    return 0;
}
