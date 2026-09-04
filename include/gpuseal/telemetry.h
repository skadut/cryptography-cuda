#ifndef GPUSEAL_TELEMETRY_H
#define GPUSEAL_TELEMETRY_H

#include "gpuseal/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double   t;             /* seconds since sampler start */
    unsigned gpu_util;      /* percent */
    unsigned mem_util;      /* percent */
    unsigned long long mem_used;
    unsigned long long mem_total;
    unsigned power_mw;
    unsigned sm_clock_mhz;
    unsigned temp_c;
    unsigned pcie_tx_kbps;
    unsigned pcie_rx_kbps;
} gpuseal_sample;

typedef struct gpuseal_sampler gpuseal_sampler;

/* Returns GPUSEAL_ERR_UNSUPPORTED when NVML is absent (no GPU on this host).
   Callers in phase 1 treat that as a SKIP, not a failure. */
gpuseal_status gpuseal_telemetry_start(gpuseal_sampler **out,
                                       unsigned device_index,
                                       double hz,
                                       const char *run_id);

gpuseal_status gpuseal_telemetry_stop(gpuseal_sampler *s);

size_t gpuseal_telemetry_count(const gpuseal_sampler *s);
const gpuseal_sample *gpuseal_telemetry_samples(const gpuseal_sampler *s);

/* Writes the tidy CSV described in phase 1:
   timestamp,run_id,gpu_util,mem_used,mem_total,power_w,sm_clock,temp_c,pcie_tx_mbps,pcie_rx_mbps */
gpuseal_status gpuseal_telemetry_write_csv(const gpuseal_sampler *s, const char *path);

void gpuseal_telemetry_free(gpuseal_sampler *s);

int gpuseal_telemetry_available(void);

/* NVTX ranges. No-ops when NVTX is not compiled in, so call sites stay clean. */
void gpuseal_nvtx_push(const char *name);
void gpuseal_nvtx_pop(void);

#ifdef __cplusplus
}
#endif
#endif
