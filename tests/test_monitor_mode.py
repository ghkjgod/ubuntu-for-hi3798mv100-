import importlib.util
import unittest
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "board" / "monitor-mode.py"
spec = importlib.util.spec_from_file_location("monitor_mode", path)
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)

def proc_data(*blocks):
    return "EDID Raw Data\n" + "\n".join(
        block[i:i+16].hex(" ") for block in blocks for i in range(0, 128, 16)
    )

def checksum(block):
    block[-1] = -sum(block[:-1]) & 255
    return block

def base_block():
    base = bytearray(128)
    base[:8] = bytes.fromhex("00 ff ff ff ff ff ff 00")
    base[18:20] = bytes([1, 4])
    return base

def qhd_dtd():
    # 241.5 MHz, 2560+160 horizontal, 1440+41 vertical.
    return bytes.fromhex("565e00a0a0a029503020350000000000001a")

class MonitorTest(unittest.TestCase):
    def test_base_preferred_mode(self):
        base = base_block()
        base[54:72] = qhd_dtd()
        self.assertEqual(monitor.select_mode(proc_data(checksum(base))), "1440")

    def test_cta_detailed_timing(self):
        base, cta = base_block(), bytearray(128)
        base[126] = 1
        cta[:4] = bytes([2, 3, 4, 0])
        cta[4:22] = qhd_dtd()
        self.assertEqual(monitor.select_mode(proc_data(checksum(base), checksum(cta))), "1440")
        self.assertEqual(monitor.select_mode(proc_data(base)), "1080")

    def test_native_1080p_with_optional_scaled_qhd(self):
        base = base_block()
        base[24] = 2
        base[54:72] = bytes.fromhex("023a801871382d40582c4500fd1e1100001e")
        base[72:90] = qhd_dtd()
        self.assertEqual(monitor.select_mode(proc_data(checksum(base))), "1080")

    def test_wrong_clock_bad_checksum_and_empty_receiver(self):
        base = base_block()
        base[54:72] = qhd_dtd()
        base[54] ^= 1
        self.assertEqual(monitor.select_mode(proc_data(checksum(base))), "auto")
        base[20] ^= 1
        self.assertEqual(monitor.select_mode(proc_data(base)), "1080")
        self.assertEqual(monitor.select_mode(proc_data(bytearray(128))), "1080")
