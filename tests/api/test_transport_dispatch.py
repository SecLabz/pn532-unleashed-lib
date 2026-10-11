"""Both control backends remain callable through one transport binding."""

from pathlib import Path
import subprocess
import unittest


class TransportDispatchTests(unittest.TestCase):
    def test_both_backends_in_one_binary(self):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests/transport_dispatch"
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(root / "include"),
                "-I" + str(root / "src"),
                str(root / "tests/api/transport_dispatch.c"),
                str(root / "src/pn532/pn532_transport.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        subprocess.run([str(output)], check=True)
