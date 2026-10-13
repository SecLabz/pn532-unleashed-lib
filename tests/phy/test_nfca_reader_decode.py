"""Fixed physical grids with adversarial rise counts and word boundaries."""

import ctypes
from pathlib import Path
import platform
import random
import subprocess
import unittest


class Response(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_uint8 * 34),
        ("parity", ctypes.c_uint8 * 34),
        ("bits", ctypes.c_uint16),
        ("framing", ctypes.c_bool),
        ("parity_valid", ctypes.c_bool),
        ("collision", ctypes.c_bool),
        ("sof", ctypes.c_bool),
    ]


class Segment(ctypes.Structure):
    _fields_ = [("duration_fc", ctypes.c_uint16), ("level", ctypes.c_uint8)]


class Waveform(ctypes.Structure):
    _fields_ = [("segments", Segment * 600), ("count", ctypes.c_uint16)]


class ReaderDecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests/nfca-reader-decode.dylib"
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
                "-I" + str(root / "src"),
                "-I" + str(root / "include"),
                str(root / "src/phy/nfca/pcd/nfca_pcd_decode.c"),
                str(root / "src/phy/pcd/reader_tx_waveform.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(output))
        cls.native.nfca_pcd_decode.argtypes = (
            ctypes.c_void_p,
            ctypes.c_uint16,
            ctypes.c_uint16,
            ctypes.POINTER(Response),
        )

    def decode(self, halves, max_bits=272):
        levels = [level for half in halves for level in half]
        words = (ctypes.c_uint32 * ((len(levels) + 31) // 32))()
        for index, level in enumerate(levels):
            words[index // 32] |= level << (index % 32)
        result = Response()
        self.native.nfca_pcd_decode(
            words, len(levels), max_bits, ctypes.byref(result)
        )
        return result

    @staticmethod
    def pulse_half(rises):
        return [int(index in rises) for index in range(64)]

    def test_two_rises_across_words_and_dense_pulses_preserve_frames(self):
        rng = random.Random(532)
        for size in (1, 4, 16, 34):
            for rises in ((2, 34), (30, 32), tuple(range(2, 64, 2))):
                data = rng.randbytes(size)
                active = self.pulse_half(rises)
                quiet = [0] * 64
                halves = [active, quiet]
                for value in data:
                    bits = [(value >> bit) & 1 for bit in range(8)]
                    bits.append(int(value.bit_count() % 2 == 0))
                    for bit in bits:
                        halves.extend(
                            [active, quiet] if bit else [quiet, active]
                        )
                result = self.decode(halves + [quiet, quiet])
                self.assertTrue(result.framing and result.parity_valid)
                self.assertEqual(result.bits, size * 8)
                self.assertEqual(bytes(result.data[:size]), data)
                self.assertFalse(self.decode(halves).framing)
                self.assertFalse(
                    self.decode(halves + [quiet, quiet], 4).framing
                )

    def test_single_rises_are_quiet_and_initial_anchor_counts_once(self):
        active = self.pulse_half((2, 34))
        quiet = [0] * 64
        one = self.pulse_half((34,))
        sof = self.pulse_half((34,))
        halves = [sof, one] + [one, active] * 4 + [one, quiet]
        result = self.decode(halves, 4)
        self.assertTrue(result.framing and result.sof)
        self.assertEqual((result.bits, result.data[0]), (4, 0))
        bad_sof = self.decode([quiet, quiet] + halves[2:], 4)
        self.assertFalse(bad_sof.framing or bad_sof.sof)
        collision = self.decode(
            [active, quiet, active, active, quiet, quiet], 4
        )
        self.assertTrue(collision.collision)
        self.assertFalse(collision.framing)

    def test_dma_slots_preserve_levels_duration_and_bounded_final_carrier(self):
        native = self.native.reader_tx_waveform_slots
        native.argtypes = (
            ctypes.POINTER(Waveform),
            ctypes.c_uint16,
            ctypes.c_void_p,
            ctypes.c_uint16,
            ctypes.POINTER(ctypes.c_uint16),
        )
        native.restype = ctypes.c_bool
        waveform = Waveform(count=3)
        for tick in (1, 3, 32, 128, 511):
            waveform.segments[:3] = (
                Segment(3 * tick, 1),
                Segment(2 * tick, 0),
                Segment(tick, 1),
            )
            slots = (ctypes.c_uint32 * 9)(*[0xDEADBEEF] * 9)
            count = ctypes.c_uint16(99)
            self.assertTrue(
                native(
                    ctypes.byref(waveform), tick, slots, 7, ctypes.byref(count)
                )
            )
            self.assertEqual(count.value, 6)
            self.assertEqual(
                list(slots), [1, 1, 1, 0, 0, 1, 1, 0xDEADBEEF, 0xDEADBEEF]
            )
            slots[:] = [0xDEADBEEF] * 9
            self.assertFalse(
                native(
                    ctypes.byref(waveform), tick, slots, 6, ctypes.byref(count)
                )
            )
            self.assertEqual(list(slots[6:]), [0xDEADBEEF] * 3)
        for invalid in (0, 31, 33):
            self.assertFalse(
                native(
                    ctypes.byref(waveform),
                    invalid,
                    slots,
                    9,
                    ctypes.byref(count),
                )
            )
        waveform.segments[0].level = 2
        self.assertFalse(
            native(ctypes.byref(waveform), 1, slots, 9, ctypes.byref(count))
        )
        waveform.count = 601
        self.assertFalse(
            native(ctypes.byref(waveform), 1, slots, 9, ctypes.byref(count))
        )
