"""Physical fixed-grid decoding across arbitrary DMA word boundaries."""

import ctypes
from pathlib import Path
import platform
import subprocess
import unittest


class Frame(ctypes.Structure):
    _fields_ = [
        ("start_fc", ctypes.c_uint64),
        ("duration_fc", ctypes.c_uint32),
        ("bits", ctypes.c_uint16),
        ("size", ctypes.c_uint16),
        ("direction", ctypes.c_int),
        ("flags", ctypes.c_uint8),
        ("data", ctypes.c_uint8 * 64),
    ]


def parity(byte):
    return 1 ^ (byte.bit_count() & 1)


def reader(data, short=False, tail=()):
    # ISO14443-A Modified Miller X/Y/Z, 128/fc bits, 32/fc pauses.
    samples = [(1, 0, 0)] * 80
    z, x, y = [0] * 4 + [1] * 12, [1] * 8 + [0] * 4 + [1] * 4, [1] * 16
    envelope = list(z)
    previous = 0
    for byte in data:
        bits = [(byte >> bit) & 1 for bit in range(7 if short else 8)]
        if not short:
            bits.append(parity(byte))
        for bit in bits:
            envelope.extend(x if bit else y if previous else z)
            previous = bit
    for bit in tail:
        envelope.extend(x if bit else y if previous else z)
        previous = bit
    envelope.extend(y if previous else z)
    envelope.extend(y + [1] * 80)
    samples.extend((level, 0, 0) for level in envelope)
    return samples


def tag(data, short=False, corrupt_parity=False):
    bits = []
    for byte in data:
        bits.extend((byte >> bit) & 1 for bit in range(4 if short else 8))
        if not short:
            bits.append(parity(byte) ^ int(corrupt_parity))
    return [(1, bit, 1) for bit in bits for _ in range(16)] + [(1, 0, 0)] * 80


class DecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests/sniff-decode.dylib"
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
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "include"),
                "-I" + str(root / "src"),
                str(root / "tests/sniff/decode_bridge.c"),
                str(root / "src/phy/nfca/sniff/decode.c"),
                str(root / "src/phy/nfca/picc/nfca_frame.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(output))
        cls.native.host_sniff_frame.restype = ctypes.POINTER(Frame)

    def decode(self, samples, stop=True):
        self.native.host_sniff_reset()
        samples += [(1, 0, 0)] * ((-len(samples)) % 32)
        for first in range(0, len(samples), 32):
            words = [
                sum(samples[first + bit][channel] << bit for bit in range(32))
                for channel in range(3)
            ]
            self.native.nfca_sniff_decode_word((ctypes.c_uint32 * 3)(*words))
        if stop:
            self.native.nfca_sniff_decode_stop()
        return [
            self.native.host_sniff_frame(i).contents
            for i in range(self.native.host_sniff_count())
        ]

    def test_wupa_and_atqa_in_one_continuous_capture(self):
        frames = self.decode(reader([0x52], True) + tag([0x44, 0]))
        self.assertEqual(
            [(f.direction, f.bits, bytes(f.data[: f.size])) for f in frames],
            [(0, 7, b"\x52"), (1, 16, b"\x44\0")],
        )
        self.assertTrue(all(f.flags & 1 for f in frames))

    def test_select_keeps_crc_and_parity(self):
        data = bytes.fromhex("93 70 88 04 2c 7a da fe a9")
        frames = self.decode(reader(data) + tag([4, 0xDA, 0x17]))
        self.assertEqual(bytes(frames[0].data[: frames[0].size]), data)
        self.assertEqual(frames[0].flags, 31)
        self.assertEqual(frames[1].flags, 31)

    def test_reader_eof_silence_cannot_supply_missing_data_or_parity(self):
        for prefix, bits, value in (
            ([], 6, 0x26),
            ([], 8, 0x80),
            ([0x93, 0x21], 8, 0x80),
        ):
            with self.subTest(prefix=prefix, bits=bits):
                tail = [(value >> bit) & 1 for bit in range(bits)]
                frames = self.decode(reader(prefix, tail=tail))
                self.assertEqual(len(frames), 1)
                self.assertFalse(frames[0].flags & 1)
                self.assertTrue(frames[0].flags & 32)

    def test_reader_short_and_full_frames_keep_physical_eof(self):
        for value in range(128):
            frames = self.decode(reader([value], short=True))
            self.assertEqual((frames[0].bits, frames[0].data[0]), (7, value))
            self.assertEqual(frames[0].flags, 1)
        for value in range(256):
            frames = self.decode(reader([value]))
            self.assertEqual((frames[0].bits, frames[0].data[0]), (8, value))
            self.assertEqual(frames[0].flags, 7)
        frames = self.decode(reader(bytes(range(32))))
        self.assertEqual(frames[0].flags & 7, 7)
        self.assertEqual(frames[0].bits, 256)

    def test_reader_eof_guard_preserves_accepted_final_one_quarters(self):
        for short, value in ((False, 0), (True, 0x40)):
            for phase in range(3, 11):
                for offset in range(32):
                    with self.subTest(short=short, phase=phase, offset=offset):
                        samples = reader([value], short=short)
                        first = 80 + 16 + (6 if short else 8) * 16
                        samples[first : first + 16] = [(1, 0, 0)] * 16
                        width = 8 if phase == 3 else 7 if phase == 4 else 4
                        samples[first + phase : first + phase + width] = [
                            (0, 0, 0)
                        ] * width
                        frames = self.decode([(1, 0, 0)] * offset + samples)
                        self.assertEqual(frames[0].data[0], value)
                        self.assertEqual(frames[0].flags & 7, 1 if short else 7)

    def test_partial_reader_tail_cannot_be_a_complete_byte_prefix(self):
        for bits in range(1, 8):
            for value in range(1 << bits):
                with self.subTest(bits=bits, value=value):
                    tail = [(value >> bit) & 1 for bit in range(bits)]
                    frames = self.decode(reader([0x93, 0x21], tail=tail))
                    self.assertEqual(len(frames), 1)
                    self.assertFalse(frames[0].flags & 1)
                    self.assertTrue(frames[0].flags & 32)

    def test_field_loss_reacquires_a_new_carrier(self):
        samples = reader([0x52], True) + [(0, 0, 0)] * 10000
        samples += reader([0x26], True)
        frames = self.decode(samples)
        short = [bytes(f.data[: f.size]) for f in frames if f.flags & 1]
        self.assertEqual(short, [b"\x52", b"\x26"])
        self.assertEqual(len(frames), 2)

    def test_native_replies_survive_missing_reader_envelope(self):
        # Independent native SOF must remain visible if SIGOUT is disconnected
        # or its envelope stays low for an entire reader session.
        samples = [(0, 0, 0)] * 160
        for data in ([0x44, 0], [4, 0xDA, 0x17], [0, 0xFE, 0x51]):
            samples += [
                (0, data_bit, active) for _, data_bit, active in tag(data)
            ]
            samples += [(0, 0, 0)] * 160
        frames = self.decode(samples)
        self.assertEqual([f.direction for f in frames], [1, 1, 1])
        self.assertEqual(
            [bytes(f.data[: f.size]) for f in frames],
            [b"\x44\0", b"\x04\xda\x17", b"\0\xfe\x51"],
        )
        self.assertTrue(all(f.flags & 7 == 7 for f in frames))

    def test_native_bit_centers_survive_every_dma_word_alignment(self):
        data = bytes(range(64))
        for offset in range(32):
            with self.subTest(offset=offset):
                frames = self.decode([(1, 0, 0)] * (80 + offset) + tag(data))
                self.assertEqual(len(frames), 1)
                frame = frames[0]
                self.assertEqual(bytes(frame.data[: frame.size]), data)
                self.assertEqual(frame.start_fc, (80 + offset) * 8)
                self.assertEqual(frame.duration_fc, 64 * 9 * 16 * 8)
                self.assertEqual(frame.flags & 7, 7)

    def test_reader_rxactive_noise_is_not_a_tag_reply(self):
        samples = reader(bytes.fromhex("93 20"))
        samples = [(level, 1, int(level == 0)) for level, _, _ in samples]
        frames = self.decode(samples)
        self.assertEqual([f.direction for f in frames], [0])

    def test_short_ack_and_bad_parity_are_not_repaired(self):
        frames = self.decode([(1, 0, 0)] * 80 + tag([0xA], True))
        self.assertEqual(
            (frames[0].bits, frames[0].data[0], frames[0].flags), (4, 0xA, 1)
        )
        frames = self.decode(
            [(1, 0, 0)] * 80 + tag([0x44, 0], corrupt_parity=True)
        )
        self.assertEqual(frames[0].flags & 7, 3)

    def test_partial_reply_has_exact_bits_without_invented_parity(self):
        samples = [(1, 0, 0)] * 80 + [(1, 1, 1)] * 80
        samples += [(1, 0, 0)] * 80
        frames = self.decode(samples)
        self.assertEqual(
            (frames[0].bits, frames[0].size, frames[0].data[0]), (5, 1, 0x1F)
        )
        self.assertEqual(frames[0].flags & 7, 0)

    def test_partial_and_overlong_frames_are_explicit(self):
        frames = self.decode([(1, 0, 0)] * 80 + tag([0] * 72))
        self.assertTrue(frames[0].flags & 32)
        self.assertFalse(frames[0].flags & 1)
        samples = [(1, 0, 0)] * 80 + [(1, 1, 1)] * 48
        self.native.host_sniff_reset()
        for first in range(0, len(samples), 32):
            chunk = samples[first : first + 32]
            words = [
                sum(item[channel] << bit for bit, item in enumerate(chunk))
                for channel in range(3)
            ]
            self.native.nfca_sniff_decode_word((ctypes.c_uint32 * 3)(*words))
        self.native.nfca_sniff_decode_stop()
        # Three bits cannot be advertised as a decoded ACK or response.
        self.assertEqual(self.native.host_sniff_count(), 0)
