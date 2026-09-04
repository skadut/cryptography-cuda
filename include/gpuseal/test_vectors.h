#ifndef GPUSEAL_TEST_VECTORS_H
#define GPUSEAL_TEST_VECTORS_H

#include "gpuseal/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One NIST CAVP GCM known-answer vector. Byte arrays are owned by the vector
   set and freed by gpuseal_vectors_free. */
typedef struct {
    uint8_t *key;   size_t key_len;
    uint8_t *iv;    size_t iv_len;
    uint8_t *pt;    size_t pt_len;
    uint8_t *aad;   size_t aad_len;
    uint8_t *ct;    size_t ct_len;
    uint8_t *tag;   size_t tag_len;
    int      count;      /* the [Count = N] field */
    int      fail;       /* decrypt-side vectors marked FAIL (expected reject) */
} gpuseal_vector;

typedef struct {
    gpuseal_vector *v;
    size_t          n;
    size_t          cap;
} gpuseal_vector_set;

/* Parses a NIST CAVP .rsp file (gcmEncryptExtIV256.rsp and friends).
   Only vectors whose Keylen is 256 and IVlen is 96 are retained: the rest are
   outside the scope declared in the spec. */
gpuseal_status gpuseal_vectors_load(const char *path, gpuseal_vector_set *out);

void gpuseal_vectors_free(gpuseal_vector_set *s);

/* Hex helpers, exposed because the tests use them directly. */
gpuseal_status gpuseal_hex_decode(const char *hex, uint8_t **out, size_t *out_len);
void gpuseal_hex_encode(const uint8_t *buf, size_t n, char *out);

#ifdef __cplusplus
}
#endif
#endif
