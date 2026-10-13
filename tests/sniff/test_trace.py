"""Bench snapshots stay bounded and cannot be streamed during live RF."""

import ctypes
from pathlib import Path
import platform
import struct
import subprocess
import unittest


class TraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests/sniff-trace.dylib"
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "cc",
                "-dynamiclib" if platform.system() == "Darwin" else "-shared",
                "-fPIC",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DPNU_RF_TRACE=1",
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "include"),
                "-I" + str(root / "src"),
                str(root / "tests/sniff/trace_bridge.c"),
                str(root / "src/phy/nfca/sniff/trace.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(output))
        cls.native.nfc_trace_sniff.restype = ctypes.c_uint16

    def test_four_bursts_are_bounded_and_keep_collecting_counters(self):
        self.native.nfca_sniff_trace_reset()
        self.native.host_sniff_trace_state(1)
        for _ in range(5):
            self.native.host_sniff_trace_words(52970, 0, 0, 0, 0)
            self.native.host_sniff_trace_words(1024, 0, 0xFFFFFFFF, 1, 5)
        buffer = (ctypes.c_uint8 * 1216)(*([0xA5] * 1216))
        size = self.native.nfc_trace_sniff(0, buffer, 1199)
        data = bytes(buffer)
        self.assertEqual(size, 1184)
        self.assertEqual(data[:4], b"SNR1")
        self.assertEqual(data[12], 4)
        self.assertEqual(struct.unpack_from("<4H", data, 72), (1024,) * 4)
        self.assertEqual(struct.unpack_from("<Q", data, 4)[0], 5 * 53994)
        self.assertEqual(
            struct.unpack_from("<4I", data, 80), (0, 0xFFFFFFFF, 1, 5)
        )
        self.assertEqual(data[1199:], b"\xA5" * 17)
        self.assertEqual(self.native.nfc_trace_sniff(4096, buffer, 1199), 80)
        self.assertEqual(self.native.nfc_trace_sniff(4097, buffer, 1199), 0)
        self.assertEqual(self.native.nfc_trace_sniff(0, buffer, 79), 0)
        self.assertEqual(self.native.nfc_trace_sniff(0, None, 1199), 0)
        for active_state in (0, 2, 3, 4, 6, 7):
            self.native.host_sniff_trace_state(active_state)
            self.assertEqual(self.native.nfc_trace_sniff(0, buffer, 1199), 0)
