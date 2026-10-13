import ctypes
from pathlib import Path
import platform
import shutil
import subprocess
import unittest

from ..support.fixtures import Model
from ..support.fixtures import sample


class SpiPins(ctypes.Structure):
    _fields_ = [
        (name + "_gpio", ctypes.c_uint8)
        for name in ("sck", "miso", "mosi", "cs", "irq")
    ]


class HsuPins(ctypes.Structure):
    _fields_ = [("tx_gpio", ctypes.c_uint8), ("rx_gpio", ctypes.c_uint8)]


class Config(ctypes.Structure):
    _fields_ = [
        ("transport", ctypes.c_int),
        ("sigout_gpio", ctypes.c_uint8),
        ("sigin_gpio", ctypes.c_uint8),
        ("spi", SpiPins),
        ("hsu", HsuPins),
    ]


from ..support.card_state import Image, fixture_view


class Tag(ctypes.Structure):
    _fields_ = [
        ("uid", ctypes.c_uint8 * 8),
        ("dsfid", ctypes.c_uint8),
        ("afi", ctypes.c_uint8),
        ("ic_ref", ctypes.c_uint8),
        ("block_count", ctypes.c_uint8),
        ("block_size_bytes", ctypes.c_uint8),
    ]


class Dump(ctypes.Structure):
    _fields_ = [
        ("tag", Tag),
        ("memory_size_bytes", ctypes.c_uint16),
        ("security", ctypes.c_uint8 * 28),
        ("memory", ctypes.c_uint8 * 112),
    ]


class NfcaDump(ctypes.Structure):
    _fields_ = [
        ("uid", ctypes.c_uint8 * 10),
        ("uid_size_bytes", ctypes.c_uint8),
        ("atqa", ctypes.c_uint8 * 2),
        ("sak", ctypes.c_uint8),
        ("memory_size_bytes", ctypes.c_uint16),
        ("memory", ctypes.c_uint8 * 4096),
        ("metadata_valid", ctypes.c_uint8),
        ("version", ctypes.c_uint8 * 8),
        ("signature", ctypes.c_uint8 * 32),
        ("pack", ctypes.c_uint8 * 2),
        ("counter_valid", ctypes.c_uint8),
        ("counters", ctypes.c_uint8 * 9),
        ("tearing_valid", ctypes.c_uint8),
        ("tearing", ctypes.c_uint8 * 3),
    ]


OK, INVALID_ARGUMENT, INVALID_IMAGE, NOT_INITIALIZED = range(4)
BUSY, WRONG_CLOCK, RF_ERROR, NO_TAG = range(4, 8)
UNINITIALIZED, IDLE, EMULATING_NFCA, EMULATING_NFCV = range(4)
READING_NFCV, ERROR = range(4, 6)


class LibraryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        cls.output = root / ".cache/tests/library"
        cls.output.mkdir(parents=True, exist_ok=True)
        sources = (
            "tests/api/library_bridge.c",
            "src/core.c",
            "tests/api/sniff_stub.c",
            "src/board/resources.c",
            "src/board/board_config.c",
            "src/cards/image.c",
            "src/cards/nfca_access.c",
            "src/cards/nfca_select_validate.c",
            "src/cards/mifare/mifare_access.c",
            "src/cards/mifare/mifare_product.c",
            "src/cards/iso4/iso4_image.c",
            "src/cards/desfire/desfire_capture.c",
            "src/cards/type2/ntag_config.c",
            "src/protocols/nfcv/reader.c",
            "src/protocols/nfcv/reader_api.c",
            "src/protocols/nfcv/read_api.c",
            "src/protocols/nfcv/frame.c",
            "src/protocols/nfca/reader_api.c",
            "src/protocols/nfca/read_api.c",
        )
        for transport in ("SPI", "LOW"):
            subprocess.run(
                [
                    "cc",
                    (
                        "-dynamiclib"
                        if platform.system() == "Darwin"
                        else "-shared"
                    ),
                    "-fPIC",
                    "-std=c11",
                    *(
                        ["-DPNU_REQUIRED_SYS_CLOCK_HZ=54240000u"]
                        if transport == "LOW"
                        else []
                    ),
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I" + str(root / "tests/stubs"),
                    "-I" + str(root / "include"),
                    "-I" + str(root / "src"),
                    *(str(root / path) for path in sources),
                    "-o",
                    str(cls.output / (transport + ".dylib")),
                ],
                check=True,
            )

    def setUp(self):
        # Each test starts with real zero-initialized library state, without
        # adding a production reset API solely for the test harness.
        target = self.output / (self._testMethodName + ".dylib")
        if self._testMethodName.startswith("test_low_"):
            transport = "LOW"
        else:
            transport = "SPI"
        shutil.copyfile(self.output / (transport + ".dylib"), target)
        self.native = ctypes.CDLL(str(target))
        self.native.nfc_init.argtypes = (ctypes.POINTER(Config),)
        self.native.nfc_config_default.restype = Config
        self.native.host_library_pins.argtypes = (ctypes.POINTER(Config),)
        self.buffers = []
        self.native.image_validate.argtypes = (ctypes.POINTER(Image),)
        self.native.image_validate.restype = ctypes.c_int
        self.native.emulation_start.argtypes = (ctypes.POINTER(Image),)
        self.native.nfcv_scan.argtypes = (ctypes.POINTER(Tag),)
        self.native.nfcv_slix_read.argtypes = (ctypes.POINTER(Dump),)
        self.native.host_library_clock.argtypes = (ctypes.c_uint32,)
        self.native.host_library_fail.argtypes = (ctypes.c_bool,) * 3
        self.native.host_reader_config.argtypes = (ctypes.c_uint8,) * 3
        self.native.host_library_count.argtypes = (ctypes.c_uint8,)
        self.native.host_library_count.restype = ctypes.c_uint32
        self.native.host_resources_busy.argtypes = (ctypes.c_uint8,)
        self.native.host_resources_claimed.restype = ctypes.c_uint8

    def image(self, model=Model.CLASSIC_1K):
        image = fixture_view(sample(model))
        self.buffers.append(image)
        self.assertEqual(self.native.image_validate(ctypes.byref(image)), 0)
        return image

    def test_nfca_read_all_requires_selection_and_preserves_other_roles(self):
        dump = NfcaDump(memory_size_bytes=540)
        self.assertEqual(
            self.native.nfca_read_all(None, None), INVALID_ARGUMENT
        )
        self.assertEqual(
            self.native.nfca_read_all(None, ctypes.byref(dump)), BUSY
        )
        self.assertEqual(dump.memory_size_bytes, 0)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(
            self.native.emulation_start(ctypes.byref(self.image())), OK
        )
        dump.memory_size_bytes = 540
        self.assertEqual(
            self.native.nfca_read_all(None, ctypes.byref(dump)), BUSY
        )
        self.assertEqual(dump.memory_size_bytes, 0)
        self.assertEqual(self.native.nfc_state(), EMULATING_NFCA)
        self.assertEqual(self.native.host_library_count(3), 0)

    def test_nfca_read_all_success_keeps_selection_failure_closes_it(self):
        self.assertEqual(self.native.nfc_init(None), OK)
        # Explicit Type 2 identity, independent of discovery stubs.
        tag = (ctypes.c_uint8 * 14)(4, 1, 2, 3, 4, 5, 6, 0, 0, 0, 7, 0x44)
        dump = NfcaDump()
        self.assertEqual(self.native.nfca_select(tag, Model.NTAG215), OK)
        self.assertEqual(
            self.native.nfca_read_all(None, None), INVALID_ARGUMENT
        )
        self.native.host_reader_config(1, 0, 0)
        self.assertEqual(
            self.native.nfca_read_all(None, ctypes.byref(dump)), OK
        )
        self.assertEqual(dump.memory_size_bytes, 540)
        self.assertEqual(self.native.nfc_state(), 6)
        self.native.host_reader_config(1, 1, 0)
        self.assertEqual(
            self.native.nfca_read_all(None, ctypes.byref(dump)), NO_TAG
        )
        self.assertEqual(dump.memory_size_bytes, 0)
        self.assertEqual(self.native.nfc_state(), IDLE)
        self.assertEqual(self.native.nfca_select(tag, Model.ULTRALIGHT), OK)
        self.assertEqual(
            self.native.nfca_read_all(b"1234", ctypes.byref(dump)),
            INVALID_ARGUMENT,
        )
        self.assertEqual(self.native.nfc_state(), IDLE)

    def test_requires_explicit_initialization_without_rf_side_effects(self):
        image = self.image()
        dump = Dump()
        self.assertEqual(self.native.nfc_state(), UNINITIALIZED)
        self.assertEqual(
            self.native.emulation_start(ctypes.byref(image)), NOT_INITIALIZED
        )
        self.assertEqual(
            self.native.nfcv_slix_read(ctypes.byref(dump)), NOT_INITIALIZED
        )
        self.assertEqual(self.native.nfc_stop(), OK)
        for counter in range(5):
            self.assertEqual(self.native.host_library_count(counter), 0)

    def test_low_clock_matches_application_and_rejects_default_clock(self):
        self.native.host_library_clock(135_600_000)
        self.assertEqual(self.native.nfc_init(None), WRONG_CLOCK)
        self.assertEqual(self.native.host_library_count(0), 0)
        self.native.host_library_clock(54_240_000)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.native.nfc_state(), IDLE)
        self.assertEqual(
            self.native.emulation_start(ctypes.byref(self.image())), OK
        )
        self.assertEqual(self.native.nfcv_scan(ctypes.byref(Tag())), BUSY)
        self.assertEqual(self.native.nfc_stop(), OK)

    def test_wrong_clock_is_rejected_before_frontend_access(self):
        self.native.host_library_clock(50_000_000)
        self.assertEqual(self.native.nfc_init(None), WRONG_CLOCK)
        self.assertEqual(self.native.host_library_count(0), 0)
        self.assertEqual(self.native.nfc_state(), UNINITIALIZED)
        self.native.host_library_clock(135_600_000)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.native.host_library_clock(50_000_000)
        image = self.image()
        self.assertEqual(
            self.native.emulation_start(ctypes.byref(image)), WRONG_CLOCK
        )
        self.assertEqual(self.native.host_library_count(1), 0)

    def test_invalid_image_cannot_replace_or_stop_active_emulation(self):
        image = self.image()
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.native.emulation_start(ctypes.byref(image)), OK)
        for name in ("uid", "memory", "memory_size_bytes", "model"):
            invalid = Image.from_buffer_copy(image)
            setattr(invalid, name, 0)
            self.assertNotEqual(
                self.native.image_validate(ctypes.byref(invalid)), 0
            )
            self.assertEqual(
                self.native.emulation_start(ctypes.byref(invalid)),
                INVALID_IMAGE,
            )
            self.assertEqual(self.native.nfc_state(), EMULATING_NFCA)
        self.assertEqual(self.native.host_library_count(1), 1)

    def test_active_role_requires_stop_before_another_operation(self):
        image = self.image(Model.SLIX_L)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.native.emulation_start(ctypes.byref(image)), OK)
        dump = Dump()
        self.assertEqual(self.native.nfc_state(), EMULATING_NFCV)
        self.assertEqual(self.native.nfc_init(None), BUSY)
        self.assertEqual(self.native.emulation_start(ctypes.byref(image)), BUSY)
        self.assertEqual(self.native.nfcv_slix_read(ctypes.byref(dump)), BUSY)
        self.assertEqual(self.native.host_library_count(2), 0)
        self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.nfc_state(), IDLE)
        self.assertEqual(self.native.nfcv_slix_read(ctypes.byref(dump)), OK)

    def test_internal_reader_owner_excludes_public_selection(self):
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.native.nfc_operation_begin(6), OK)
        self.assertEqual(self.native.nfc_operation_require(6), OK)
        self.assertEqual(self.native.nfc_session_require(6), BUSY)
        tag = ctypes.create_string_buffer(14)
        self.assertEqual(self.native.nfca_reader_select(tag), BUSY)
        self.assertEqual(self.native.nfca_reader_start(), BUSY)
        self.assertEqual(self.native.host_public_count(), 0)
        self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.nfca_reader_start(), OK)
        self.assertEqual(self.native.nfc_session_require(6), OK)

    def test_frontend_and_start_failures_require_explicit_recovery(self):
        self.native.host_library_fail(True, False, False)
        self.assertEqual(self.native.nfc_init(None), RF_ERROR)
        self.assertEqual(self.native.nfc_state(), ERROR)
        self.native.host_library_fail(False, False, False)
        self.assertEqual(self.native.nfc_init(None), OK)
        image = self.image()
        self.native.host_library_fail(False, False, True)
        self.assertEqual(
            self.native.emulation_start(ctypes.byref(image)), RF_ERROR
        )
        self.native.host_library_fail(False, True, False)
        self.assertEqual(self.native.nfc_stop(), RF_ERROR)
        self.assertEqual(self.native.nfc_state(), ERROR)
        self.assertEqual(
            self.native.emulation_start(ctypes.byref(image)), RF_ERROR
        )
        self.native.host_library_fail(False, False, False)
        self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.emulation_start(ctypes.byref(image)), OK)

    def test_complete_dump_geometry_and_unused_bytes(self):
        self.assertEqual(self.native.nfc_init(None), OK)
        for model, count in ((1, 28), (3, 8)):
            self.native.host_reader_config(model, 0, 0)
            dump = Dump()
            ctypes.memset(ctypes.byref(dump), 0xA5, ctypes.sizeof(dump))
            self.assertEqual(self.native.nfcv_slix_read(ctypes.byref(dump)), OK)
            self.assertEqual(
                bytes(dump.tag.uid), bytes((0xE0, 4, model, 1, 2, 3, 4, 5))
            )
            self.assertEqual(dump.tag.block_count, count)
            self.assertEqual(dump.tag.block_size_bytes, 4)
            self.assertEqual(dump.memory_size_bytes, count * 4)
            self.assertEqual(
                bytes(dump.security[:count]), bytes(i % 2 for i in range(count))
            )
            self.assertEqual(
                bytes(dump.memory[: count * 4]), bytes(range(count * 4))
            )
            self.assertEqual(bytes(dump.security[count:]), bytes(28 - count))
            self.assertEqual(
                bytes(dump.memory[count * 4 :]), bytes(112 - count * 4)
            )
            self.assertEqual(self.native.nfc_state(), IDLE)
        self.assertEqual(self.native.host_library_count(4), 4)

    def test_scan_checks_system_information_without_reading_memory(self):
        self.assertEqual(self.native.nfc_init(None), OK)
        tag = Tag()
        self.assertEqual(self.native.nfcv_scan(ctypes.byref(tag)), OK)
        self.assertEqual((tag.dsfid, tag.afi, tag.ic_ref), (0x12, 0x34, 0x56))
        self.assertEqual(self.native.host_library_count(3), 0)

    def test_failed_dump_never_publishes_partial_or_stale_data(self):
        self.assertEqual(self.native.nfc_init(None), OK)
        for fault, status in (
            (1, NO_TAG),
            (2, RF_ERROR),
            (3, NO_TAG),
            (4, RF_ERROR),
            (5, RF_ERROR),
        ):
            self.native.host_reader_config(1, fault, 3)
            dump = Dump()
            ctypes.memset(ctypes.byref(dump), 0xA5, ctypes.sizeof(dump))
            dump.memory_size_bytes = 0
            original = bytes(dump)
            dump.memory_size_bytes = 112
            self.assertEqual(
                self.native.nfcv_slix_read(ctypes.byref(dump)), status
            )
            self.assertEqual(dump.memory_size_bytes, 0)
            self.assertEqual(bytes(dump), original)
            self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.host_library_count(4), 10)

    def test_null_outputs_do_not_start_rf(self):
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.native.nfcv_scan(None), INVALID_ARGUMENT)
        self.assertEqual(self.native.nfcv_slix_read(None), INVALID_ARGUMENT)
        self.assertEqual(self.native.host_library_count(2), 0)

    def test_resource_conflicts_preserve_other_owners_and_allow_retry(self):
        for mask in (1, 2, 4, 0x10, 0x20, 0x40):
            self.native.host_resources_busy(mask)
            self.assertEqual(self.native.nfc_init(None), 8)
            self.assertEqual(self.native.nfc_state(), UNINITIALIZED)
            self.assertEqual(self.native.host_resources_claimed(), mask & 15)
            self.assertEqual(self.native.host_library_count(0), 0)
        self.native.host_resources_busy(0)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.native.host_resources_claimed(), 7)
        self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.host_resources_claimed(), 7)
        self.assertEqual(self.native.nfc_init(None), OK)

    def test_spi_does_not_claim_the_unused_hsu_rx_state_machine(self):
        self.native.host_resources_busy(8)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.native.host_resources_claimed(), 15)

    def test_hsu_requires_both_control_state_machines(self):
        config = self.native.nfc_config_default()
        config.transport = 1
        self.native.host_resources_busy(8)
        self.assertEqual(self.native.nfc_init(ctypes.byref(config)), 8)
        self.assertEqual(self.native.host_resources_claimed(), 8)
        self.assertEqual(self.native.host_library_count(0), 0)
        self.native.host_resources_busy(0)
        self.assertEqual(self.native.nfc_init(ctypes.byref(config)), OK)
        self.assertEqual(self.native.host_resources_claimed(), 15)

    def test_reader_setup_failure_releases_role_without_transmitting(self):
        self.assertEqual(self.native.nfc_init(None), OK)
        for fault in (6, 7):
            self.native.host_reader_config(1, fault, 0)
            dump = Dump()
            ctypes.memset(ctypes.byref(dump), 0xA5, ctypes.sizeof(dump))
            dump.memory_size_bytes = 0
            original = bytes(dump)
            dump.memory_size_bytes = 112
            self.assertEqual(
                self.native.nfcv_slix_read(ctypes.byref(dump)), RF_ERROR
            )
            self.assertEqual(dump.memory_size_bytes, 0)
            self.assertEqual(bytes(dump), original)
            self.assertEqual(self.native.host_library_count(2), 0)
            self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.host_library_count(4), 4)


if __name__ == "__main__":
    unittest.main()
