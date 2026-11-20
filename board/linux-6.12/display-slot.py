"""Operate the qualified kernel partition of this single EC6108V9C."""
import argparse
import fcntl
import hashlib
import os
from pathlib import Path
import struct
import zlib

slot = Path('/dev/mmcblk0p6')
sysblock = Path('/sys/class/block')
recovery = Path('/var/lib/histb-kernel-recovery')
previous_hash = '5b691ef1ae8d0d0ff39074409a50d26e8bb9f792c67488c868d764e09080ce2e'
candidate_hash = '7d5cf2b4d78af6dca412df63c44442c630ed1a32cb1ef3e3713870acdadbea06'
stock_hash = 'e24147d24554d302230cf2d376c6ecddc655441bcc03e523d3c1712ae6ec9547'

def require(ok, message):
    if not ok:
        raise SystemExit(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def validate_image(image):
    header = bytearray(image[:64])
    magic, hcrc, _, length, load, entry, dcrc = struct.unpack_from('>7I', header)
    require((magic, length, load, entry) == (0x27051956, len(image)-64, 0x02000000, 0x02000000), 'Unexpected uImage header')
    header[4:8] = bytes(4)
    require(zlib.crc32(header) == hcrc and zlib.crc32(image[64:]) == dcrc, 'uImage CRC mismatch')

parser = argparse.ArgumentParser()
parser.add_argument('operation', choices=['audit', 'install', 'restore'])
args = parser.parse_args()
require(os.geteuid() == 0 and slot.is_block_device(), 'Expected root on the target board')
require((sysblock/'mmcblk0/device/cid').read_text().strip() == '110100303038473330009474d3adc483', 'Unexpected eMMC identity')
geometry = [int((sysblock/'mmcblk0p6'/k).read_text()) for k in ('partition','start','size')]
require(geometry == [6, 28672, 40960], 'Unexpected partition geometry')
require(not any((sysblock/'mmcblk0p6/holders').iterdir()), 'Partition in use')
dev = (sysblock/'mmcblk0p6/dev').read_text().strip()
require(all(line.split()[2] != dev for line in Path('/proc/self/mountinfo').read_text().splitlines()), 'Partition is mounted')
stock = (recovery/'stock-p6.bin').read_bytes()
previous_image = (recovery/'trial.bin').read_bytes()
candidate = (recovery/'hdmi-uImage').read_bytes()
require(sha(previous_image) == previous_hash, 'Previous image hash mismatch')
require(max(len(previous_image), len(candidate)) <= len(stock), 'Image exceeds kernel partition')
validate_image(previous_image)
require(len(stock) == 40960*512 and sha(stock) == stock_hash and sha(candidate) == candidate_hash, 'Stored image hash mismatch')
validate_image(candidate)
validate_image(stock[:64 + struct.unpack_from('>I',stock,12)[0]])
previous = previous_image + stock[len(previous_image):]
prior_image = (recovery/'hdmi-prior').read_bytes()
require(sha(prior_image) == '80cdda4379d636cb8f63803effaba7e9b9021fac0295d2ae219fbb81e5ad90c8', 'Prior HDMI image hash mismatch')
validate_image(prior_image)
prior = prior_image + previous[len(prior_image):]
before_audio = (recovery/'hdmi-before-audio').read_bytes()
require(sha(before_audio) == '7ab1ceb32adc62eaf9af1c0a5ba97def44a5bf0f905104a145ce50db42e36d1b', 'Earlier HDMI hash mismatch')
validate_image(before_audio)
last = before_audio + previous[len(before_audio):]
audio_image = (recovery/'hdmi-pcm').read_bytes()
require(sha(audio_image) == 'a9af93cb5c1e9697aebb9db7fdb5275b80242fa686462515c33fb0ed06f8f901', 'Previous PCM image hash mismatch')
validate_image(audio_image)
audio = audio_image + previous[len(audio_image):]
replacement = candidate + previous[len(candidate):]
with (slot.open('rb', buffering=0) if args.operation == 'audit' else slot.open('r+b', buffering=0)) as target:
    fcntl.flock(target, fcntl.LOCK_EX)
    current = target.read()
    require(current in (previous, prior, last, audio, replacement), 'The current kernel partition is unknown')
    print('geometry',geometry,'current',sha(current),'candidate',candidate_hash, flush=True)
    if args.operation == 'audit':
        raise SystemExit(0)
    wanted = replacement if args.operation == 'install' else previous
    if current == wanted:
        print('Already installed')
        raise SystemExit(0)
    if args.operation == 'install':
        require(os.uname().release == '6.12.114-hi3798-mv100-exp' and Path('/sys/class/drm/card0-HDMI-A-1').exists(), 'Run the RAM trial first')
    target.seek(0)
    # Both known partition states have identical bytes after this prefix.
    length = max(len(candidate), len(previous_image), len(prior_image), len(before_audio), len(audio_image))
    written = target.write(wanted[:length])
    os.fsync(target.fileno())
    require(written == length, 'Short kernel write, keep the board running')
    target.seek(0)
    require(target.read(length) == wanted[:length], 'Kernel prefix readback mismatch')
    print('verified prefix',written,sha(wanted[:length]),flush=True)
