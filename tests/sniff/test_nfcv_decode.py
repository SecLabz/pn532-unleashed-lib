"""Fixed-grid dual PPM/subcarrier decoding, loss and long-response bounds."""

import ctypes
from pathlib import Path
import platform
import subprocess
import unittest
from .test_api import NfcvFrame
from ..phy.test_nfcv_rx import crc_frame, samples


def reader(data, slow=False):
    # PPM symbols sampled 96/fc from the first pause, every 128/fc.
    return [
        int(bit)
        for byte in samples(data, slow)
        for bit in f"{byte:08b}"
        for _ in range(16)
    ]


def tag(data, eof=True):
    # Each half-bit is 256/fc; active subcarrier is fc/32, 50% duty.
    half_bits = [int(bit) for bit in f"{0x1d:08b}"]
    for byte in data:
        for bit in range(8):
            value = (byte >> bit) & 1
            half_bits.extend((1 - value, value))
    if eof:
        half_bits.extend(int(bit) for bit in f"{0xb8:08b}")
    return [
        int(active and i % 4 < 2) for active in half_bits for i in range(32)
    ]


class NfcvDecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests/nfcv-sniff-decode.dylib"
        output.parent.mkdir(parents=True, exist_ok=True)
        decoder_object = output.with_suffix(".o")
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-fPIC",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "include"),
                "-I" + str(root / "src"),
                "-Dnfcv_find_sof=host_find_sof",
                "-Dnfcv_decode_levels=host_decode_levels",
                "-c",
                str(root / "src/phy/nfcv/sniff/decode.c"),
                "-o",
                str(decoder_object),
            ],
            check=True,
        )
        subprocess.run(
            [
                "cc",
                "-dynamiclib" if platform.system() == "Darwin" else "-shared",
                "-fPIC",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "include"),
                "-I" + str(root / "src"),
                *(
                    str(root / name)
                    for name in (
                        "tests/sniff/nfcv_decode_bridge.c",
                        "src/phy/nfcv/pcd/nfcv_pcd_decode.c",
                        "src/phy/nfcv/picc/nfcv_rx_decode.c",
                        "src/phy/nfcv/picc/nfcv_frame.c",
                    )
                ),
                str(decoder_object),
                "-o",
                str(output),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(output))
        cls.native.host_nfcv_sniff_frame.restype = ctypes.POINTER(NfcvFrame)

    def decode(self, levels, stop=False):
        self.native.host_nfcv_sniff_reset()
        levels = levels + [(1, 0)] * (-len(levels) % 32)
        for first in range(0, len(levels), 32):
            words = [
                sum(levels[first + bit][channel] << bit for bit in range(32))
                for channel in range(2)
            ]
            self.native.nfcv_sniff_decode_word((ctypes.c_uint32 * 3)(*words, 0))
        if stop:
            self.native.nfcv_sniff_decode_stop()
        return [
            self.native.host_nfcv_sniff_frame(i).contents
            for i in range(self.native.host_nfcv_sniff_count())
        ]

    def exchange(self, request, response, shift=0, slow=False):
        return (
            [(1, 0)] * (64 + shift)
            + [(level, 0) for level in reader(request, slow)]
            + [(1, 0)] * 160
            + [(1, level) for level in tag(response)]
            + [(1, 0)] * 192
        )

    def test_dual_at_every_word_boundary_and_both_ppm_codings(self):
        request = crc_frame(b"\x26\x01\x00")
        response = crc_frame(bytes.fromhex("000088a96025530104e0"))
        for slow in (False, True):
            for shift in range(32):
                with self.subTest(slow=slow, shift=shift):
                    frames = self.decode(
                        self.exchange(request, response, shift, slow)
                    )
                    self.assertEqual([f.direction for f in frames], [0, 1])
                    self.assertEqual([f.flags for f in frames], [25, 25])
                    self.assertEqual(
                        [bytes(f.data[: f.size]) for f in frames],
                        [request, response],
                    )
                    self.assertLess(frames[0].start_fc, frames[1].start_fc)

    def test_crc_corruption_keeps_physical_frames(self):
        request = crc_frame(b"\x26\x01\x00")[:-1] + b"\x00"
        response = crc_frame(b"\x00\x00\x01\x02")[:-1] + b"\x00"
        frames = self.decode(self.exchange(request, response))
        self.assertEqual([f.flags for f in frames], [9, 9])

    def test_longest_response_and_oversize_prefix(self):
        request = crc_frame(b"\x22\x23" + bytes.fromhex("88a96025530104e0004f"))
        response = crc_frame(b"\x00" + bytes(range(256)) + bytes(range(64)))
        frames = self.decode(self.exchange(request, response))
        self.assertEqual(frames[-1].size, 323)
        self.assertEqual(frames[-1].flags, 25)
        self.assertEqual(bytes(frames[-1].data[:323]), response)
        frames = self.decode(self.exchange(request, response + b"\x00"))
        self.assertEqual(frames[-1].size, 323)
        self.assertEqual(frames[-1].flags & 33, 32)

    def test_field_off_during_reply_tail_does_not_discard_tag(self):
        request = crc_frame(b"\x26\x01\x00")
        response = crc_frame(bytes.fromhex("000088a96025530104e0"))
        levels = self.exchange(request, response)
        # Carrier disappears after EOF, before the collector's quiet threshold.
        levels[-170:] = [(0, tag_level) for _, tag_level in levels[-170:]]
        frames = self.decode(levels)
        self.assertEqual([f.flags for f in frames], [25, 25])

    def test_stop_and_noise_do_not_invent_complete_frames(self):
        request = crc_frame(b"\x26\x01\x00")
        levels = [(1, 0)] * 64 + [(x, 0) for x in reader(request)[:-128]]
        frames = self.decode(levels, stop=True)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].flags & 33, 32)
        self.assertEqual(self.decode([(1, i % 4 < 2) for i in range(1024)]), [])
        self.assertEqual(self.decode([(0, 0)] * 256, stop=True), [])

    def test_long_unframed_subcarrier_has_bounded_foreground_work(self):
        self.native.host_scan_reset()
        noise = [(1, int(i % 4 < 2)) for i in range(200_000)]
        self.assertEqual(self.decode(noise, stop=True), [])
        self.assertLessEqual(self.native.host_maximum_scan_samples(), 512)

    def test_sof_survives_noise_and_every_search_window_boundary(self):
        response = crc_frame(bytes.fromhex("000088a96025530104e0"))
        for shift in range(256):
            noise = [(1, int(i % 4 < 2)) for i in range(1024 + shift)]
            levels = (
                noise + [(1, level) for level in tag(response)] + [(1, 0)] * 192
            )
            frames = self.decode(levels)
            self.assertEqual(len(frames), 1, shift)
            self.assertEqual(frames[0].flags, 25, shift)
            self.assertEqual(bytes(frames[0].data[: frames[0].size]), response)
            self.assertEqual(frames[0].start_fc, (1024 + shift + 96) * 8)

    def test_existing_reader_response_capacity_is_preserved(self):
        class ReaderResult(ctypes.Structure):
            _fields_ = [
                ("data", ctypes.c_uint8 * 32),
                ("size", ctypes.c_uint8),
            ] + [
                (name, ctypes.c_bool)
                for name in ("sof", "eof", "crc", "framing", "truncated")
            ]

        self.native.nfcv_pcd_decode_level_samples.restype = ctypes.c_bool
        for size in (32, 33):
            response = crc_frame(b"\x00" + bytes(range(size - 3)))
            levels = [0] * 64 + tag(response) + [0] * 192
            levels += [0] * (-len(levels) % 32)
            words = [
                sum(levels[first + bit] << bit for bit in range(32))
                for first in range(0, len(levels), 32)
            ]
            result = ReaderResult()
            valid = self.native.nfcv_pcd_decode_level_samples(
                (ctypes.c_uint32 * len(words))(*words),
                len(levels),
                ctypes.byref(result),
            )
            self.assertEqual(valid, size == 32)
            self.assertEqual(result.size, 32)
            self.assertEqual(bytes(result.data), response[:32])
            self.assertEqual(result.truncated, size == 33)

    def test_back_to_back_exchanges_keep_order_and_reset(self):
        request = crc_frame(b"\x26\x01\x00")
        response = crc_frame(bytes.fromhex("000088a96025530104e0"))
        frames = self.decode(self.exchange(request, response) * 3)
        self.assertEqual([f.direction for f in frames], [0, 1] * 3)
        self.assertEqual([f.flags for f in frames], [25] * 6)
