"""Autonomous protocol behavior: activation, access rules and bounded replies."""

import ctypes
from dataclasses import replace
from pathlib import Path
import platform
import subprocess
import unittest

from ..support.card_state import Image, fixture_view
from ..support.fixtures import TYPE2_TYPES, Model, sample
from ..support.classic_peer import ClassicPeer, crc_frame


class CardBehaviorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = (
            root / ".cache/tests" / getattr(cls, "native_name", "cards.dylib")
        )
        output.parent.mkdir(parents=True, exist_ok=True)
        sources = (
            "tests/cards/tag_bridge.c",
            "src/cards/image.c",
            "src/cards/image_copy.c",
            "src/cards/iso4/iso4_image.c",
            "src/cards/desfire/desfire_capture.c",
            "src/cards/type2/ntag_config.c",
            "src/common/crypto1.c",
            "src/phy/nfca/picc/nfca_frame.c",
            "src/phy/nfcv/picc/nfcv_frame.c",
            "src/protocols/nfca/nfca_activation.c",
            "src/protocols/nfca/nfca_iso4.c",
            "src/cards/desfire/desfire_commands.c",
            "src/cards/desfire/desfire_files.c",
            "src/cards/desfire/desfire_tag.c",
            "src/cards/mifare/mifare_access.c",
            "src/cards/mifare/mifare_commands.c",
            "src/cards/mifare/mifare_product.c",
            "src/cards/mifare/tag/mifare_tag_session.c",
            "src/cards/mifare/tag/mifare_tag.c",
            "src/cards/type2/ntag_image.c",
            "src/cards/type2/ntag_memory.c",
            "src/cards/type2/ntag_tag.c",
            "src/cards/slix/slix_tag.c",
            "src/cards/slix/slix_image.c",
            "src/protocols/nfcv/frame.c",
            "src/protocols/nfcv/nfcv_standard.c",
            "src/cards/nfcv/nfcv_memory_store.c",
            *getattr(cls, "extra_sources", ()),
        )
        subprocess.run(
            [
                "cc",
                "-dynamiclib" if platform.system() == "Darwin" else "-shared",
                "-fPIC",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DUNLEASHED_HOST_TEST=1",
                "-D__not_in_flash_func(name)=name",
                "-I" + str(root / "tests/stubs"),
                "-I" + str(root / "src"),
                "-I" + str(root / "include"),
                *(str(root / path) for path in sources),
                "-o",
                str(output),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(output))
        cls.native.host_tag_load_view.argtypes = (ctypes.POINTER(Image),)
        cls.native.host_tag_load_view.restype = ctypes.c_bool
        cls.native.host_tag_exchange.argtypes = (
            ctypes.c_char_p,
            ctypes.c_uint16,
            ctypes.c_bool,
            ctypes.c_bool,
            ctypes.c_void_p,
            ctypes.c_size_t,
        )
        cls.native.host_tag_exchange.restype = ctypes.c_size_t

    def load(self, source):
        image = fixture_view(source)
        self.assertTrue(self.native.host_tag_load_view(ctypes.byref(image)))

    def exchange(self, data, short=False, crc=True):
        output = ctypes.create_string_buffer(600)
        size = self.native.host_tag_exchange(
            data, len(data), short, crc, output, len(output)
        )
        return output.raw[:size]

    def select_ntag(self, image, *, load=True):
        if load:
            self.load(image)
        self.assertEqual(self.exchange(b"\x26", short=True), b"\x44\x00")
        self.exchange(b"\x93\x20", crc=False)
        fragment = b"\x88" + image.uid[:3]
        bcc = bytes((fragment[0] ^ fragment[1] ^ fragment[2] ^ fragment[3],))
        self.assertEqual(
            self.exchange(b"\x93\x70" + fragment + bcc + b"\0\0")[:1], b"\x04"
        )

        self.exchange(b"\x95\x20", crc=False)
        fragment = image.uid[3:]
        bcc = bytes((fragment[0] ^ fragment[1] ^ fragment[2] ^ fragment[3],))
        self.assertEqual(
            self.exchange(b"\x95\x70" + fragment + bcc + b"\0\0")[:1], b"\x00"
        )

    def select_iso4(self, image):
        self.load(image)
        start = 5 if image.model == Model.DESFIRE else 4
        metadata = image.metadata
        self.assertEqual(self.exchange(crc_frame(b"\xe0\x80")), b"")
        self.assertEqual(
            self.exchange(b"\x26", short=True), metadata[start : start + 2]
        )
        levels = (len(image.uid) - 1) // 3
        for level in range(levels):
            offset = level * 3
            more = offset + 4 < len(image.uid)
            uid = (
                b"\x88" + image.uid[offset : offset + 3]
                if more
                else image.uid[offset : offset + 4]
            )
            fragment = uid + bytes((uid[0] ^ uid[1] ^ uid[2] ^ uid[3],))
            command = bytes((0x93 + 2 * level,))
            self.assertEqual(
                self.exchange(command + b"\x20", crc=False), fragment
            )
            self.assertEqual(
                self.exchange(crc_frame(command + b"\x70" + fragment)),
                crc_frame(b"\x04" if more else metadata[start + 2 : start + 3]),
            )
            if more:
                self.assertEqual(self.exchange(crc_frame(b"\xe0\x80")), b"")
        self.assertEqual(self.exchange(crc_frame(b"\x02\0\xa4")), b"")
        self.assertEqual(
            self.exchange(crc_frame(b"\xe0\x80")),
            crc_frame(metadata[start + 4 : start + 4 + metadata[start + 3]]),
        )

    def type2_image(self, model):
        image = sample(model)
        _, _, config, _ = TYPE2_TYPES[model]
        memory = bytearray(image.memory)
        memory[10:12] = bytes(2)
        if config:
            memory[config * 4 + 3] = 0xFF
            memory[(config + 1) * 4] = 0
        return replace(image, memory=bytes(memory))

    def classic_peer(self, model=Model.CLASSIC_1K, *, load=True):
        image = sample(model)
        memory = bytearray(image.memory)
        memory[16:32] = bytes(range(16))
        memory[64:80] = bytes(range(16, 32))
        memory[3 * 16 : 3 * 16 + 6] = bytes.fromhex("A0 A1 A2 A3 A4 A5")
        memory[7 * 16 : 7 * 16 + 6] = bytes.fromhex("B0 B1 B2 B3 B4 B5")
        image = replace(image, memory=bytes(memory))
        if load:
            self.load(image)
        self.exchange(b"\x26", short=True)
        self.exchange(b"\x93\x20", crc=False)
        bcc = bytes(
            (image.uid[0] ^ image.uid[1] ^ image.uid[2] ^ image.uid[3],)
        )
        self.exchange(crc_frame(b"\x93\x70" + image.uid + bcc))
        return image, ClassicPeer(self.native, self.exchange, image.uid)

    def test_type2_reload_discards_auth_pending_write_and_ram_changes(self):
        image = self.type2_image(Model.NTAG213)
        memory = bytearray(image.memory)
        memory[41 * 4 + 3] = 4
        memory[42 * 4] = 0x80
        image = replace(image, memory=bytes(memory))
        password = image.memory[43 * 4 : 44 * 4]
        self.select_ntag(image)
        self.assertEqual(len(self.exchange(crc_frame(b"\x1b" + password))), 4)
        self.assertEqual(
            self.exchange(crc_frame(b"\xa2\x04" + b"\x5a" * 4)), b"\x0a"
        )
        self.assertEqual(self.exchange(crc_frame(b"\xa0\x05")), b"\x0a")
        self.load(image)
        self.assertEqual(self.exchange(crc_frame(b"\x30\x04")), b"")
        self.select_ntag(image, load=False)
        self.assertEqual(self.exchange(crc_frame(b"\x30\x04")), b"\x04")
        self.assertEqual(len(self.exchange(crc_frame(b"\x1b" + password))), 4)
        self.assertEqual(
            self.exchange(crc_frame(b"\x30\x04")),
            crc_frame(image.memory[16:32]),
        )

    def test_classic_reload_discards_authenticated_session_and_ram_changes(
        self,
    ):
        for model in (
            Model.CLASSIC_4K,
            Model.CLASSIC_MINI,
            Model.CLASSIC_2K,
            Model.CLASSIC_1K,
        ):
            with self.subTest(model=model):
                image, peer = self.classic_peer(model)
                self.assertTrue(peer.authenticate(1, image.memory[48:54]))
                self.assertEqual(peer.write(1, b"\x5a" * 16), (0xA, 0xA))
                self.load(image)
                self.assertEqual(peer.read(1), b"")
                image, peer = self.classic_peer(model, load=False)
                self.assertTrue(peer.authenticate(1, image.memory[48:54]))
                self.assertEqual(peer.read(1), crc_frame(image.memory[16:32]))

    def test_ntag_engine_preserves_pages_version_and_signature(self):
        image = sample(Model.NTAG215)
        self.select_ntag(image)
        self.assertEqual(self.exchange(b"\x30\x00\0\0")[:-2], image.memory[:16])
        self.assertEqual(self.exchange(b"\x60\0\0")[:-2], image.metadata[4:12])
        self.assertEqual(
            self.exchange(b"\x3c\x00\0\0")[:-2], image.metadata[12:44]
        )
        self.assertEqual(self.exchange(b"\x30\x00\0\0", crc=False), b"")

    def test_ntag_fast_read_complete_memory_and_bounds(self):
        image = sample(Model.NTAG215)
        memory = bytearray(image.memory)
        memory[131 * 4 + 3] = 0xFF
        memory[132 * 4] = 0
        image = replace(image, memory=bytes(memory))
        self.select_ntag(image)
        expected = image.memory[: 133 * 4] + b"\0" * 8
        self.assertEqual(
            self.exchange(crc_frame(b"\x3a\x00\x86")), crc_frame(expected)
        )
        for bounds in (b"\x01\x00", b"\x00\x87", b"\x87\x87"):
            self.assertEqual(
                self.exchange(crc_frame(b"\x3a" + bounds)), b"\x00"
            )

    def test_ultralight_plain_rejects_ev1_commands(self):
        image = sample(Model.ULTRALIGHT)
        self.select_ntag(image)
        for command in (b"\xa5\x02\x01\0\0\0", b"\x3e\x02", b"\x4b"):
            self.assertEqual(self.exchange(crc_frame(command)), b"\x00")

    def test_type2_models_preserve_version_cyclic_read_and_fast_read_bounds(
        self,
    ):
        for model, (_, pages, config, version) in TYPE2_TYPES.items():
            with self.subTest(model=model):
                image = self.type2_image(model)
                self.select_ntag(image)
                expected = bytearray(image.memory)
                if config:
                    expected[(config + 2) * 4 : (config + 4) * 4] = bytes(8)
                cyclic = expected[-4:] + expected[:12]
                self.assertEqual(
                    self.exchange(crc_frame(bytes((0x30, pages - 1)))),
                    crc_frame(cyclic),
                )
                self.assertEqual(
                    self.exchange(crc_frame(bytes((0x30, pages)))), b"\0"
                )
                if version:
                    self.assertEqual(
                        self.exchange(crc_frame(b"\x60")), crc_frame(version)
                    )
                    for start in range(0, pages, 135):
                        end = min(start + 134, pages - 1)
                        self.assertEqual(
                            self.exchange(crc_frame(bytes((0x3A, start, end)))),
                            crc_frame(expected[start * 4 : (end + 1) * 4]),
                        )
                    if pages > 135:
                        self.assertEqual(
                            self.exchange(
                                crc_frame(bytes((0x3A, 0, pages - 1)))
                            ),
                            b"\0",
                        )
                else:
                    for payload in (
                        b"\x60",
                        b"\x3a\0\x0f",
                        b"\x3c\0",
                        b"\x39\0",
                        b"\x1b" + bytes(4),
                    ):
                        self.assertEqual(
                            self.exchange(crc_frame(payload)), b"\0"
                        )

    def test_type2_lock_groups_are_model_specific_and_one_way(self):
        for model, dynamic, group in (
            (Model.ULTRALIGHT_EV1_128, 36, 2),
            (Model.NTAG213, 40, 2),
            (Model.NTAG215, 130, 16),
            (Model.NTAG216, 226, 16),
        ):
            with self.subTest(model=model):
                image = self.type2_image(model)
                memory = bytearray(image.memory)
                memory[dynamic * 4 : dynamic * 4 + 4] = bytes(4)
                self.select_ntag(replace(image, memory=bytes(memory)))
                self.assertEqual(
                    self.exchange(
                        crc_frame(bytes((0xA2, dynamic, 1, 0, 0, 0)))
                    ),
                    b"\x0a",
                )
                self.assertEqual(
                    self.exchange(
                        crc_frame(bytes((0xA2, dynamic, 0, 0, 0, 0)))
                    ),
                    b"\x0a",
                )
                for page in (16, 16 + group - 1):
                    self.assertEqual(
                        self.exchange(
                            crc_frame(bytes((0xA2, page)) + bytes(4))
                        ),
                        b"\0",
                    )
                self.assertEqual(
                    self.exchange(
                        crc_frame(bytes((0xA2, 16 + group)) + b"\xa5" * 4)
                    ),
                    b"\x0a",
                )
        self.select_ntag(self.type2_image(Model.ULTRALIGHT))
        self.assertEqual(
            self.exchange(crc_frame(b"\xa2\x02\0\0\x10\0")), b"\x0a"
        )
        self.assertEqual(
            self.exchange(crc_frame(b"\xa2\x02" + bytes(4))), b"\x0a"
        )
        self.assertEqual(
            self.exchange(crc_frame(b"\xa2\x04" + bytes(4))), b"\0"
        )

    def test_type2_ev1_counter_overflow_tearing_and_vcsl(self):
        for model in (Model.ULTRALIGHT_EV1_48, Model.ULTRALIGHT_EV1_128):
            with self.subTest(model=model):
                image = self.type2_image(model)
                self.select_ntag(image)
                for index in range(3):
                    self.assertEqual(
                        self.exchange(crc_frame(bytes((0x39, index)))),
                        crc_frame((123).to_bytes(3, "little")),
                    )
                    self.assertEqual(
                        self.exchange(
                            crc_frame(bytes((0xA5, index, 5, 0, 0, 0)))
                        ),
                        b"\x0a",
                    )
                    self.assertEqual(
                        self.exchange(crc_frame(bytes((0x3E, index)))),
                        crc_frame(b"\xbd"),
                    )
                    self.assertEqual(
                        self.exchange(
                            crc_frame(bytes((0xA5, index, 0xFF, 0xFF, 0xFF, 0)))
                        ),
                        b"\x04",
                    )
                    self.assertEqual(
                        self.exchange(crc_frame(bytes((0x39, index)))),
                        crc_frame((128).to_bytes(3, "little")),
                    )
                for opcode in (0x39, 0x3E):
                    self.assertEqual(
                        self.exchange(crc_frame(bytes((opcode, 3)))), b"\0"
                    )
                self.assertEqual(
                    self.exchange(crc_frame(b"\xa5\x03" + bytes(4))), b"\x04"
                )
                config = TYPE2_TYPES[model][2]
                self.assertEqual(
                    self.exchange(crc_frame(b"\x4b")),
                    crc_frame(
                        image.memory[
                            (config + 1) * 4 + 1 : (config + 1) * 4 + 2
                        ]
                    ),
                )

    def test_type2_password_profiles_protect_memory_and_defer_configuration_lock(
        self,
    ):
        for model, (_, _, config, _) in TYPE2_TYPES.items():
            if not config:
                continue
            with self.subTest(model=model):
                image = self.type2_image(model)
                memory = bytearray(image.memory)
                memory[config * 4 + 3] = 4
                memory[(config + 1) * 4] = 0x82
                metadata = image.metadata[:56] + bytes(4)
                image = replace(image, memory=bytes(memory), metadata=metadata)
                self.select_ntag(image)
                self.assertEqual(self.exchange(crc_frame(b"\x30\x04")), b"\x04")
                self.assertEqual(
                    self.exchange(crc_frame(b"\x1b" + b"\xff" * 4)), b"\x04"
                )
                password = image.memory[(config + 2) * 4 : (config + 3) * 4]
                pack = image.memory[(config + 3) * 4 : (config + 3) * 4 + 2]
                self.assertEqual(
                    self.exchange(crc_frame(b"\x1b" + password)),
                    crc_frame(pack),
                )
                self.assertEqual(
                    self.exchange(crc_frame(b"\x30\x04")),
                    crc_frame(image.memory[16:32]),
                )
                self.assertEqual(
                    self.exchange(
                        crc_frame(bytes((0xA2, config + 1, 0xC2, 0, 0, 0)))
                    ),
                    b"\x0a",
                )
                self.assertEqual(
                    self.exchange(crc_frame(bytes((0xA2, config)) + bytes(4))),
                    b"\x0a",
                )

    def test_ntag_authentication_uses_supplied_password_and_pack(self):
        image = sample(Model.NTAG215)
        self.select_ntag(image)
        self.assertEqual(
            self.exchange(b"\x1b" + b"\xff" * 4 + b"\0\0"), b"\x04"
        )
        password = image.memory[133 * 4 : 134 * 4]
        self.assertEqual(
            self.exchange(b"\x1b" + password + b"\0\0")[:-2],
            image.memory[134 * 4 : 134 * 4 + 2],
        )

    def test_vicinity_inventory_and_addressed_memory(self):
        for model in (Model.SLIX, Model.SLIX_L, Model.ISO15693):
            image = sample(model)
            self.load(image)
            self.assertEqual(
                self.exchange(b"\x26\x01\x00\0\0"),
                b"\x00" + image.metadata[5:6] + image.uid[::-1],
            )
            command = b"\x62\x20" + image.uid[::-1] + b"\x01\0\0"
            self.assertEqual(
                self.exchange(command), b"\x00\x01" + image.memory[4:8]
            )
            self.assertEqual(self.exchange(command, crc=False), b"")

    def test_iso4_uid_lengths_rats_and_identity_only_silence(self):
        for length in (4, 7, 10):
            image = sample(Model.ISO_DEP)
            uid = bytes(range(length))
            atqa0 = {4: 4, 7: 0x44, 10: 0x84}[length]
            metadata = image.metadata[:4] + bytes((atqa0,)) + image.metadata[5:]
            image = replace(image, uid=uid, metadata=metadata)
            with self.subTest(length=length):
                self.select_iso4(image)
                self.assertEqual(self.exchange(crc_frame(b"\x02\0\xa4")), b"")
                self.assertEqual(self.exchange(crc_frame(b"\xd0\x01")), b"")
                self.assertEqual(
                    self.exchange(crc_frame(b"\xe0\x80"), crc=False), b""
                )

    def test_iso4_local_transport_and_reset_have_bounded_replies(self):
        for model in (Model.ISO4, Model.MIFARE_PLUS):
            with self.subTest(model=model):
                image = sample(model)
                self.select_iso4(image)
                for command, response in (
                    (b"\x02\0\xa4", b"\x02\x90\0"),
                    (b"\x03\0\xa4", b"\x03\x90\0"),
                    (b"\x0a\x07\0\xa4", b"\x0a\x07\x90\0"),
                    (b"\x1b\x07\0\xa4", b"\xab\x07"),
                    (b"\xaa", b"\xbb"),
                    (b"\xba", b"\xab\0"),
                    (b"\xd0\x01", b"\xd0"),
                ):
                    self.assertEqual(
                        self.exchange(crc_frame(command)), crc_frame(response)
                    )
                self.assertEqual(self.exchange(crc_frame(b"\x0a")), b"")
                self.assertEqual(self.exchange(crc_frame(b"\xd0\x11\x01")), b"")
                self.assertEqual(
                    self.exchange(crc_frame(b"\x02" + b"x" * 30)), b""
                )
                self.assertEqual(
                    self.exchange(crc_frame(b"\xc2")), crc_frame(b"\xca\0")
                )
                self.assertEqual(self.exchange(crc_frame(b"\x02\0\xa4")), b"")
                self.exchange(crc_frame(b"\xe0\x80"))
                self.assertEqual(
                    self.exchange(crc_frame(b"\x02\0\xa4")),
                    crc_frame(b"\x02\x90\0"),
                )
                self.exchange(b"\x52", short=True)
                self.assertEqual(self.exchange(crc_frame(b"\x02\0\xa4")), b"")

    def test_st25ta_reference_local_replies_are_explicit_demonstration(self):
        image = sample(Model.ST25TA)
        self.select_iso4(image)
        self.assertEqual(
            self.exchange(crc_frame(b"\x02\0\xa4")), crc_frame(b"\x02\x90\0")
        )
        response = self.exchange(crc_frame(b"\x02\xa2\xb0\0\0\x1d"))
        self.assertEqual(len(response), 34)
        self.assertEqual(response[:8], bytes.fromhex("02 00 1B D1 01 17 54 02"))
        self.assertEqual(response[-4:-2], b"\x90\0")
        self.assertEqual(response, crc_frame(response[:-2]))
        self.assertEqual(
            self.exchange(crc_frame(b"\x02\0\x20\0")), crc_frame(b"\x02\x63\0")
        )

    def test_generic_vicinity_full_memory_and_last_block_locks(self):
        image = sample(Model.ISO15693)
        self.load(image)

        def command(code, params=b"", option=False):
            return (
                bytes((0x62 if option else 0x22, code))
                + image.uid[::-1]
                + params
                + b"\0\0"
            )

        self.assertEqual(
            self.exchange(command(0x2B)),
            b"\x00\x0f"
            + image.uid[::-1]
            + image.metadata[5:7]
            + b"\x3f\x03"
            + image.metadata[7:8],
        )
        self.assertEqual(
            self.exchange(command(0x23, b"\0\x3f")), b"\0" + image.memory
        )
        expected = b"\0" + b"".join(
            image.metadata[12 + i : 13 + i] + image.memory[i * 4 : i * 4 + 4]
            for i in range(64)
        )
        self.assertEqual(len(expected), 321)
        self.assertEqual(
            self.exchange(command(0x23, b"\0\x3f", True)), expected
        )
        self.assertEqual(
            self.exchange(command(0x2C, b"\0\x3f")), b"\0" + image.metadata[12:]
        )
        self.assertEqual(self.exchange(command(0x23, b"\0\xff")), b"\1\x10")
        self.assertEqual(self.exchange(command(0x20, b"\x40")), b"\1\x10")
        self.assertEqual(self.exchange(command(0x21, b"\x3eABCD")), b"\0")
        self.assertEqual(self.exchange(command(0x20, b"\x3e")), b"\0ABCD")
        self.assertEqual(self.exchange(command(0x22, b"\x3e")), b"\0")
        self.assertEqual(self.exchange(command(0x21, b"\x3eEFGH")), b"\1\x12")
        self.assertEqual(self.exchange(command(0x22, b"\x3e")), b"\1\x11")
        self.assertEqual(self.exchange(command(0x21, b"\x3fABCD")), b"\1\x12")
        self.assertEqual(
            self.exchange(command(0x20, b"\x3f")), b"\0" + image.memory[-4:]
        )
        # Manufacturer custom commands are never aliased to NXP functionality.
        custom = b"\x22\xb2" + image.uid[1:2] + image.uid[::-1] + b"\0\0"
        self.assertEqual(self.exchange(custom), b"\1\1")
        self.assertEqual(self.exchange(custom[:2] + b"\x04" + custom[3:]), b"")

    def test_generic_vicinity_selected_quiet_and_metadata_locks(self):
        image = sample(Model.ISO15693)
        self.load(image)

        def addressed(code, params=b""):
            return bytes((0x22, code)) + image.uid[::-1] + params + b"\0\0"

        selected = b"\x12\x20\0\0\0"
        self.assertEqual(self.exchange(selected), b"")
        self.assertEqual(self.exchange(addressed(0x25)), b"\0")
        self.assertEqual(self.exchange(selected), b"\0" + image.memory[:4])
        self.assertEqual(self.exchange(addressed(0x27, b"\x55")), b"\0")
        self.assertEqual(self.exchange(addressed(0x28)), b"\0")
        self.assertEqual(self.exchange(addressed(0x27, b"\x66")), b"\1\x12")
        self.assertEqual(self.exchange(addressed(0x29, b"\x77")), b"\0")
        self.assertEqual(self.exchange(addressed(0x2A)), b"\0")
        self.assertEqual(self.exchange(addressed(0x29, b"\x88")), b"\1\x12")
        self.assertEqual(self.exchange(b"\x26\x01\0\0\0")[1:2], b"\x77")
        self.assertEqual(self.exchange(addressed(0x02)), b"")
        self.assertEqual(self.exchange(b"\x26\x01\0\0\0"), b"")
        self.assertEqual(self.exchange(addressed(0x26)), b"\0")
        self.assertEqual(self.exchange(b"\x26\x01\0\0\0")[1:2], b"\x77")
        # Reload restores the supplied image, including locks and metadata.
        self.load(image)
        self.assertEqual(self.exchange(addressed(0x27, b"\x66")), b"\0")

    def test_classic_selection_and_authentication_boundary(self):
        image = sample(Model.CLASSIC_1K)
        self.load(image)
        self.assertEqual(self.exchange(b"\x26", short=True), b"\x04\x00")
        bcc = bytes(
            (image.uid[0] ^ image.uid[1] ^ image.uid[2] ^ image.uid[3],)
        )
        self.assertEqual(self.exchange(b"\x93\x20", crc=False), image.uid + bcc)
        self.assertEqual(
            self.exchange(b"\x93\x70" + image.uid + bcc + b"\0\0")[:1],
            b"\x08",
        )
        self.assertEqual(self.exchange(b"\x30\x00\0\0"), b"")

        self.assertEqual(len(self.exchange(b"\x60\x00\0\0")), 4)
        self.assertEqual(self.exchange(b"\0" * 8), b"")
        self.assertEqual(self.exchange(b"\x30\x00\0\0"), b"")

    def test_classic_encrypted_read_write_and_trailer_key_masking(self):
        image, peer = self.classic_peer()
        self.assertTrue(peer.authenticate(1, image.memory[48:54]))
        self.assertEqual(peer.read(1), crc_frame(image.memory[16:32]))
        trailer = b"\0" * 6 + image.memory[54:64]
        self.assertEqual(peer.read(3), crc_frame(trailer))
        replacement = b"\x5a" * 16
        self.assertEqual(peer.write(1, replacement), (0xA, 0xA))
        self.assertEqual(peer.read(1), crc_frame(replacement))

    def test_classic_cached_write_crc_failure_preserves_memory_and_cipher(self):
        image, peer = self.classic_peer()
        self.assertTrue(peer.authenticate(1, image.memory[48:54]))
        self.assertEqual(peer.nibble(peer.command(b"\xa0\x01")), 0xA)
        bad_data = bytearray(crc_frame(b"\x5a" * 16))
        bad_data[-1] ^= 1
        nack = self.exchange(peer.crypt(bytes(bad_data)), crc=False)
        self.assertEqual(peer.nibble(nack), 1)
        self.assertEqual(peer.read(1), crc_frame(image.memory[16:32]))
        self.assertEqual(peer.write(1, b"\xa5" * 16), (0xA, 0xA))
        self.assertEqual(peer.read(1), crc_frame(b"\xa5" * 16))

    def test_classic_cached_value_operand_advances_only_received_bits(self):
        image, peer = self.classic_peer()
        self.assertTrue(peer.authenticate(1, image.memory[48:54]))
        value = (7).to_bytes(4, "little")
        block = value + bytes(x ^ 255 for x in value) + value + b"\1\xfe\1\xfe"
        self.assertEqual(peer.write(1, block), (0xA, 0xA))
        self.assertEqual(peer.nibble(peer.command(b"\xc1\x01")), 0xA)
        operand = crc_frame((3).to_bytes(4, "little"))
        self.assertEqual(self.exchange(peer.crypt(operand), crc=False), b"")
        self.assertEqual(peer.nibble(peer.command(b"\xb0\x01")), 0xA)
        updated = (10).to_bytes(4, "little")
        expected = (
            updated + bytes(x ^ 255 for x in updated) + updated + block[12:]
        )
        self.assertEqual(peer.read(1), crc_frame(expected))

    def test_classic_nested_authentication_switches_sector_and_key(self):
        image, peer = self.classic_peer()
        self.assertTrue(peer.authenticate(1, image.memory[48:54]))
        self.assertNotEqual(peer.nibble(peer.command(b"\x30\x04")), 0xA)
        self.assertTrue(
            peer.authenticate(4, image.memory[112:118], nested=True)
        )
        self.assertEqual(peer.read(4), crc_frame(image.memory[64:80]))
        self.assertNotEqual(peer.nibble(peer.command(b"\x30\x01")), 0xA)

    def test_classic_wrong_key_does_not_reveal_memory(self):
        _, peer = self.classic_peer()
        self.assertFalse(peer.authenticate(1, b"\xff" * 6))
        self.assertEqual(self.exchange(crc_frame(b"\x30\x01")), b"")

    def test_classic_sizes_authenticate_last_sector_and_reject_outside_memory(
        self,
    ):
        for model, last, sak, atqa in (
            (Model.CLASSIC_MINI, 19, 0x09, b"\x04\x00"),
            (Model.CLASSIC_1K, 63, 0x08, b"\x04\x00"),
            (Model.CLASSIC_2K, 127, 0x08, b"\x04\x00"),
            (Model.CLASSIC_4K, 255, 0x18, b"\x02\x00"),
        ):
            with self.subTest(model=model):
                image, peer = self.classic_peer(model)
                # Restart activation to check model-specific identification.
                self.assertEqual(self.exchange(b"\x52", short=True), atqa)
                bcc = bytes(
                    (image.uid[0] ^ image.uid[1] ^ image.uid[2] ^ image.uid[3],)
                )
                self.assertEqual(
                    self.exchange(crc_frame(b"\x93\x70" + image.uid + bcc))[:1],
                    bytes((sak,)),
                )
                self.assertTrue(peer.authenticate(last - 1, b"\xff" * 6))
                pattern = bytes(range(16))
                self.assertEqual(peer.write(last - 1, pattern), (0xA, 0xA))
                self.assertEqual(peer.read(last - 1), crc_frame(pattern))
                trailer = (
                    b"\0" * 6 + image.memory[last * 16 + 6 : (last + 1) * 16]
                )
                self.assertEqual(peer.read(last), crc_frame(trailer))
                if last < 255:
                    self.assertFalse(
                        peer.authenticate(last + 1, b"\xff" * 6, nested=True)
                    )

    def test_classic_4k_large_sector_groups_enforce_distinct_permissions(self):
        image = sample(Model.CLASSIC_4K)
        memory = bytearray(image.memory)
        # Sector 32: group 1 forbids all data operations. Groups 0/2 stay open.
        memory[143 * 16 + 6 : 143 * 16 + 9] = bytes.fromhex("D5 AD 22")
        image = replace(image, memory=bytes(memory))
        self.load(image)
        self.exchange(b"\x26", short=True)
        bcc = bytes(
            (image.uid[0] ^ image.uid[1] ^ image.uid[2] ^ image.uid[3],)
        )
        self.exchange(crc_frame(b"\x93\x70" + image.uid + bcc))
        peer = ClassicPeer(self.native, self.exchange, image.uid)
        self.assertTrue(peer.authenticate(126, b"\xff" * 6))
        self.assertEqual(peer.read(126), crc_frame(bytes(16)))
        self.assertTrue(peer.authenticate(128, b"\xff" * 6, nested=True))
        for block in (128, 132, 138, 142):
            self.assertEqual(peer.read(block), crc_frame(bytes(16)))
        for block in (133, 137):
            self.assertEqual(
                peer.nibble(peer.command(bytes((0x30, block)))), 0x4
            )
            self.assertEqual(
                peer.nibble(peer.command(bytes((0xA0, block)))), 0x4
            )
        self.assertTrue(peer.authenticate(254, b"\xff" * 6, nested=True))
        self.assertEqual(peer.write(254, b"\xa5" * 16), (0xA, 0xA))
        self.assertEqual(peer.read(254), crc_frame(b"\xa5" * 16))

    def test_slix_privacy_requires_the_supplied_password(self):
        image = sample(Model.SLIX_L)
        metadata = bytearray(image.metadata)
        metadata[4] |= 16
        self.load(replace(image, metadata=bytes(metadata)))
        inventory = b"\x26\x01\x00\0\0"
        self.assertEqual(self.exchange(inventory), b"")
        random = self.exchange(b"\x02\xb2\x04\0\0")
        self.assertEqual(len(random), 3)
        mask = int.from_bytes(random[1:], "big") * 0x10001
        password = int.from_bytes(image.secrets[:4], "little")
        command = b"\x02\xb3\x04\x04"
        self.assertEqual(
            self.exchange(
                command + (mask ^ password ^ 1).to_bytes(4, "big") + b"\0\0"
            ),
            b"",
        )
        self.assertEqual(self.exchange(inventory), b"")
        self.assertEqual(
            self.exchange(
                command + (mask ^ password).to_bytes(4, "big") + b"\0\0"
            ),
            b"\x00",
        )
        self.assertEqual(self.exchange(inventory)[2:], image.uid[::-1])

    def test_slix_eas_state_and_standard_alarm_sequence(self):
        for model in (Model.SLIX, Model.SLIX_L):
            image = sample(model)
            self.load(image)
            prefix = b"\x22"

            def command(code, flags=prefix):
                return flags + bytes((code, 4)) + image.uid[::-1] + b"\0\0"

            self.assertEqual(self.exchange(command(0xA5)), b"")
            self.assertEqual(self.exchange(command(0xA2)), b"\x00")
            sequence = bytes.fromhex(
                "2F B3 62 70 D5 A7 90 7F E8 B1 C0 3C E2 81 49 76 "
                "82 DA 9A 86 6F 97 8B B0 F1 8C D1 12 A5 72 37 EF"
            )
            self.assertEqual(self.exchange(command(0xA5)), b"\x00" + sequence)
            self.assertEqual(self.exchange(command(0xA5, b"\x62")), b"")
            self.assertEqual(self.exchange(command(0xA3)), b"\x00")
            self.assertEqual(self.exchange(command(0xA5)), b"")

    def test_slix_does_not_acknowledge_unauthorized_vendor_operations(self):
        image = sample(Model.SLIX_L)
        self.load(image)
        for command in (0xA6, 0xB9):
            request = bytes((0x22, command, 0x04)) + image.uid[::-1] + b"\0\0"
            self.assertEqual(self.exchange(request), b"")
