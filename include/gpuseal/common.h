#ifndef GPUSEAL_COMMON_H
#define GPUSEAL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPUSEAL_AES256_KEY_BYTES 32
#define GPUSEAL_GCM_IV_BYTES     12
#define GPUSEAL_GCM_TAG_BYTES    16

typedef enum {
    GPUSEAL_OK = 0,
    GPUSEAL_ERR_ARG = 1,
    GPUSEAL_ERR_ALLOC = 2,
    GPUSEAL_ERR_CRYPTO = 3,
    GPUSEAL_ERR_IO = 4,
    GPUSEAL_ERR_UNSUPPORTED = 5
} gpuseal_status;

const char *gpuseal_strerror(gpuseal_status s);

/* Monotonic clock in seconds. Wall-clock deltas only; never an absolute epoch. */
double gpuseal_now(void);

/* Constant-time compare. Used for tag checks so a failed verify leaks no
   information through timing. */
int gpuseal_ct_equal(const void *a, const void *b, size_t n);

/* Best-effort scrub. Written so the compiler cannot elide it. */
void gpuseal_secure_zero(void *p, size_t n);

int gpuseal_cpu_count(void);

#ifdef __cplusplus
}
#endif
#endif
