"""Type 2 access rules from NXP lock diagrams and command tables.

NTAG213/215/216: sections 8.5.2/3/7, figures 10-12 and table 22.
Ultralight EV1 MF0ULX1: sections 8.5.2/3/6 and figure 9.
Byte 3 checks retain supplied image bytes, rather than silicon BDh readback.
"""

from dataclasses import replace
import unittest

from . import test_behavior as behavior
from ..support.classic_peer import crc_frame
from ..support.fixtures import Model, TYPE2_TYPES


# (dynamic page, pages per L bit, valid L bits, valid BL bits).
# These are independent datasheet expectations, not C configuration lookups.
DYNAMIC_LOCKS = {
    Model.ULTRALIGHT_EV1_128: (36, 2, 10, 5),
    Model.NTAG213: (40, 2, 12, 6),
    Model.NTAG215: (130, 16, 8, 4),
    Model.NTAG216: (226, 16, 14, 7),
}


class Type2AccessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        behavior.CardBehaviorTests.setUpClass.__func__(cls)

    load = behavior.CardBehaviorTests.load
    exchange = behavior.CardBehaviorTests.exchange
    select_ntag = behavior.CardBehaviorTests.select_ntag
    type2_image = behavior.CardBehaviorTests.type2_image

    def unlocked_image(self, model):
        image = self.type2_image(model)
        memory = bytearray(image.memory)
        if model in DYNAMIC_LOCKS:
            page = DYNAMIC_LOCKS[model][0]
            memory[page * 4 : page * 4 + 4] = bytes(4)
        return replace(image, memory=bytes(memory))

    def write(self, page, data, *, compat=False):
        if compat:
            self.assertEqual(
                self.exchange(crc_frame(bytes((0xA0, page)))), b"\x0a"
            )
            command = data + bytes(12)
        else:
            command = bytes((0xA2, page)) + data
        return self.exchange(crc_frame(command))

    def read_page(self, page):
        return self.exchange(crc_frame(bytes((0x30, page))))[:4]

    def test_every_dynamic_lock_bit_protects_only_its_documented_pages(self):
        for model, (dynamic, group, bits, _) in DYNAMIC_LOCKS.items():
            for bit in range(bits):
                with self.subTest(model=model, bit=bit):
                    self.select_ntag(self.unlocked_image(model))
                    lock = (1 << bit).to_bytes(2, "little") + bytes(2)
                    self.assertEqual(self.write(dynamic, lock), b"\x0a")
                    first = 16 + bit * group
                    last = min(first + group, dynamic)
                    for page in range(16, dynamic):
                        expected = b"\0" if first <= page < last else b"\x0a"
                        self.assertEqual(self.write(page, bytes(4)), expected)

    def test_static_block_locks_freeze_only_their_documented_lock_bits(self):
        for model in TYPE2_TYPES:
            for block_bit, frozen in ((0, 0x0008), (1, 0x03F0), (2, 0xFC00)):
                for compat in (False, True):
                    with self.subTest(
                        model=model, bit=block_bit, compat=compat
                    ):
                        self.select_ntag(self.unlocked_image(model))
                        before = self.read_page(2)
                        block = 1 << block_bit
                        self.assertEqual(
                            self.write(
                                2, bytes(2) + block.to_bytes(2, "little")
                            ),
                            b"\x0a",
                        )
                        self.assertEqual(
                            self.write(
                                2, bytes(2) + b"\xff\xff", compat=compat
                            ),
                            b"\x0a",
                        )
                        expected = 0xFFFF & ~frozen
                        self.assertEqual(
                            self.read_page(2),
                            before[:2] + expected.to_bytes(2, "little"),
                        )
                        for page in range(3, 16):
                            reply = b"\x0a" if frozen & (1 << page) else b"\0"
                            self.assertEqual(self.write(page, bytes(4)), reply)

    def test_dynamic_block_locks_freeze_each_pair_of_lock_bits(self):
        for model, (dynamic, group, bits, block_bits) in DYNAMIC_LOCKS.items():
            all_locks = (1 << bits) - 1
            for block_bit in range(block_bits):
                for compat in (False, True):
                    with self.subTest(
                        model=model, bit=block_bit, compat=compat
                    ):
                        self.select_ntag(self.unlocked_image(model))
                        block = 1 << block_bit
                        self.assertEqual(
                            self.write(dynamic, bytes((0, 0, block, 0))),
                            b"\x0a",
                        )
                        self.assertEqual(
                            self.write(
                                dynamic, b"\xff\xff\0\xff", compat=compat
                            ),
                            b"\x0a",
                        )
                        locks = all_locks & ~(3 << (2 * block_bit))
                        self.assertEqual(
                            self.read_page(dynamic)[:3],
                            locks.to_bytes(2, "little") + bytes((block,)),
                        )
                        for page in range(16, dynamic):
                            bit = (page - 16) // group
                            reply = b"\0" if locks & (1 << bit) else b"\x0a"
                            self.assertEqual(self.write(page, bytes(4)), reply)

    def test_simultaneous_lock_and_block_lock_updates_are_one_way(self):
        for model in TYPE2_TYPES:
            with self.subTest(model=model, area="static"):
                self.select_ntag(self.unlocked_image(model))
                self.assertEqual(self.write(2, b"\0\0\x12\0"), b"\x0a")
                self.assertEqual(self.write(2, bytes(4)), b"\x0a")
                self.assertEqual(self.write(4, bytes(4)), b"\0")
                self.assertEqual(self.write(5, bytes(4)), b"\x0a")
        for model, (dynamic, _, _, _) in DYNAMIC_LOCKS.items():
            with self.subTest(model=model, area="dynamic"):
                self.select_ntag(self.unlocked_image(model))
                self.assertEqual(self.write(dynamic, b"\1\0\1\0"), b"\x0a")
                self.assertEqual(self.write(dynamic, bytes(4)), b"\x0a")
                self.assertEqual(self.read_page(dynamic)[:3], b"\1\0\1")
                self.assertEqual(self.write(16, bytes(4)), b"\0")

    def test_imported_block_locks_preserve_existing_locks(self):
        for model, (dynamic, _, _, _) in DYNAMIC_LOCKS.items():
            with self.subTest(model=model):
                image = self.unlocked_image(model)
                memory = bytearray(image.memory)
                memory[10:12] = b"\x12\0"
                memory[dynamic * 4 : dynamic * 4 + 3] = b"\1\0\1"
                self.select_ntag(replace(image, memory=bytes(memory)))
                self.assertEqual(self.write(2, b"\0\0\x20\0"), b"\x0a")
                self.assertEqual(self.read_page(2)[2:], b"\x12\0")
                self.assertEqual(self.write(dynamic, b"\2\0\0\0"), b"\x0a")
                self.assertEqual(self.read_page(dynamic)[:3], b"\1\0\1")

    def test_dynamic_reserved_bits_ignore_writes_and_preserve_imported_bytes(
        self,
    ):
        for model, (dynamic, _, bits, block_bits) in DYNAMIC_LOCKS.items():
            lock_mask = (1 << bits) - 1
            block_mask = (1 << block_bits) - 1
            for imported in (False, True):
                for compat in (False, True):
                    with self.subTest(
                        model=model, imported=imported, compat=compat
                    ):
                        image = self.unlocked_image(model)
                        memory = bytearray(image.memory)
                        reserved = (
                            (0xFFFF ^ lock_mask).to_bytes(2, "little")
                            + bytes((0xFF ^ block_mask, 0xA5))
                            if imported
                            else bytes(4)
                        )
                        memory[dynamic * 4 : dynamic * 4 + 4] = reserved
                        self.select_ntag(replace(image, memory=bytes(memory)))
                        self.assertEqual(
                            self.write(dynamic, b"\xff" * 4, compat=compat),
                            b"\x0a",
                        )
                        expected = (
                            int.from_bytes(reserved[:2], "little") | lock_mask
                        ).to_bytes(2, "little") + bytes(
                            (reserved[2] | block_mask, reserved[3])
                        )
                        self.assertEqual(self.read_page(dynamic), expected)
                        self.assertEqual(
                            self.write(dynamic, bytes(4), compat=compat),
                            b"\x0a",
                        )
                        self.assertEqual(self.read_page(dynamic), expected)

    def test_ntag_rejects_ev1_only_commands_without_mutating_counters(self):
        for model in (Model.NTAG213, Model.NTAG215, Model.NTAG216):
            with self.subTest(model=model):
                image = self.unlocked_image(model)
                config = TYPE2_TYPES[model][2]
                memory = bytearray(image.memory)
                memory[(config + 1) * 4] = (
                    0x10  # Enable NTAG's automatic counter.
                )
                self.select_ntag(replace(image, memory=bytes(memory)))
                for index in range(3):
                    self.assertEqual(
                        self.exchange(
                            crc_frame(bytes((0xA5, index, 5, 0, 0, 0)))
                        ),
                        b"\0",
                    )
                    self.assertEqual(
                        self.exchange(crc_frame(bytes((0x3E, index)))), b"\0"
                    )
                self.assertEqual(self.exchange(crc_frame(b"\x4b")), b"\0")
                self.assertEqual(
                    self.exchange(crc_frame(b"\x39\2")),
                    crc_frame((123).to_bytes(3, "little")),
                )
                self.exchange(crc_frame(b"\x30\4"))
                self.assertEqual(
                    self.exchange(crc_frame(b"\x39\2")),
                    crc_frame((124).to_bytes(3, "little")),
                )

    def test_cfglck_is_deferred_and_halt_wakeup_does_not_apply_it(self):
        for model, (_, _, config, _) in TYPE2_TYPES.items():
            if not config:
                continue
            with self.subTest(model=model):
                self.select_ntag(self.unlocked_image(model))
                self.assertEqual(self.write(config + 1, b"\x40\0\0\0"), b"\x0a")
                self.assertEqual(self.read_page(config + 1), b"\x40\0\0\0")
                self.assertEqual(self.write(config, b"\0\0\0\xff"), b"\x0a")
                self.assertEqual(self.exchange(crc_frame(b"\x50\0")), b"")
                self.assertEqual(self.exchange(b"\x52", short=True), b"\x44\0")
                # Select again without loading or otherwise resetting the image.
                uid = self.unlocked_image(model).uid
                for opcode, fragment in (
                    (0x93, b"\x88" + uid[:3]),
                    (0x95, uid[3:]),
                ):
                    bcc = bytes(
                        (fragment[0] ^ fragment[1] ^ fragment[2] ^ fragment[3],)
                    )
                    self.exchange(
                        crc_frame(bytes((opcode, 0x70)) + fragment + bcc)
                    )
                self.assertEqual(self.write(config, b"\0\0\0\xff"), b"\x0a")
                self.assertEqual(self.write(config + 1, b"\x40\1\0\0"), b"\x0a")

    def test_loaded_cfglck_protects_config_but_not_authenticated_pwd_pack(self):
        for model, (_, _, config, _) in TYPE2_TYPES.items():
            if not config:
                continue
            with self.subTest(model=model):
                image = self.unlocked_image(model)
                memory = bytearray(image.memory)
                memory[(config + 1) * 4] = 0x40
                memory[config * 4 + 3] = config + 2
                image = replace(image, memory=bytes(memory))
                self.select_ntag(image)
                for page in (config, config + 1, config + 2, config + 3):
                    self.assertEqual(self.write(page, bytes(4)), b"\0")
                password = image.memory[(config + 2) * 4 : (config + 3) * 4]
                pack = image.memory[(config + 3) * 4 : (config + 3) * 4 + 2]
                self.assertEqual(
                    self.exchange(crc_frame(b"\x1b" + password)),
                    crc_frame(pack),
                )
                for page in (config, config + 1):
                    self.assertEqual(self.write(page, bytes(4)), b"\0")
                for page in (config + 2, config + 3):
                    self.assertEqual(self.write(page, bytes(4)), b"\x0a")
