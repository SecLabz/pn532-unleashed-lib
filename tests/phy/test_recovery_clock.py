"""Production clock wrap extension, checked against independent rational time."""

import ctypes
from pathlib import Path
import platform
import subprocess
import unittest


class RecoveryClockTests(unittest.TestCase):
    def test_pwm_wraps_usb_gaps_and_microsecond_wrap_at_both_clocks(self):
        root = Path(__file__).resolve().parents[2]
        for hz in (54_240_000, 135_600_000):
            output = root / f".cache/tests/recovery-clock-{hz}.dylib"
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
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-D__not_in_flash_func(x)=x",
                    f"-DPNU_REQUIRED_SYS_CLOCK_HZ={hz}",
                    "-I" + str(root / "tests/stubs"),
                    "-I" + str(root / "src"),
                    "-I" + str(root / "include"),
                    str(root / "tests/phy/recovery_clock_bridge.c"),
                    str(root / "src/phy/nfca/pcd/nfca_recovery_clock.c"),
                    "-o",
                    str(output),
                ],
                check=True,
            )
            native = ctypes.CDLL(str(output))
            native.host_recovery_clock_set.argtypes = (
                ctypes.c_uint32,
                ctypes.c_uint16,
            )
            native.nfca_recovery_clock_now.restype = ctypes.c_uint32
            native.nfca_recovery_clock_wait.argtypes = (ctypes.c_uint32,)
            native.nfca_recovery_clock_wait.restype = ctypes.c_bool
            native.host_recovery_clock_advance.argtypes = (
                ctypes.c_bool,
                ctypes.c_bool,
            )
            native.host_recovery_clock_divider.restype = ctypes.c_float
            native.host_recovery_clock_running.restype = ctypes.c_bool
            native.host_recovery_clock_counter_reads.restype = ctypes.c_uint32
            start_us = 0xFFFFFF00
            native.host_recovery_clock_set(start_us, 0)
            native.nfca_recovery_clock_start()
            self.assertEqual(
                native.host_recovery_clock_divider(), hz * 16 / 13_560_000
            )
            # Polling spans short RF loops and multiple full PWM periods during USB gaps.
            total_us = 0
            for elapsed_us in (
                1,
                5,
                500,
                60_000,
                77_330,
                160_000,
                2_000_000,
                10_000_000,
            ):
                total_us += elapsed_us
                ticks = total_us * 339 // 400
                native.host_recovery_clock_set(
                    (start_us + total_us) & 0xFFFFFFFF, ticks & 65535
                )
                self.assertEqual(native.nfca_recovery_clock_now(), ticks)
            self.assertFalse(native.nfca_recovery_clock_wait(ticks))
            self.assertFalse(native.nfca_recovery_clock_wait(ticks + 65537))
            native.host_recovery_clock_advance(True, False)
            self.assertFalse(native.nfca_recovery_clock_wait(ticks + 100))
            native.host_recovery_clock_advance(True, True)
            self.assertTrue(native.nfca_recovery_clock_wait(ticks + 100))
            # A full 16-bit counter period must not become a zero-length wait.
            # Exercise shorter waits that cross the raw counter wrap as well.
            for duration_ticks in (1, 100, 32768, 65536):
                native.host_recovery_clock_advance(False, False)
                native.host_recovery_clock_set(0, 0)
                native.nfca_recovery_clock_start()
                origin_ticks = 65500
                origin_us = origin_ticks * 400 // 339
                native.host_recovery_clock_set(origin_us, origin_ticks)
                self.assertEqual(native.nfca_recovery_clock_now(), origin_ticks)
                native.host_recovery_clock_advance(False, True)
                deadline_ticks = origin_ticks + duration_ticks
                self.assertTrue(native.nfca_recovery_clock_wait(deadline_ticks))
                self.assertGreaterEqual(
                    native.host_recovery_clock_counter_reads(), duration_ticks
                )
                # Supply independent rational time after the simulated wait:
                # the extension must retain the full elapsed PWM period.
                native.host_recovery_clock_advance(False, False)
                native.host_recovery_clock_set(
                    deadline_ticks * 400 // 339, deadline_ticks & 65535
                )
                self.assertEqual(
                    native.nfca_recovery_clock_now(), deadline_ticks
                )
            native.host_recovery_clock_advance(False, False)
            native.nfca_recovery_clock_stop()
            self.assertFalse(native.host_recovery_clock_running())
