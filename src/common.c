#include "gpuseal/common.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

const char *gpuseal_strerror(gpuseal_status s)
{
    switch (s) {
    case GPUSEAL_OK:              return "ok";
    case GPUSEAL_ERR_ARG:         return "invalid argument";
    case GPUSEAL_ERR_ALLOC:       return "allocation failed";
    case GPUSEAL_ERR_CRYPTO:      return "crypto operation failed";
    case GPUSEAL_ERR_IO:          return "io error";
    case GPUSEAL_ERR_UNSUPPORTED: return "unsupported";
    default:                      return "unknown";
    }
}

double gpuseal_now(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) { QueryPerformanceFrequency(&freq); }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

int gpuseal_ct_equal(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    unsigned char diff = 0;
    for (size_t i = 0; i < n; i++) { diff |= (unsigned char)(pa[i] ^ pb[i]); }
    return diff == 0;
}

void gpuseal_secure_zero(void *p, size_t n)
{
#if defined(_WIN32)
    SecureZeroMemory(p, n);
#else
    /* volatile pointer keeps the store from being optimized away */
    volatile unsigned char *vp = (volatile unsigned char *)p;
    while (n--) { *vp++ = 0; }
#endif
}

int gpuseal_cpu_count(void)
{
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}
