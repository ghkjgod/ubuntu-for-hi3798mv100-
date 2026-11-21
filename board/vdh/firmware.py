#!/usr/bin/env python3
"""Prepare the two CABAC initialization tables used by MV100 VDH."""
import argparse
import hashlib
import re
import struct
import tempfile
import urllib.request
from pathlib import Path

SHA = {
    "h264": "0f5b1a41f7d2ab21f1bd5ffc7be9a3e4777cac1c81cc15f53cc6cd12998a626e",
    "hevc": "eb04288c3fa644565f9d71931918cf27f02022ffb77b4343383b80d2dcfff972",
}
REV = "2267974fe56de7dfdd443687c3279d1efda410cb"
ROOT = "https://raw.githubusercontent.com/qwec01/Hi3798cv200_firmware/" + REV + "/hisilicon/"


def words(path, label, count):
    text = path.read_text(encoding="ascii", errors="replace").split("\n" + label + ":\n", 1)[1]
    values = []
    for line in text.splitlines():
        match = re.fullmatch(r"\s*\.word\s+(-?\d+)\s*", line)
        if not match:
            break
        values.append(int(match[1]))
        if len(values) == count:
            return values
    raise ValueError(f"unexpected table layout: {path} {label}")


def extract(sdk, codec):
    base = sdk / "source/msp/drv/vfmw/vfmw_v4.0/firmware/product/Hi3798MV100/HD_FULL/32bit"
    if codec == "h264":
        table = words(base / "common/syntax/h264.S", "g_CabacMN", 1280)
        return b"".join(struct.pack("<I", v & 0xffffffff) for v in table)
    table = words(base / "driver/HiVDHV300R001/vdm_hal_hevc.S", "s_InitValue", 480)
    return bytes(x & 255 for v in table[16:] for x in
                 ((0, 0) if v == -1 else ((v & 15) * 8 - 16, (v >> 4) * 5 - 45)))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("output", type=Path)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--sdk", type=Path)
    src.add_argument("--from-dir", type=Path)
    args = ap.parse_args()
    prepared = {}
    for codec, digest in SHA.items():
        name = f"mv100-{codec}-cabac.bin"
        if args.sdk:
            data = extract(args.sdk, codec)
        elif args.from_dir:
            data = (args.from_dir / name).read_bytes()
        else:
            with urllib.request.urlopen(ROOT + f"histb-{codec}-cabac.bin", timeout=20) as response:
                data = response.read(8192)
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("unexpected SHA-256 for " + name)
        prepared[name] = data
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in prepared.items():
        with tempfile.NamedTemporaryFile(dir=args.output, delete=False) as f:
            f.write(data)
            tmp = Path(f.name)
        tmp.chmod(0o644)
        tmp.replace(args.output / name)


if __name__ == "__main__":
    main()
