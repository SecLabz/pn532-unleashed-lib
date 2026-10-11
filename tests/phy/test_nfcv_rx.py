"""IRQ delivery without polling, with real framing and CRC rejection."""

import ctypes
from pathlib import Path
import platform
import subprocess
import unittest


def crc_frame(payload):
    crc = 0xFFFF
    for byte in payload:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return payload + (crc ^ 0xFFFF).to_bytes(2, "little")


def samples(frame, slow=False):
    symbols = (0xBF, 0xEF, 0xFB, 0xFE)
    output = [0x7E if slow else 0x7B]
    for byte in frame:
        if slow:
            group = [0xFF] * 64
            group[byte // 4] = symbols[byte % 4]
            output.extend(group)
        else:
            output.extend(
                symbols[(byte >> shift) & 3] for shift in (0, 2, 4, 6)
            )
    return bytes([*output, 0xDF])


class NfcvRxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests/nfcv-rx.dylib"
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
                "-D__not_in_flash_func(name)=name",
                "-I" + str(root / "tests/stubs/reader_rx"),
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "src"),
                "-I" + str(root / "include"),
                *(
                    str(root / name)
                    for name in (
                        "tests/phy/nfcv_rx_bridge.c",
                        "src/phy/nfcv/picc/nfcv_rx.c",
                        "src/phy/nfcv/picc/nfcv_rx_decode.c",
                        "src/phy/nfcv/picc/nfcv_frame.c",
                    )
                ),
                "-o",
                str(output),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(output))
        cls.native.host_nfcv_samples.argtypes = (
            ctypes.c_char_p,
            ctypes.c_uint16,
        )
        cls.native.host_nfcv_callbacks.restype = ctypes.c_uint32
        cls.native.host_nfcv_eof_us.restype = ctypes.c_uint32
        cls.native.host_nfcv_rearms.restype = ctypes.c_uint32
        cls.native.host_nfcv_frame.argtypes = (ctypes.c_void_p,)
        cls.native.host_nfcv_frame.restype = ctypes.c_uint16

    def setUp(self):
        self.native.host_nfcv_start()

    def tearDown(self):
        self.native.host_nfcv_stop()

    def feed(self, capture):
        for offset in range(0, len(capture), 8):
            chunk = capture[offset : offset + 8]
            self.native.host_nfcv_samples(chunk, len(chunk))

    def assert_frame(self, frame, callbacks=1):
        self.assertEqual(self.native.host_nfcv_callbacks(), callbacks)
        self.assertEqual(self.native.host_nfcv_eof_us(), 123456)
        output = ctypes.create_string_buffer(64)
        size = self.native.host_nfcv_frame(output)
        self.assertEqual(output.raw[:size], frame)

    def test_both_encodings_dispatch_at_eof_without_foreground_service(self):
        frame = crc_frame(b"\x26\x01\x00")
        for count, slow in enumerate((False, True, False), 1):
            capture = samples(frame, slow)
            self.feed(capture[:-1])
            self.assertEqual(self.native.host_nfcv_callbacks(), count - 1)
            self.feed(capture[-1:])
            self.assert_frame(frame, count)
            self.assertEqual(self.native.host_nfcv_rearms(), count + 1)

    def test_bad_crc_partial_symbol_noise_and_overflow_rearm_silently(self):
        frame = crc_frame(b"\x26\x01\x00")
        for slow in (False, True):
            for capture in (
                samples(frame[:-1] + bytes([frame[-1] ^ 1]), slow),
                bytes([0x7E if slow else 0x7B, 0xBF, 0xDF]),
                bytes([0x00]),
                samples(bytes([0x26]) * 65, slow),
            ):
                with self.subTest(slow=slow, length=len(capture)):
                    self.native.host_nfcv_start()
                    self.feed(capture)
                    self.assertEqual(self.native.host_nfcv_callbacks(), 0)
                    self.feed(samples(frame, slow))
                    self.assert_frame(frame)

    def test_maximum_frame_and_restart_discard_partial_capture(self):
        frame = crc_frame(b"\x22\x23" + bytes(range(60)))
        for slow in (False, True):
            self.native.host_nfcv_start()
            self.feed(samples(frame, slow)[:8])
            self.native.host_nfcv_start()
            self.feed(samples(frame, slow))
            self.assert_frame(frame)
