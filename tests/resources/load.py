import hashlib, json, os, re, statistics, subprocess, sys, time
from pathlib import Path


def worker(core):
    os.sched_setaffinity(0, {core})
    b = bytes(1024 * 1024)
    t = time.monotonic()
    chunks = 0
    while time.monotonic() - t < 18:
        hashlib.sha256(b).digest()
        chunks += 1
    print(round(chunks / (time.monotonic() - t), 2))


if sys.argv[1] == "worker":
    worker(int(sys.argv[2]))
    raise SystemExit
d = next(Path("/sys/class/devfreq").glob("*gpu"))
p = Path("/sys/devices/system/cpu/cpufreq/policy0")
debug = Path("/sys/kernel/debug/dynamic_debug/control")
load = Path("/run/gpu-load")


def cpu_ticks():
    return [
        list(map(int, line.split()[1:9]))
        for line in Path("/proc/stat").read_text().splitlines()
        if re.match(r"cpu\d+ ", line)
    ]


def uptime():
    return float(Path("/proc/uptime").read_text().split()[0])


cpus = []
gr = None
try:
    start = uptime()
    debug.write_text("func lima_devfreq_get_dev_status +p\n")
    cpus = [
        subprocess.Popen(
            [sys.executable, __file__, "worker", str(core)],
            stdout=subprocess.PIPE,
            text=True,
        )
        for core in sorted(os.sched_getaffinity(0))
    ]
    env = {
        **os.environ,
        "MESA_LOADER_DRIVER_OVERRIDE": "lima",
        "LD_LIBRARY_PATH": "/usr/local/lib/histb-lima/mesa",
    }
    gr = subprocess.Popen(
        [load], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    time.sleep(1)
    a = cpu_ticks()
    low = uptime()
    rows = []
    while gr.poll() is None and len(rows) < 100:
        rows.append(
            [
                int((p / "cpuinfo_cur_freq").read_text()),
                int((d / "cur_freq").read_text()),
            ]
        )
        time.sleep(0.25)
    high = uptime()
    b = cpu_ticks()
    out, err = gr.communicate(timeout=20)
    if gr.returncode:
        raise RuntimeError(err or str(gr.returncode))
    rates = [float(proc.communicate(timeout=10)[0]) for proc in cpus]
    log = subprocess.check_output(["dmesg"], text=True)
    busy = [
        (int(v), int(t))
        for ts, v, t in re.findall(
            r"\[\s*([\d.]+)\].*?lima .*?busy (\d+) total (\d+)", log
        )
        if low < float(ts) < high
    ]
    deltas = [[y - x for x, y in zip(v, w)] for v, w in zip(a, b)]
    print(
        json.dumps(
            dict(
                label=sys.argv[1],
                cpu_busy_percent=[
                    round(100 * (sum(a) - a[3] - a[4]) / sum(a), 1) for a in deltas
                ],
                cpu_kHz=sorted(set(r[0] for r in rows)),
                gpu_Hz=sorted(set(r[1] for r in rows)),
                samples=len(rows),
                gpu_busy_percent=(
                    round(100 * sum(v for v, t in busy) / sum(t for v, t in busy), 1)
                    if busy and sum(t for v, t in busy)
                    else None
                ),
                gpu_samples=len(busy),
                sha256_MiB_per_sec=rates,
                render=out.strip(),
            )
        )
    )
finally:
    debug.write_text("func lima_devfreq_get_dev_status -p\n")
    for proc in cpus + ([gr] if gr else []):
        if proc.poll() is None:
            proc.terminate()
