/* Phase 1 gate tests P1.3, P1.4.
   Exits 77 (the CTest "skipped" convention) when no NVML is present, so this
   binary is runnable on a machine without an NVIDIA GPU without turning the
   phase gate red. On GPU hardware it must actually pass. */

#include "gpuseal/telemetry.h"

#include <stdio.h>
#include <string.h>

#define SKIP_EXIT 77
#define SAMPLE_SECONDS 5.0
#define SAMPLE_HZ 10.0
#define MIN_ROWS 45   /* 5 s at 10 Hz, less 10% slack for scheduler jitter */

static void busy_wait(double seconds)
{
    const double end = gpuseal_now() + seconds;
    while (gpuseal_now() < end) { /* spin: keeps the sampler thread scheduled */ }
}

int main(void)
{
    if (!gpuseal_telemetry_available()) {
        printf("SKIP: built without NVML, no GPU telemetry on this host\n");
        return SKIP_EXIT;
    }

    gpuseal_sampler *s = NULL;
    const gpuseal_status st = gpuseal_telemetry_start(&s, 0, SAMPLE_HZ, "p1-telemetry");
    if (st == GPUSEAL_ERR_UNSUPPORTED) {
        printf("SKIP: NVML present but no device available\n");
        return SKIP_EXIT;
    }
    if (st != GPUSEAL_OK) {
        fprintf(stderr, "FAIL: sampler start: %s\n", gpuseal_strerror(st));
        return 1;
    }

    gpuseal_nvtx_push("p1_telemetry_window");
    busy_wait(SAMPLE_SECONDS);
    gpuseal_nvtx_pop();

    gpuseal_telemetry_stop(s);

    int failures = 0;
    const size_t n = gpuseal_telemetry_count(s);
    printf("collected %zu samples in %.1f s at %.0f Hz\n", n, SAMPLE_SECONDS, SAMPLE_HZ);

    if (n < MIN_ROWS) {
        fprintf(stderr, "FAIL P1.3: expected >= %d rows, got %zu\n", MIN_ROWS, n);
        failures++;
    }

    /* P1.4: power and memory must be real readings, not zeros. */
    const gpuseal_sample *v = gpuseal_telemetry_samples(s);
    size_t zero_power = 0, zero_mem = 0;
    for (size_t i = 0; i < n; i++) {
        if (v[i].power_mw == 0) { zero_power++; }
        if (v[i].mem_total == 0) { zero_mem++; }
    }
    if (zero_power == n && n > 0) {
        fprintf(stderr, "FAIL P1.4: power_w is zero in every sample\n");
        failures++;
    }
    if (zero_mem > 0) {
        fprintf(stderr, "FAIL P1.4: mem_total missing in %zu sample(s)\n", zero_mem);
        failures++;
    }

    if (gpuseal_telemetry_write_csv(s, "telemetry_p1.csv") != GPUSEAL_OK) {
        fprintf(stderr, "FAIL: cannot write telemetry_p1.csv\n");
        failures++;
    }

    gpuseal_telemetry_free(s);

    if (failures) { return 1; }
    printf("telemetry gate passed, wrote telemetry_p1.csv\n");
    return 0;
}
