"""Sniff resource cleanup, optional wiring and bounded queue ownership."""

import ctypes
from pathlib import Path
import platform
import subprocess
import unittest
from .test_decode import Frame


class Config(ctypes.Structure):
    _fields_ = [("aux1", ctypes.c_uint8), ("aux2", ctypes.c_uint8)]


class Summary(ctypes.Structure):
    _fields_ = [
        (name, ctypes.c_uint32)
        for name in ("pcd", "picc", "dropped", "overruns", "invalid")
    ]


class ApiTests(unittest.TestCase):
    protocol = "nfca"
    frame_type = Frame
    queue_capacity = 128
    config_type = Config
    invalid_pins = ((0, 0), (0, 28), (29, 1), (8, 1), (16, 1), (30, 1))

    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = root / f".cache/tests/{cls.protocol}-sniff-api.dylib"
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
                "-DPNU_ACTIVITY_LED=1",
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "include"),
                "-I" + str(root / "src"),
                str(
                    root
                    / (
                        "tests/sniff/nfcv_api_bridge.c"
                        if cls.protocol == "nfcv"
                        else "tests/sniff/api_bridge.c"
                    )
                ),
                str(root / f"src/phy/{cls.protocol}/sniff/sniff.c"),
                str(root / "src/board/board_config.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(output))
        if cls.protocol == "nfcv":
            for name in (
                "start",
                "stop",
                "requires_stop",
                "poll",
                "read",
                "summary",
            ):
                setattr(
                    cls.native,
                    f"nfca_sniff_{name}",
                    getattr(cls.native, f"nfcv_sniff_{name}"),
                )
        cls.native.nfca_sniff_start.argtypes = [ctypes.POINTER(cls.config_type)]
        cls.native.nfca_sniff_requires_stop.restype = ctypes.c_bool
        cls.native.nfca_sniff_stop.restype = ctypes.c_bool

    def setUp(self):
        self.native.host_sniff_api_reset()

    def test_invalid_optional_pins_never_touch_hardware(self):
        for pins in self.invalid_pins:
            with self.subTest(pins=pins):
                config = self.config_type(*pins)
                self.assertEqual(
                    self.native.nfca_sniff_start(ctypes.byref(config)), 1
                )
                self.assertEqual(self.native.host_sniff_api_calls(), 0)
        self.assertEqual(self.native.nfca_sniff_start(None), 0)
        self.assertEqual(self.native.nfca_sniff_start(None), 4)

    def test_initial_register_failure_still_requires_explicit_stop(self):
        self.native.host_sniff_api_read_failure(True)
        self.assertEqual(self.native.nfca_sniff_start(None), 6)
        self.assertTrue(self.native.nfca_sniff_requires_stop())
        self.assertTrue(self.native.nfca_sniff_stop())
        self.assertFalse(self.native.nfca_sniff_requires_stop())

    def test_queue_losses_do_not_overwrite_unread_frames(self):
        self.assertEqual(self.native.nfca_sniff_start(None), 0)
        self.native.host_sniff_api_words(132)
        for _ in range(3):
            self.native.nfca_sniff_poll()
        summary = Summary()
        self.assertEqual(
            self.native.nfca_sniff_summary(ctypes.byref(summary)), 0
        )
        self.assertEqual(
            (summary.pcd, summary.dropped), (132, 132 - self.queue_capacity)
        )
        available = ctypes.c_bool()
        frame = self.frame_type()
        for _ in range(self.queue_capacity):
            self.assertEqual(
                self.native.nfca_sniff_read(
                    ctypes.byref(frame), ctypes.byref(available)
                ),
                0,
            )
            self.assertTrue(available.value)
            self.assertEqual(frame.data[0], 0x52)
        frame.data[0] = 0xFF
        self.native.nfca_sniff_read(
            ctypes.byref(frame), ctypes.byref(available)
        )
        self.assertFalse(available.value)
        self.assertEqual(frame.data[0], 0xFF)

    def test_failed_dma_acquisition_restores_registers(self):
        original = [self.native.host_sniff_api_register(i) for i in range(3)]
        self.native.host_sniff_api_capture_failure(True)
        self.assertEqual(self.native.nfca_sniff_start(None), 8)
        self.assertEqual(self.native.nfc_state(), 1)
        self.assertEqual(
            [self.native.host_sniff_api_register(i) for i in range(3)], original
        )
        self.assertFalse(self.native.nfca_sniff_requires_stop())

    def test_failed_restore_cannot_release_ownership(self):
        self.assertEqual(self.native.nfca_sniff_start(None), 0)
        self.native.host_sniff_api_write_failure(True)
        self.assertFalse(self.native.nfca_sniff_stop())
        self.assertTrue(self.native.nfca_sniff_requires_stop())
        self.native.host_sniff_api_write_failure(False)
        self.assertTrue(self.native.nfca_sniff_stop())
        self.assertFalse(self.native.nfca_sniff_requires_stop())

    def test_dma_overrun_enters_error_and_keeps_cleanup_required(self):
        self.assertEqual(self.native.nfca_sniff_start(None), 0)
        self.native.host_sniff_api_overrun()
        self.native.nfca_sniff_poll()
        self.assertEqual(self.native.nfc_state(), 5)
        self.assertTrue(self.native.nfca_sniff_requires_stop())
        summary = Summary()
        self.native.nfca_sniff_summary(ctypes.byref(summary))
        self.assertEqual(summary.overruns, 1)
        self.assertTrue(self.native.nfca_sniff_stop())


class NfcvConfig(ctypes.Structure):
    _fields_ = [("aux1", ctypes.c_uint8)]


class NfcvFrame(ctypes.Structure):
    _fields_ = Frame._fields_[:-1] + [("data", ctypes.c_uint8 * 323)]


class NfcvApiTests(ApiTests):
    protocol = "nfcv"
    frame_type = NfcvFrame
    queue_capacity = 16
    config_type = NfcvConfig
    invalid_pins = ((28,), (29,), (8,), (16,), (30,))

    def test_start_uses_nfcv_state_and_preserves_aux2_selector(self):
        previous_aux2 = self.native.host_sniff_api_register(2) & 15
        self.assertEqual(self.native.nfca_sniff_start(None), 0)
        self.assertEqual(self.native.nfc_state(), 8)
        self.assertEqual(
            self.native.host_sniff_api_register(2) & 15, previous_aux2
        )
        self.assertEqual(self.native.host_sniff_api_register(1), 2)
