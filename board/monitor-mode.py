#!/usr/bin/python3
"""Select a tested HDMI timing from the raw sink EDID read by the HiSilicon BSP."""
import re
import sys
from pathlib import Path

QHD = (2560, 1440, 2720, 1481, 24150)

def detailed_timings(block, offset, end):
    for i in range(offset, end - 17, 18):
        d = block[i:i+18]
        clock = int.from_bytes(d[:2], 'little')
        if not clock or d[17] & 0x80:
            continue
        h = d[2] + ((d[4] & 0xf0) << 4)
        v = d[5] + ((d[7] & 0xf0) << 4)
        ht = h + d[3] + ((d[4] & 15) << 8)
        vt = v + d[6] + ((d[7] & 15) << 8)
        yield h, v, ht, vt, clock

def select_mode(proc_text):
    data = proc_text.partition('EDID Raw Data')[2]
    rows = re.findall(r'(?m)^\s*((?:[0-9a-fA-F]{2}[ \t]+){15}[0-9a-fA-F]{2})[ \t]*$', data)
    edid = bytes.fromhex(' '.join(rows))
    if len(edid) < 128 or edid[:8] != bytes.fromhex('00 ff ff ff ff ff ff 00'):
        return '1080'
    if len(edid) != 128 * (1 + edid[126]):
        return '1080'
    blocks = [edid[i:i+128] for i in range(0,len(edid),128)]
    if any(sum(b) % 256 for b in blocks):
        return '1080'
    # A preferred 1080p panel may also advertise a larger downscaled input.
    preferred = next(detailed_timings(blocks[0], 54, 72), None)
    if blocks[0][24] & 2 and preferred and preferred[:2] == (1920, 1080):
        return '1080'
    timings = list(detailed_timings(blocks[0], 54, 126))
    for b in blocks[1:]:
        if b[0] == 2 and 4 <= b[2] <= 109:
            timings.extend(detailed_timings(b, b[2], 127))
    return '1440' if QHD in timings else 'auto'

if __name__ == '__main__':
    src = Path(sys.argv[1] if len(sys.argv) > 1 else '/proc/msp/hdmi0_sink')
    try:
        print(select_mode(src.read_text(errors='replace')))
    except OSError:
        print('1080')
