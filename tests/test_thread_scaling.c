/* Phase 1 gate P1.7: the CPU baseline must actually parallelize.

   This gate is load-bearing for the whole study. Acceptance criterion A7 asks
   the report to state honestly when the CPU wins; if the CPU baseline silently
   ran single-threaded, every GPU speedup number would be inflated and the
   conclusion would be wrong in the flattering direction. A weak baseline is the
   most common flaw in published GPU-crypto results -- this test exists to make
   that failure loud.

   Gate: 8-thread throughput >= 4x 1-thread throughput (50% scaling efficiency).

   Exits 77 (CTest "skipped") when the machine has fewer than 8 usable cores, or
   when OpenMP was not compiled in -- neither is a code defect, but neither can
   produce a meaningful measurement. A skip here means P1.7 is UNVERIFIED and the
   phase gate is not clear; re-run on the target benchmark machine. */

#include "gpuseal/cpu_baseline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP_EXIT 77

#define TARGET_THREADS 8
#define MIN_SPEEDUP    4.0

/* Large enough that per-run overhead (thread spawn, EVP context setup) is noise
   rather than the thing being measured. 256 MiB at ~2 GB/s is ~130 ms per run. */
#define PAYLOAD (256u << 20)

#define WARMUP 2
#define TRIALS 5

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Median of TRIALS runs. Median, not mean: a single scheduler hiccup or a
   turbo-clock drop should not decide a pass/fail gate. */
static double median_gbps(const uint8_t *key, const uint8_t *pt, size_t n,
                          uint8_t *ct, int threads, int *ok)
{
    gpuseal_baseline_result r;
    double v[TRIALS];

    for (int i = 0; i < WARMUP; i++) {
        if (gpuseal_cpu_baseline_run(key, pt, n, ct, threads, &r) != GPUSEAL_OK) {
            *ok = 0;
            return 0.0;
        }
    }
    for (int i = 0; i < TRIALS; i++) {
        if (gpuseal_cpu_baseline_run(key, pt, n, ct, threads, &r) != GPUSEAL_OK) {
            *ok = 0;
            return 0.0;
        }
        v[i] = r.gbps;
    }

    qsort(v, TRIALS, sizeof(v[0]), cmp_double);
    *ok = 1;
    return v[TRIALS / 2];
}

int main(void)
{
    const int cores = gpuseal_cpu_count();
    printf("detected %d logical core(s)\n", cores);

    if (cores < TARGET_THREADS) {
        printf("SKIP: P1.7 needs >= %d cores to measure %d-thread scaling, "
               "this host has %d. P1.7 remains UNVERIFIED.\n",
               TARGET_THREADS, TARGET_THREADS, cores);
        return SKIP_EXIT;
    }

    uint8_t key[GPUSEAL_AES256_KEY_BYTES];
    for (size_t i = 0; i < sizeof(key); i++) { key[i] = (uint8_t)(i * 13 + 2); }

    uint8_t *pt = (uint8_t *)malloc(PAYLOAD);
    uint8_t *ct = (uint8_t *)malloc(PAYLOAD);
    if (!pt || !ct) {
        fprintf(stderr, "FAIL: cannot allocate 2 x %u bytes\n", (unsigned)PAYLOAD);
        free(pt); free(ct);
        return 1;
    }
    for (size_t i = 0; i < PAYLOAD; i++) { pt[i] = (uint8_t)(i * 31); }

    int ok = 1;
    const double one = median_gbps(key, pt, PAYLOAD, ct, 1, &ok);
    if (!ok) {
        fprintf(stderr, "FAIL: 1-thread run failed\n");
        free(pt); free(ct);
        return 1;
    }

    const double many = median_gbps(key, pt, PAYLOAD, ct, TARGET_THREADS, &ok);
    if (!ok) {
        fprintf(stderr, "FAIL: %d-thread run failed\n", TARGET_THREADS);
        free(pt); free(ct);
        return 1;
    }

    free(pt);
    free(ct);

    if (one <= 0.0) {
        fprintf(stderr, "FAIL: 1-thread throughput reported as %.4f\n", one);
        return 1;
    }

    const double speedup = many / one;
    const double efficiency = speedup / (double)TARGET_THREADS * 100.0;

    printf("  1 thread : %7.3f Gbps  [end-to-end]\n", one);
    printf("  %d threads: %7.3f Gbps  [end-to-end]\n", TARGET_THREADS, many);
    printf("  speedup  : %.2fx  (%.0f%% scaling efficiency)\n", speedup, efficiency);

    if (speedup < MIN_SPEEDUP) {
        fprintf(stderr,
            "\nFAIL P1.7: %.2fx speedup, gate requires >= %.1fx.\n"
            "The CPU baseline is not parallelizing. Likely causes, in order:\n"
            "  1. OpenMP not linked -- check the CMake configure output for the\n"
            "     'OpenMP not found' warning.\n"
            "  2. Thermal or power throttling under all-core load.\n"
            "  3. Memory bandwidth saturation: at %u MiB the working set may\n"
            "     exceed what the memory controller can feed 8 cores.\n"
            "Do NOT record P1.10 baseline results from this build -- an\n"
            "artificially slow CPU baseline invalidates every speedup figure\n"
            "in the final report (acceptance criterion A7).\n",
            speedup, MIN_SPEEDUP, (unsigned)(PAYLOAD >> 20));
        return 1;
    }

    printf("\nPASS P1.7: %.2fx speedup on %d threads\n", speedup, TARGET_THREADS);
    return 0;
}
