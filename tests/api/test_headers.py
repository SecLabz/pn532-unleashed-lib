"""Exported headers must be self-contained and keep private paths private."""

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class HeaderBoundaryTests(unittest.TestCase):
    def test_individual_headers_compile_as_c_and_cpp(self):
        for header in sorted((ROOT / "include").rglob("*.h")):
            modes = ((),)
            if header.name == "diagnostics.h":
                modes = (
                    (),
                    ("-DPNU_RF_TRACE=1",),
                    ("-DPNU_HSU_TRACE=1",),
                    ("-DPNU_RF_TRACE=1", "-DPNU_HSU_TRACE=1"),
                )
            for flags in modes:
                for language, standard in (("c", "c11"), ("c++", "c++17")):
                    with self.subTest(
                        header=header.name, language=language, flags=flags
                    ):
                        subprocess.run(
                            [
                                "cc",
                                "-x",
                                language,
                                f"-std={standard}",
                                "-Wall",
                                "-Wextra",
                                "-Werror",
                                "-fsyntax-only",
                                "-I",
                                str(ROOT / "include"),
                                *flags,
                                "-",
                            ],
                            input=f'#include "{header.relative_to(ROOT / "include")}"\n',
                            text=True,
                            check=True,
                        )

    def test_exported_headers_do_not_include_private_sources(self):
        for header in sorted((ROOT / "include").rglob("*.h")):
            with self.subTest(header=header.name):
                result = subprocess.run(
                    ["cc", "-x", "c", "-M", "-I", str(ROOT / "include"), "-"],
                    input=f'#include "{header.relative_to(ROOT / "include")}"\n',
                    text=True,
                    capture_output=True,
                    check=True,
                )
                self.assertNotIn("/src/", result.stdout)
