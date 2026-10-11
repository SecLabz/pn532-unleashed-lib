"""Execute published recipes to check RF ownership and recovery cleanup."""

import ctypes
from pathlib import Path
import platform
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
OK, INVALID_ARGUMENT, BUSY, RF_ERROR, NO_TAG = 0, 1, 4, 6, 7


class Tag(ctypes.Structure):
    _fields_ = [
        ("uid", ctypes.c_uint8 * 10),
        ("uid_size_bytes", ctypes.c_uint8),
        ("atqa", ctypes.c_uint8 * 2),
        ("sak", ctypes.c_uint8),
    ]


class DocRecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = ROOT / ".cache/tests/doc-recipes"
        output.mkdir(parents=True, exist_ok=True)
        cls.names = (
            "write_page4",
            "read_type2",
            "read_nfcv_block",
            "read_sector1",
            "detect_magic",
            "change_gen1a_uid",
            "read_desfire_version",
            "start_iso15693_demo",
        )
        snippets = []
        for document in (
            "nfca",
            "nfcv",
            "classic",
            "desfire",
            "emulation",
        ):
            content = (ROOT / f"docs/{document}.md").read_text()
            for snippet in re.findall(r"```c\n(.*?)```", content, re.S):
                if any(
                    re.search(rf"\b{name}\s*\(", snippet) for name in cls.names
                ):
                    snippets.append(snippet)
        if len(snippets) != len(cls.names):
            raise AssertionError("Every ownership recipe must be compiled")
        source = output / "recipes.c"
        source.write_text("\n".join(snippets))
        library = output / "recipes.dylib"
        subprocess.run(
            [
                "cc",
                "-dynamiclib" if platform.system() == "Darwin" else "-shared",
                "-fPIC",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-I" + str(ROOT / "include"),
                str(ROOT / "tests/api/doc_recipe_bridge.c"),
                str(source),
                "-o",
                str(library),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(library))

    def call(self, name):
        tag = Tag(uid_size_bytes=4)
        data = ctypes.create_string_buffer(16384)
        size = ctypes.c_uint16()
        arguments = {
            "write_page4": (ctypes.byref(tag), data, data),
            "read_type2": (ctypes.byref(tag), 8, None, data),
            "read_nfcv_block": (0, data),
            "read_sector1": (data, data, ctypes.byref(size)),
            "detect_magic": (data,),
            "change_gen1a_uid": (ctypes.byref(tag), data),
            "read_desfire_version": (data,),
            "start_iso15693_demo": (),
        }
        return getattr(self.native, name)(*arguments[name])

    def test_rejected_operations_preserve_the_existing_role(self):
        for name in self.names:
            for status in (BUSY, INVALID_ARGUMENT, 3, 5):
                with self.subTest(recipe=name, status=status):
                    self.native.host_recipe_config(status, OK, RF_ERROR)
                    self.assertEqual(self.call(name), status)
                    self.assertEqual(self.native.host_recipe_stops(), 0)
                    self.assertEqual(self.native.host_recipe_operations(), 0)

    def test_acquired_sessions_stop_after_a_failed_operation(self):
        for name in (
            "write_page4",
            "read_type2",
            "read_nfcv_block",
            "read_sector1",
            "read_desfire_version",
        ):
            for cleanup, result in ((OK, NO_TAG), (RF_ERROR, RF_ERROR)):
                if name in ("write_page4", "read_type2"):
                    result = NO_TAG
                with self.subTest(recipe=name, cleanup=cleanup):
                    self.native.host_recipe_config(OK, NO_TAG, cleanup)
                    self.assertEqual(self.call(name), result)
                    self.assertEqual(self.native.host_recipe_stops(), 1)
                    self.assertEqual(self.native.host_recipe_operations(), 1)

    def test_reader_and_emulation_hardware_failures_still_recover(self):
        for name in (
            "read_nfcv_block",
            "read_desfire_version",
            "start_iso15693_demo",
        ):
            with self.subTest(recipe=name):
                self.native.host_recipe_config(RF_ERROR, OK, OK)
                self.assertEqual(self.call(name), RF_ERROR)
                self.assertEqual(self.native.host_recipe_stops(), 1)
                self.assertEqual(self.native.host_recipe_operations(), 0)

    def test_successful_emulation_keeps_running(self):
        for name in ("start_iso15693_demo",):
            with self.subTest(recipe=name):
                self.native.host_recipe_config(OK, OK, OK)
                self.assertEqual(self.call(name), OK)
                self.assertEqual(self.native.host_recipe_stops(), 0)

    def test_complete_operations_do_not_repeat_internal_cleanup(self):
        for name in ("change_gen1a_uid",):
            for status in (OK, NO_TAG, RF_ERROR):
                with self.subTest(recipe=name, status=status):
                    self.native.host_recipe_config(status, OK, RF_ERROR)
                    self.assertEqual(self.call(name), status)
                    self.assertEqual(self.native.host_recipe_stops(), 0)
        self.native.host_recipe_config(OK, RF_ERROR, RF_ERROR)
        self.assertEqual(self.call("detect_magic"), RF_ERROR)
        self.assertEqual(self.native.host_recipe_stops(), 0)
