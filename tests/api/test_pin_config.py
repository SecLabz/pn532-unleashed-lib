"""GPIO validation and immutable initialization/recovery contracts."""

import ctypes
import unittest

from ..api import test_library as library_tests
from ..api.test_library import (
    Config,
    OK,
    INVALID_ARGUMENT,
    WRONG_CLOCK,
    RF_ERROR,
    BUSY,
    UNINITIALIZED,
    IDLE,
)


class PinConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        library_tests.LibraryTests.setUpClass.__func__(cls)

    setUp = library_tests.LibraryTests.setUp
    image = library_tests.LibraryTests.image

    def observed_pins(self):
        pins = Config()
        self.native.host_library_pins(ctypes.byref(pins))
        return bytes(pins)

    def custom_pins(self, transport=0):
        pins = self.native.nfc_config_default()
        pins.transport = transport
        pins.sigout_gpio = 0
        pins.sigin_gpio = 1
        pins.spi.sck_gpio = 2
        pins.spi.miso_gpio = 3
        pins.spi.mosi_gpio = 4
        pins.spi.cs_gpio = 5
        pins.spi.irq_gpio = 6
        pins.hsu.tx_gpio = 2
        pins.hsu.rx_gpio = 3
        return pins

    def test_defaults_preserve_reference_wiring(self):
        default = self.native.nfc_config_default()
        expected = bytes(default)
        self.assertEqual(default.transport, 0)
        self.assertEqual(default.sigout_gpio, 28)
        self.assertEqual(default.sigin_gpio, 29)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.observed_pins(), expected)

    def check_copy_and_retry(self, transport):
        pins = self.custom_pins(transport)
        expected = bytes(pins)
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), OK)
        self.assertEqual(self.observed_pins(), expected)
        pins.sigin_gpio = 7
        self.assertEqual(
            self.native.nfc_init(ctypes.byref(pins)), INVALID_ARGUMENT
        )
        self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.observed_pins(), expected)
        self.assertEqual(self.native.host_library_count(0), 1)
        self.assertEqual(self.native.nfc_state(), IDLE)

    def test_spi_copy_and_retry_preserve_custom_pins(self):
        self.check_copy_and_retry(0)

    def test_hsu_copy_and_retry_preserve_custom_pins(self):
        self.check_copy_and_retry(1)

    def check_invalid(self, transport):
        pins = self.custom_pins(1 if transport == "hsu" else 0)
        fields = [(pins, "sigout_gpio"), (pins, "sigin_gpio")]
        fields += [
            (getattr(pins, transport), name)
            for name, _ in getattr(pins, transport)._fields_
        ]
        for owner, name in fields:
            previous = getattr(owner, name)
            for invalid in (30, 31, 255):
                setattr(owner, name, invalid)
                self.assertEqual(
                    self.native.nfc_init(ctypes.byref(pins)), INVALID_ARGUMENT
                )
                self.assertEqual(self.native.host_library_count(0), 0)
                self.assertEqual(self.native.host_resources_claimed(), 0)
                self.assertEqual(self.native.nfc_state(), UNINITIALIZED)
            setattr(owner, name, previous)
        for i, (owner, name) in enumerate(fields):
            previous = getattr(owner, name)
            for other, other_name in fields[:i]:
                setattr(owner, name, getattr(other, other_name))
                self.assertEqual(
                    self.native.nfc_init(ctypes.byref(pins)), INVALID_ARGUMENT
                )
            setattr(owner, name, previous)
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), OK)

    def test_spi_rejects_invalid_and_duplicate_active_pins(self):
        self.check_invalid("spi")

    def test_hsu_rejects_invalid_and_duplicate_active_pins(self):
        self.check_invalid("hsu")

    def check_inactive_fields(self, inactive):
        pins = self.custom_pins(1 if inactive == "spi" else 0)
        for name, _ in getattr(pins, inactive)._fields_:
            setattr(getattr(pins, inactive), name, 255)
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), OK)
        for name, _ in getattr(pins, inactive)._fields_:
            setattr(getattr(pins, inactive), name, 0)
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), OK)

    def test_spi_ignores_hsu_fields(self):
        self.check_inactive_fields("hsu")

    def test_hsu_ignores_spi_fields(self):
        self.check_inactive_fields("spi")

    def test_invalid_transport_and_change_after_claim(self):
        pins = self.custom_pins()
        pins.transport = 2
        self.assertEqual(
            self.native.nfc_init(ctypes.byref(pins)), INVALID_ARGUMENT
        )
        self.assertEqual(self.native.host_resources_claimed(), 0)
        pins.transport = 1
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), OK)
        self.assertEqual(self.native.host_selected_transport(), 1)
        pins.transport = 0
        self.assertEqual(
            self.native.nfc_init(ctypes.byref(pins)), INVALID_ARGUMENT
        )
        self.assertEqual(self.native.nfc_stop(), OK)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.native.host_selected_transport(), 1)

    def test_frontend_failure_freezes_pins_and_null_retry_reuses_them(self):
        pins = self.custom_pins()
        expected = bytes(pins)
        self.native.host_library_fail(True, False, False)
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), RF_ERROR)
        self.assertEqual(self.observed_pins(), expected)
        self.assertEqual(self.native.nfc_stop(), OK)
        self.native.host_library_fail(False, False, False)
        pins.sigin_gpio = 7
        self.assertEqual(
            self.native.nfc_init(ctypes.byref(pins)), INVALID_ARGUMENT
        )
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(self.observed_pins(), expected)

    def test_clock_and_resource_failures_do_not_commit_pins(self):
        pins = self.custom_pins()
        self.native.host_library_clock(50_000_000)
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), WRONG_CLOCK)
        self.native.host_library_clock(135_600_000)
        self.native.host_resources_busy(1)
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), 8)
        self.native.host_resources_busy(0)
        self.assertEqual(self.native.nfc_init(None), OK)
        self.assertEqual(
            self.observed_pins(), bytes(self.native.nfc_config_default())
        )

    def test_active_emulation_rejects_pin_changes_without_stopping_rf(self):
        pins = self.custom_pins()
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), OK)
        self.assertEqual(
            self.native.emulation_start(ctypes.byref(self.image())), OK
        )
        pins.sigin_gpio = 7
        self.assertEqual(self.native.nfc_init(ctypes.byref(pins)), BUSY)
        self.assertEqual(self.native.host_library_count(0), 1)
        self.assertEqual(self.native.nfc_stop(), OK)


if __name__ == "__main__":
    unittest.main()
