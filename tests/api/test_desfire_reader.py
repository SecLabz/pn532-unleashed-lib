"""Physical-block contracts; these do not qualify analog RF behavior."""

import ctypes
from pathlib import Path
import platform
import subprocess
import unittest


class DesfireReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests/desfire-reader.dylib"
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
                "-I" + str(root / "src"),
                "-I" + str(root / "include"),
                str(root / "tests/api/desfire_bridge.c"),
                str(root / "src/protocols/nfca/isodep_reader.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(output))
        cls.native.host_desfire_block.argtypes = (
            ctypes.c_void_p,
            ctypes.c_uint8,
            ctypes.c_void_p,
            ctypes.c_uint8,
            ctypes.c_uint8,
            ctypes.c_uint32,
        )
        cls.native.host_desfire_exchange.argtypes = (
            ctypes.c_void_p,
            ctypes.c_uint8,
            ctypes.c_void_p,
            ctypes.c_uint16,
            ctypes.POINTER(ctypes.c_size_t),
            ctypes.POINTER(ctypes.c_uint8),
        )

    def setUp(self):
        self.native.host_desfire_reset()

    def block(self, tx, rx, fault=0, elapsed_us=100):
        self.native.host_desfire_block(
            tx, len(tx), rx, len(rx), fault, elapsed_us
        )

    def begin(self, ats=b"\x02\x02"):
        self.block(b"\xe0\x20", ats)
        return self.native.host_desfire_activate()

    def exchange(self, command=b"\x6e", capacity=1024):
        output = ctypes.create_string_buffer(1024)
        size, status = ctypes.c_size_t(), ctypes.c_uint8()
        result = self.native.host_desfire_exchange(
            command,
            len(command),
            output,
            capacity,
            ctypes.byref(size),
            ctypes.byref(status),
        )
        return result, status.value, output.raw[: size.value]

    def test_ats_validation_and_fwt(self):
        for ats, expected in (
            (b"\x02\x02", 0),
            (b"\x03\x22\x81", 0),
            (b"\x02\x12", 12),
            (b"\x02\x09", 12),
            (b"\x03\x22\xf0", 12),
            (b"\x03\x22\x9f", 12),
            (b"\x03\x22\x90", 9),
            (b"\x03\x42\x80", 12),
            (b"\x04\x02", 12),
        ):
            with self.subTest(ats=ats):
                self.setUp()
                self.assertEqual(self.begin(ats), expected)
        self.setUp()
        self.assertEqual(self.begin(b"\x03\x22\x81"), 0)
        self.assertGreaterEqual(self.native.host_desfire_clock(), 704)
        self.block(b"\x02\x6e", b"\x02\x00\x00\x10\x00")
        self.assertEqual(self.exchange(), (0, 0, b"\0\x10\0"))
        self.assertEqual(self.native.host_desfire_timeout(), 77329)

    def test_chaining_duplicate_blocks_and_native_continuations(self):
        self.assertEqual(self.begin(), 0)
        self.block(b"\x02\x6a", b"\x12\xafabc")
        self.block(b"\xa3", b"\x12\xafabc")  # Duplicate, no second copy.
        self.block(b"\xa3", b"\x03def")
        self.block(b"\x03\xaf", b"\x02\x00ghi")
        self.assertEqual(self.exchange(b"\x6a"), (0, 0, b"abcdefghi"))
        self.assertEqual(self.native.host_desfire_consumed(), 5)

    def test_wtx_requires_valid_multiplier_and_is_bounded(self):
        self.assertEqual(self.begin(), 0)
        self.block(b"\x02\x6e", b"\xf2\x02")
        self.block(b"\xf2\x02", b"\x02\0\1\2\3")
        self.assertEqual(self.exchange(), (0, 0, b"\1\2\3"))
        self.assertEqual(self.native.host_desfire_timeout(), 9668)
        for multiplier in (0, 60, 255):
            self.setUp()
            self.assertEqual(self.begin(), 0)
            self.block(b"\x02\x6e", bytes((0xF2, multiplier)))
            self.assertEqual(self.exchange()[0], 12)
        self.setUp()
        self.begin()
        self.block(b"\x02\x6e", b"\xf2\1")
        for _ in range(8):
            self.block(b"\xf2\1", b"\xf2\1")
        self.assertEqual(self.exchange()[0], 12)

    def test_faults_abort_without_retransmission_or_partial_output(self):
        for fault, expected in ((1, 11), (2, 12), (3, 12), (4, 12)):
            with self.subTest(fault=fault):
                self.setUp()
                self.begin()
                self.block(b"\x02\x6e", b"\x02\0\1\2\3", fault)
                self.assertEqual(self.exchange(), (expected, 0, b""))
                self.assertEqual(self.native.host_desfire_state(), 5)
                self.assertEqual(self.native.host_desfire_consumed(), 2)
                self.assertEqual(self.exchange()[0], 1)

    def test_reject_mutations_and_bad_shapes_before_transmit(self):
        self.begin()
        for command in (
            b"\x0a\0",
            b"\x3d" + bytes(7),
            b"\xaf",
            b"\x60\0",
            b"\x64\x0e",
            b"\xf5\x20",
        ):
            self.assertEqual(self.exchange(command)[0], 1)
        self.assertEqual(self.native.host_desfire_consumed(), 1)
        for response in (b"\x03\0", b"\x0a\0\0", b"\xa2", b"\x12\0"):
            self.setUp()
            self.begin()
            self.block(b"\x02\x6e", response)
            self.assertEqual(self.exchange()[0], 12)

    def test_native_denial_is_not_a_transport_error(self):
        self.begin()
        self.block(b"\x02\x6e", b"\x02\x9d")
        self.assertEqual(self.exchange(), (0, 0x9D, b""))
        self.block(b"\x03\x6e", b"\x03\0\1\2\3")
        self.assertEqual(self.exchange(), (0, 0, b"\1\2\3"))

    def test_no_progress_overflow_and_elapsed_budget(self):
        self.begin()
        self.block(b"\x02\x6e", b"\x02\xaf")
        self.assertEqual(self.exchange()[0], 12)
        self.setUp()
        self.begin()
        self.block(b"\x02\x6e", b"\x02\0\1\2\3")
        self.assertEqual(self.exchange(capacity=2)[0], 13)
        self.setUp()
        self.begin()
        self.block(b"\x02\x6e", b"\xf2\1", elapsed_us=750000)
        self.assertEqual(self.exchange()[0], 12)
        self.assertEqual(self.native.host_desfire_consumed(), 2)

    def test_get_version_requires_three_exact_native_parts(self):
        self.begin()
        self.block(b"\2\x60", b"\2\xaf" + b"H" * 7)
        self.block(b"\3\xaf", b"\3\xaf" + b"S" * 7)
        self.block(b"\2\xaf", b"\2\0" + b"P" * 14)
        self.assertEqual(
            self.exchange(b"\x60"), (0, 0, b"H" * 7 + b"S" * 7 + b"P" * 14)
        )
        for status, size in ((0, 28), (0xAF, 6), (0xAF, 8)):
            self.setUp()
            self.begin()
            self.block(b"\2\x60", bytes((2, status)) + b"x" * size)
            self.assertEqual(self.exchange(b"\x60")[0], 12)

    def test_chained_block_budget_rejects_endless_duplicates(self):
        self.begin()
        self.block(b"\2\x6a", b"\x12\0abc")
        for _ in range(63):
            self.block(b"\xa3", b"\x12\0abc")
        self.assertEqual(self.exchange(b"\x6a")[0], 12)
        self.assertEqual(self.native.host_desfire_consumed(), 65)
