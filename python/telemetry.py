"""pynvml reference sampler.

Cross-check for src/telemetry.c: if the C sampler and this disagree on GPU
utilization or power during the same window, the C polling loop is wrong.
Not used in the benchmark path.

    python python/telemetry.py --seconds 5 --hz 10 --out telemetry_ref.csv
"""

import argparse
import csv
import sys
import time

COLUMNS = [
    "timestamp", "run_id", "gpu_util", "mem_used", "mem_total",
    "power_w", "sm_clock", "temp_c", "pcie_tx_mbps", "pcie_rx_mbps",
]


def sample(handle, nvml, t0):
    util = nvml.nvmlDeviceGetUtilizationRates(handle)
    mem = nvml.nvmlDeviceGetMemoryInfo(handle)
    return {
        "timestamp": round(time.monotonic() - t0, 6),
        "gpu_util": util.gpu,
        "mem_used": mem.used,
        "mem_total": mem.total,
        "power_w": nvml.nvmlDeviceGetPowerUsage(handle) / 1000.0,
        "sm_clock": nvml.nvmlDeviceGetClockInfo(handle, nvml.NVML_CLOCK_SM),
        "temp_c": nvml.nvmlDeviceGetTemperature(handle, nvml.NVML_TEMPERATURE_GPU),
        "pcie_tx_mbps": nvml.nvmlDeviceGetPcieThroughput(
            handle, nvml.NVML_PCIE_UTIL_TX_BYTES) / 1000.0,
        "pcie_rx_mbps": nvml.nvmlDeviceGetPcieThroughput(
            handle, nvml.NVML_PCIE_UTIL_RX_BYTES) / 1000.0,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--hz", type=float, default=10.0)
    ap.add_argument("--device", type=int, default=0)
    ap.add_argument("--run-id", default="ref")
    ap.add_argument("--out", default="telemetry_ref.csv")
    args = ap.parse_args()

    try:
        import pynvml as nvml
    except ImportError:
        print("SKIP: pynvml not installed (pip install nvidia-ml-py)", file=sys.stderr)
        return 77

    try:
        nvml.nvmlInit()
    except nvml.NVMLError:
        print("SKIP: no NVIDIA driver on this host", file=sys.stderr)
        return 77

    handle = nvml.nvmlDeviceGetHandleByIndex(args.device)
    period = 1.0 / args.hz
    t0 = time.monotonic()
    rows = []

    while time.monotonic() - t0 < args.seconds:
        tick = time.monotonic()
        row = sample(handle, nvml, t0)
        row["run_id"] = args.run_id
        rows.append(row)
        # Sleep the remainder so the achieved rate stays at --hz.
        time.sleep(max(0.0, period - (time.monotonic() - tick)))

    nvml.nvmlShutdown()

    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)

    print(f"wrote {len(rows)} rows to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
