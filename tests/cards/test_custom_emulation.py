"""Real dispatch boundaries and independent requests against the NTAG example."""

from pathlib import Path
import subprocess
import unittest


class CustomEmulationTests(unittest.TestCase):
    def test_handlers_bounds_stop_and_ntag_activation_read(self):
        self.check_dispatch(False)

    def test_led_records_after_tx_including_silent_rx_and_failed_tx(self):
        self.check_dispatch(True)

    def check_dispatch(self, led):
        root = Path(__file__).resolve().parents[2]
        output = root / f".cache/tests/custom-emulation-{int(led)}"
        output.parent.mkdir(parents=True, exist_ok=True)
        sources = [
            "tests/cards/custom_emulation_bridge.c",
            "src/board/board_config.c",
            "src/emulation/emulation.c",
            "src/protocols/nfca/nfca_activation.c",
            "src/cards/type2/ntag_config.c",
        ]
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-D__not_in_flash_func(name)=name",
                "-DPNU_EXAMPLE_TRANSPORT=NFC_TRANSPORT_SPI",
                *(["-DPNU_ACTIVITY_LED=1"] if led else []),
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "src"),
                "-I" + str(root / "include"),
                *(str(root / source) for source in sources),
                "-o",
                str(output),
            ],
            check=True,
        )
        subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
