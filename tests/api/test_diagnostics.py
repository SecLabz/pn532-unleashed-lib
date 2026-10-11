"""Public diagnostics validate both settings before any mutation."""

from pathlib import Path
import subprocess
import platform
import unittest


class DiagnosticsTests(unittest.TestCase):
    def test_invalid_and_active_rf_plans_preserve_both_settings(self):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests/diagnostics"
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DPNU_RF_TRACE=1",
                "-D__not_in_flash_func(name)=name",
                "-ffunction-sections",
                "-fdata-sections",
                (
                    "-Wl,-dead_strip"
                    if platform.system() == "Darwin"
                    else "-Wl,--gc-sections"
                ),
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "include"),
                "-I" + str(root / "src"),
                str(root / "tests/api/diagnostics_bridge.c"),
                str(root / "src/emulation/emulation.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        subprocess.run([str(output)], check=True)
