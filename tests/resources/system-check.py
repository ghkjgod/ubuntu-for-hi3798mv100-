import hashlib, json, os, pathlib, tempfile, time, sys

if sys.argv[1] == "pressure":
    cg = pathlib.Path("/sys/fs/cgroup") / pathlib.Path(
        "/proc/self/cgroup"
    ).read_text().strip().split("::")[1].lstrip("/")
    data = bytearray(78 * 1024**2)
    block = hashlib.sha256(b"EC6108").digest() * 128
    for i in range(0, len(data), 4096):
        data[i : i + 4096] = block
        data[i : i + 4] = (i // 4096).to_bytes(4, "little")
    result = {
        k: (cg / k).read_text().strip()
        for k in ("memory.current", "memory.swap.current", "memory.events")
    }
    result["zram_mm_stat"] = (
        pathlib.Path("/sys/block/zram0/mm_stat").read_text().strip()
    )
    for i in range(0, len(data), 4096):
        assert (
            data[i : i + 4] == (i // 4096).to_bytes(4, "little")
            and data[i + 4 : i + 4096] == block[4:]
        )
    print(json.dumps(result))
elif sys.argv[1] == "storage":
    for where in ["/var/tmp", "/srv/tf"]:
        sample = os.urandom(1024**2)
        want = hashlib.sha256(sample * 16).digest()
        with tempfile.NamedTemporaryFile(dir=where) as f:
            t = time.monotonic()
            for _ in range(16):
                f.write(sample)
            f.flush()
            os.fsync(f.fileno())
            w = time.monotonic() - t
            import subprocess

            t = time.monotonic()
            sha = hashlib.sha256(
                subprocess.check_output(
                    ["dd", "if=" + f.name, "iflag=direct", "bs=1M", "status=none"]
                )
            )
            r = time.monotonic() - t
            assert sha.digest() == want
            print(
                where,
                "16 MiB fsync write / read+SHA256 MiB/s",
                round(16 / w, 2),
                round(16 / r, 2),
                "verified",
            )

else:
    raise SystemExit("Usage: system-check.py pressure|storage")
