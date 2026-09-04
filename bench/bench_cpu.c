/* Phase 1 exit artifact: baseline_results.csv (gate P1.10).
   Every throughput figure here is end-to-end (host buffer to host buffer),
   which acceptance criterion A6 requires be stated explicitly. */

#include "gpuseal/cpu_baseline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t SIZES[] = {
    1u << 10, 4u << 10, 64u << 10, 256u << 10,
    1u << 20, 16u << 20, 64u << 20, 256u << 20
};
#define NSIZES (sizeof(SIZES) / sizeof(SIZES[0]))

#define WARMUP 5
#define ITERS  30

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    const char *out_path = argc > 1 ? argv[1] : "baseline_results.csv";
    const int max_threads = gpuseal_cpu_count();

    gpuseal_cpu_features feat;
    gpuseal_cpu_features_probe(&feat);

    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 1;
    }

    fprintf(f, "backend,algorithm,payload_bytes,threads,iteration,"
               "throughput_gbps,throughput_label,latency_us,"
               "cores,aesni,vaes,pclmulqdq,openssl_version\n");

    uint8_t key[GPUSEAL_AES256_KEY_BYTES];
    for (size_t i = 0; i < sizeof(key); i++) { key[i] = (uint8_t)(i * 11 + 5); }

    double gbps[ITERS];
    int rc = 0;

    for (size_t si = 0; si < NSIZES; si++) {
        const size_t n = SIZES[si];
        uint8_t *pt = (uint8_t *)malloc(n);
        uint8_t *ct = (uint8_t *)malloc(n);
        if (!pt || !ct) {
            fprintf(stderr, "alloc failed at %zu bytes\n", n);
            free(pt); free(ct);
            rc = 1;
            break;
        }
        for (size_t i = 0; i < n; i++) { pt[i] = (uint8_t)i; }

        const int thread_counts[2] = {1, max_threads};
        for (int ti = 0; ti < 2; ti++) {
            const int threads = thread_counts[ti];
            if (ti == 1 && threads == 1) { continue; }

            gpuseal_baseline_result r;
            for (int w = 0; w < WARMUP; w++) {
                if (gpuseal_cpu_baseline_run(key, pt, n, ct, threads, &r) != GPUSEAL_OK) {
                    fprintf(stderr, "warmup failed\n");
                    rc = 1;
                    goto cleanup;
                }
            }

            for (int it = 0; it < ITERS; it++) {
                if (gpuseal_cpu_baseline_run(key, pt, n, ct, threads, &r) != GPUSEAL_OK) {
                    fprintf(stderr, "iteration failed\n");
                    rc = 1;
                    goto cleanup;
                }
                gbps[it] = r.gbps;
                fprintf(f, "cpu,aes-256-gcm,%zu,%d,%d,%.4f,%s,%.2f,%d,%d,%d,%d,\"%s\"\n",
                        n, threads, it, r.gbps, r.label, r.seconds * 1e6,
                        max_threads, feat.aesni, feat.vaes, feat.pclmulqdq,
                        feat.openssl_version ? feat.openssl_version : "unknown");
            }

            qsort(gbps, ITERS, sizeof(gbps[0]), cmp_double);
            printf("%10zu B  %2d thr  median %7.3f Gbps  p99 %7.3f Gbps  [end-to-end]\n",
                   n, threads, gbps[ITERS / 2], gbps[(ITERS * 99) / 100]);
        }

cleanup:
        free(pt);
        free(ct);
        if (rc) { break; }
    }

    fclose(f);
    if (rc == 0) { printf("\nwrote %s\n", out_path); }
    return rc;
}
