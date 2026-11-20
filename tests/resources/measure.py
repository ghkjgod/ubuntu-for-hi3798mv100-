import ctypes as C
import json, os, statistics, sys, time
from pathlib import Path

label = sys.argv[1]
policy = Path("/sys/devices/system/cpu/cpufreq/policy0")
gpu = next(Path("/sys/class/devfreq").glob("*gpu"))


def ticks():
    return list(map(int, Path("/proc/stat").read_text().splitlines()[0].split()[1:9]))


def numa():
    return {
        k: int(v.split()[0])
        for k, v in (
            line.split(":", 1)
            for line in Path("/proc/meminfo").read_text().splitlines()
        )
    }


times1 = ticks()
avail = []
clocks = []
gclocks = []
for _ in range(16):
    avail.append(numa()["MemAvailable"])
    clocks.append(int((policy / "scaling_cur_freq").read_text()))
    gclocks.append(int((gpu / "cur_freq").read_text()))
    time.sleep(0.5)
times2 = ticks()
delta = [b - a for a, b in zip(times1, times2)]
data = {
    "label": label,
    "cpu_governor": (policy / "scaling_governor").read_text().strip(),
    "gpu_governor": (gpu / "governor").read_text().strip(),
    "idle_MemAvailable_MiB": round(statistics.median(avail) / 1024, 1),
    "idle_cpu_percent": round(100 * (sum(delta) - delta[3] - delta[4]) / sum(delta), 1),
    "idle_cpu_kHz_median": statistics.median(clocks),
    "idle_gpu_Hz_median": statistics.median(gclocks),
}
pss = {}
for p in Path("/proc").glob("[0-9]*"):
    try:
        name = (p / "comm").read_text().strip()
        v = next(
            int(l.split()[1])
            for l in (p / "smaps_rollup").read_text().splitlines()
            if l.startswith("Pss:")
        )
        pss[name] = pss.get(name, 0) + v
    except (OSError, StopIteration):
        pass
if pss:
    data["process_PSS_MiB"] = round(sum(pss.values()) / 1024, 1)
    data["top_PSS_MiB"] = {
        k: round(v / 1024, 1) for k, v in sorted(pss.items(), key=lambda i: -i[1])[:12]
    }
x = C.CDLL("libX11.so.6")
P = C.c_void_p
U = C.c_ulong
I = C.c_int
N = C.c_uint


def fn(n, r, *a):
    f = getattr(x, n)
    f.restype = r
    f.argtypes = a
    return f


d = fn("XOpenDisplay", P, C.c_char_p)(None)
if not d:
    raise SystemExit("display unavailable")
root = fn("XDefaultRootWindow", U, P)(d)
w = fn("XCreateSimpleWindow", U, P, U, I, I, N, N, N, U, U)(
    d, root, 20, 20, 800, 450, 0, 0, 0
)


class Attr(C.Structure):
    _fields_ = [
        ("bgpix", U),
        ("bg", U),
        ("borderpix", U),
        ("border", U),
        ("bitg", I),
        ("wing", I),
        ("backing", I),
        ("planes", U),
        ("pixel", U),
        ("save", I),
        ("eventmask", C.c_long),
        ("donot", C.c_long),
        ("override", I),
        ("cm", U),
        ("cursor", U),
    ]


a = Attr()
a.override = 1
fn("XChangeWindowAttributes", I, P, U, U, C.POINTER(Attr))(d, w, 1 << 9, C.byref(a))
fn("XMapRaised", I, P, U)(d, w)
fn("XSync", I, P, I)(d, 0)
gc = fn("XCreateGC", P, P, U, U, P)(d, w, 0, None)
put = fn("XSetForeground", I, P, P, U)
rect = fn("XFillRectangle", I, P, U, P, I, I, N, N)
get = fn("XGetImage", P, P, U, I, I, N, N, U, I)
pixel = fn("XGetPixel", U, P, I, I)
destroy = fn("XDestroyImage", I, P)
time.sleep(0.35)
lat = []
misses = 0
for i in range(130):
    color = 0x247A9B if i % 2 else 0x993754
    t = time.monotonic_ns()
    put(d, gc, color)
    rect(d, w, gc, 0, 0, 800, 450)
    im = get(d, w, 394, 220, 1, 1, 0xFFFFFFFF, 2)
    observed = pixel(im, 0, 0)
    destroy(im)
    ms = (time.monotonic_ns() - t) / 1e6
    misses += int(observed != color)
    if i >= 10:
        lat.append(ms)
fn("XCloseDisplay", I, P)(d)
data.update(
    x11_readback_samples=len(lat),
    x11_pixel_errors=misses,
    x11_median_ms=round(statistics.median(lat), 3),
    x11_p95_ms=round(sorted(lat)[113], 3),
)
print(json.dumps(data, sort_keys=True))
