#include "gpuseal/telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(GPUSEAL_HAVE_NVML)
#include <nvml.h>
#endif

#if defined(GPUSEAL_HAVE_NVTX)
#include <nvtx3/nvToolsExt.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
typedef HANDLE gpuseal_thread;
#else
#include <pthread.h>
#include <time.h>
typedef pthread_t gpuseal_thread;
#endif

#define GPUSEAL_RUN_ID_MAX 64

struct gpuseal_sampler {
    gpuseal_sample *buf;
    size_t          n;
    size_t          cap;
    double          period;
    unsigned        device_index;
    char            run_id[GPUSEAL_RUN_ID_MAX];
    volatile int    stop;
    gpuseal_thread  thread;
    int             running;
#if defined(GPUSEAL_HAVE_NVML)
    nvmlDevice_t    dev;
#endif
};

static void sleep_seconds(double s)
{
    if (s <= 0.0) { return; }
#if defined(_WIN32)
    Sleep((DWORD)(s * 1000.0));
#else
    struct timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
#endif
}

static int push_sample(struct gpuseal_sampler *s, const gpuseal_sample *smp)
{
    if (s->n == s->cap) {
        const size_t cap = s->cap ? s->cap * 2 : 1024;
        gpuseal_sample *p = (gpuseal_sample *)realloc(s->buf, cap * sizeof(*p));
        if (!p) { return 0; }
        s->buf = p;
        s->cap = cap;
    }
    s->buf[s->n++] = *smp;
    return 1;
}

#if defined(GPUSEAL_HAVE_NVML)
static void poll_once(struct gpuseal_sampler *s, double t0)
{
    gpuseal_sample smp;
    memset(&smp, 0, sizeof(smp));
    smp.t = gpuseal_now() - t0;

    nvmlUtilization_t util;
    if (nvmlDeviceGetUtilizationRates(s->dev, &util) == NVML_SUCCESS) {
        smp.gpu_util = util.gpu;
        smp.mem_util = util.memory;
    }

    nvmlMemory_t mem;
    if (nvmlDeviceGetMemoryInfo(s->dev, &mem) == NVML_SUCCESS) {
        smp.mem_used  = mem.used;
        smp.mem_total = mem.total;
    }

    nvmlDeviceGetPowerUsage(s->dev, &smp.power_mw);
    nvmlDeviceGetClockInfo(s->dev, NVML_CLOCK_SM, &smp.sm_clock_mhz);
    nvmlDeviceGetTemperature(s->dev, NVML_TEMPERATURE_GPU, &smp.temp_c);
    nvmlDeviceGetPcieThroughput(s->dev, NVML_PCIE_UTIL_TX_BYTES, &smp.pcie_tx_kbps);
    nvmlDeviceGetPcieThroughput(s->dev, NVML_PCIE_UTIL_RX_BYTES, &smp.pcie_rx_kbps);

    push_sample(s, &smp);
}
#endif

#if defined(_WIN32)
static unsigned __stdcall sampler_main(void *arg)
#else
static void *sampler_main(void *arg)
#endif
{
    struct gpuseal_sampler *s = (struct gpuseal_sampler *)arg;
#if defined(GPUSEAL_HAVE_NVML)
    const double t0 = gpuseal_now();
    while (!s->stop) {
        const double tick = gpuseal_now();
        poll_once(s, t0);
        /* Sleep the remainder of the period so sampling rate stays at hz even
           when a poll is slow. P1.3 checks the achieved rate. */
        const double spent = gpuseal_now() - tick;
        sleep_seconds(s->period - spent);
    }
#else
    (void)s;
#endif
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

int gpuseal_telemetry_available(void)
{
#if defined(GPUSEAL_HAVE_NVML)
    return 1;
#else
    return 0;
#endif
}

gpuseal_status gpuseal_telemetry_start(gpuseal_sampler **out,
                                       unsigned device_index,
                                       double hz,
                                       const char *run_id)
{
    if (!out || hz <= 0.0) { return GPUSEAL_ERR_ARG; }
    *out = NULL;

#if !defined(GPUSEAL_HAVE_NVML)
    (void)device_index; (void)run_id;
    return GPUSEAL_ERR_UNSUPPORTED;
#else
    struct gpuseal_sampler *s = (struct gpuseal_sampler *)calloc(1, sizeof(*s));
    if (!s) { return GPUSEAL_ERR_ALLOC; }
    s->period = 1.0 / hz;
    s->device_index = device_index;
    if (run_id) {
        strncpy(s->run_id, run_id, GPUSEAL_RUN_ID_MAX - 1);
        s->run_id[GPUSEAL_RUN_ID_MAX - 1] = '\0';
    }

    if (nvmlInit_v2() != NVML_SUCCESS) { free(s); return GPUSEAL_ERR_UNSUPPORTED; }
    if (nvmlDeviceGetHandleByIndex_v2(device_index, &s->dev) != NVML_SUCCESS) {
        nvmlShutdown();
        free(s);
        return GPUSEAL_ERR_UNSUPPORTED;
    }

#if defined(_WIN32)
    s->thread = (HANDLE)_beginthreadex(NULL, 0, sampler_main, s, 0, NULL);
    if (!s->thread) { nvmlShutdown(); free(s); return GPUSEAL_ERR_ALLOC; }
#else
    if (pthread_create(&s->thread, NULL, sampler_main, s) != 0) {
        nvmlShutdown();
        free(s);
        return GPUSEAL_ERR_ALLOC;
    }
#endif
    s->running = 1;
    *out = s;
    return GPUSEAL_OK;
#endif
}

gpuseal_status gpuseal_telemetry_stop(gpuseal_sampler *s)
{
    if (!s) { return GPUSEAL_ERR_ARG; }
    if (!s->running) { return GPUSEAL_OK; }
    s->stop = 1;
#if defined(_WIN32)
    WaitForSingleObject(s->thread, INFINITE);
    CloseHandle(s->thread);
#else
    pthread_join(s->thread, NULL);
#endif
    s->running = 0;
#if defined(GPUSEAL_HAVE_NVML)
    nvmlShutdown();
#endif
    return GPUSEAL_OK;
}

size_t gpuseal_telemetry_count(const gpuseal_sampler *s) { return s ? s->n : 0; }

const gpuseal_sample *gpuseal_telemetry_samples(const gpuseal_sampler *s)
{
    return s ? s->buf : NULL;
}

gpuseal_status gpuseal_telemetry_write_csv(const gpuseal_sampler *s, const char *path)
{
    if (!s || !path) { return GPUSEAL_ERR_ARG; }
    FILE *f = fopen(path, "w");
    if (!f) { return GPUSEAL_ERR_IO; }

    fprintf(f, "timestamp,run_id,gpu_util,mem_used,mem_total,power_w,"
               "sm_clock,temp_c,pcie_tx_mbps,pcie_rx_mbps\n");
    for (size_t i = 0; i < s->n; i++) {
        const gpuseal_sample *v = &s->buf[i];
        fprintf(f, "%.6f,%s,%u,%llu,%llu,%.3f,%u,%u,%.3f,%.3f\n",
                v->t, s->run_id, v->gpu_util,
                (unsigned long long)v->mem_used, (unsigned long long)v->mem_total,
                (double)v->power_mw / 1000.0, v->sm_clock_mhz, v->temp_c,
                (double)v->pcie_tx_kbps / 1000.0, (double)v->pcie_rx_kbps / 1000.0);
    }
    fclose(f);
    return GPUSEAL_OK;
}

void gpuseal_telemetry_free(gpuseal_sampler *s)
{
    if (!s) { return; }
    gpuseal_telemetry_stop(s);
    free(s->buf);
    free(s);
}

void gpuseal_nvtx_push(const char *name)
{
#if defined(GPUSEAL_HAVE_NVTX)
    nvtxRangePushA(name);
#else
    (void)name;
#endif
}

void gpuseal_nvtx_pop(void)
{
#if defined(GPUSEAL_HAVE_NVTX)
    nvtxRangePop();
#endif
}
