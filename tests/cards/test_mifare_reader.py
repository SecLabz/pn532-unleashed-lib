"""Autonomous reader contracts against the production card protocol peer."""

import ctypes as c
from dataclasses import replace
import unittest

from . import test_behavior
from ..support.card_state import Image, fixture_view
from ..support.classic_peer import State
from ..support.fixtures import Model, sample


class Source(c.Structure):
    _fields_ = [
        ("block", c.c_uint8),
        ("key_b", c.c_bool),
        ("key", c.c_uint8 * 6),
    ]


class Observation(c.Structure):
    _fields_ = [
        ("nonce", c.c_uint32),
        ("ciphertext", c.c_uint32),
        ("parity", c.c_uint8 * 4),
    ]


class MifareReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.native_name = "mifare-reader.dylib"
        cls.extra_sources = (
            "tests/cards/nfca_reader_bridge.c",
            "tests/cards/mifare_recovery_bridge.c",
            "tests/cards/reader_lifecycle_bridge.c",
            "src/cards/nfca_access.c",
            "src/cards/nfca_select_validate.c",
            "src/cards/mifare/reader/mifare_access_reader.c",
            "src/cards/mifare/reader/mifare_recovery.c",
            "src/cards/mifare/reader/mifare_darkside.c",
            "src/cards/mifare/reader/mifare_reader.c",
            "src/cards/mifare/reader/mifare_write.c",
            "src/cards/type2/reader/ntag_access_reader.c",
            "src/cards/type2/reader/ntag_reader.c",
            "src/protocols/nfca/reader.c",
            "src/phy/nfca/pcd/nfca_pcd_encode.c",
            "src/phy/pcd/reader_tx_waveform.c",
        )
        test_behavior.CardBehaviorTests.setUpClass.__func__(cls)
        native = cls.native
        native.host_mifare_open_view.argtypes = (
            c.POINTER(Image),
            c.c_int,
            c.c_uint8,
        )
        native.host_nfca_field.restype = c.c_bool
        native.host_mifare_interrupts_masked.restype = c.c_bool
        native.nfca_mifare_read_block.argtypes = (
            c.c_uint8,
            c.c_bool,
            c.c_void_p,
            c.c_void_p,
            c.POINTER(c.c_bool),
        )
        for name in ("nfca_mifare_trailer_check", "nfca_mifare_trailer_write"):
            getattr(native, name).argtypes = (
                c.c_uint8,
                c.c_bool,
                c.c_void_p,
                c.c_void_p,
                c.c_void_p,
            )
        native.nfca_mifare_collect_static.argtypes = (
            c.POINTER(Source),
            c.c_uint8,
            c.c_bool,
            c.c_bool,
            c.POINTER(Observation),
        )
        native.crypto1_init.argtypes = (c.POINTER(State), c.c_uint64)
        native.crypto1_word.argtypes = (c.POINTER(State), c.c_uint32, c.c_bool)
        native.crypto1_word.restype = c.c_uint32
        native.crypto1_prng_successor.argtypes = (c.c_uint32, c.c_uint32)
        native.crypto1_prng_successor.restype = c.c_uint32

    def tearDown(self):
        self.native.nfc_stop()

    def open(self, model=Model.CLASSIC_1K, fault=0, image=None):
        self.native.nfc_stop()
        self.image = image or sample(model)
        view = fixture_view(self.image)
        self.assertEqual(
            self.native.host_mifare_open_view(c.byref(view), model, fault), 0
        )

    def observed(self, sector=1):
        block = sector * 4 + 3 if sector < 32 else 128 + (sector - 32) * 16 + 15
        data = c.create_string_buffer(16)
        self.assertEqual(
            self.native.nfca_mifare_read_block(
                block, False, b"\xff" * 6, data, c.byref(c.c_bool())
            ),
            0,
        )
        return data.raw

    def test_check_is_read_only_and_releases_selection(self):
        self.open()
        before = self.observed()
        after = bytes.fromhex("0102030405067F078869112233445566")
        self.assertEqual(
            self.native.nfca_mifare_trailer_check(
                1, False, b"\xff" * 6, before, after
            ),
            0,
        )
        self.assertEqual(self.native.host_nfca_classic_writes(), 0)
        self.assertFalse(self.native.host_nfca_field())

    def test_write_verifies_each_new_authenticating_key_once(self):
        for model, sector in (
            (Model.CLASSIC_MINI, 1),
            (Model.CLASSIC_1K, 1),
            (Model.CLASSIC_2K, 31),
            (Model.CLASSIC_4K, 39),
        ):
            with self.subTest(model=model):
                self.open(model)
                before = self.observed(sector)
                authentications = self.native.host_nfca_authentications()
                after = bytes.fromhex("0102030405067F078869112233445566")
                self.assertEqual(
                    self.native.nfca_mifare_trailer_write(
                        sector, False, b"\xff" * 6, before, after
                    ),
                    0,
                )
                self.assertEqual(self.native.host_nfca_classic_writes(), 1)
                self.assertEqual(
                    self.native.host_nfca_authentications() - authentications,
                    3,  # Fresh old key, new A, new B.
                )
                self.assertFalse(self.native.host_nfca_field())

    def test_post_ack_failures_are_unknown_and_never_retry(self):
        # New A, usable B, metadata, readable B, then final field cleanup.
        for fault in (20, 21, 22, 23, 24):
            with self.subTest(fault=fault):
                self.open()
                before = self.observed()
                access = "FF078069" if fault == 23 else "7F078869"
                after = bytes.fromhex("010203040506" + access + "112233445566")
                self.native.host_nfca_fault(fault)
                self.assertEqual(
                    self.native.nfca_mifare_trailer_write(
                        1, False, b"\xff" * 6, before, after
                    ),
                    15,  # NFC_WRITE_OUTCOME_UNKNOWN
                )
                self.assertEqual(self.native.host_nfca_classic_writes(), 1)
                self.assertFalse(self.native.host_nfca_field())

    def test_unsupported_selected_model_is_distinct_from_bad_arguments(self):
        for operation in (
            self.native.nfca_mifare_trailer_check,
            self.native.nfca_mifare_trailer_write,
        ):
            self.open(Model.NTAG213)
            self.assertEqual(
                operation(1, False, bytes(6), bytes(16), bytes(16)), 9
            )
            self.assertFalse(self.native.host_nfca_field())
            self.open()
            self.assertEqual(
                operation(40, False, bytes(6), bytes(16), bytes(16)), 1
            )
            self.assertEqual(self.native.host_nfca_classic_writes(), 0)
            self.assertFalse(self.native.host_nfca_field())

    def test_static_nested_retains_last_known_nonce_and_target_cipher(self):
        image = sample(Model.CLASSIC_1K)
        memory = bytearray(image.memory)
        target_key = bytes.fromhex("010203040506")
        memory[7 * 16 : 7 * 16 + 6] = target_key
        self.open(image=replace(image, memory=bytes(memory)))
        source = Source(0, False, (c.c_uint8 * 6)(*([255] * 6)))
        nonces = []
        for advance in (False, True):
            record = Observation()
            self.assertEqual(
                self.native.nfca_mifare_collect_static(
                    c.byref(source), 4, False, advance, c.byref(record)
                ),
                0,
            )
            cipher = State()
            self.native.crypto1_init(
                c.byref(cipher), int.from_bytes(target_key, "big")
            )
            cuid = int.from_bytes(image.uid[-4:], "big")
            plain = record.ciphertext ^ self.native.crypto1_word(
                c.byref(cipher), record.ciphertext ^ cuid, True
            )
            self.assertEqual(
                plain, self.native.crypto1_prng_successor(record.nonce, 32)
            )
            nonces.append(record.nonce)
        self.assertNotEqual(*nonces)
        self.assertFalse(self.native.host_mifare_interrupts_masked())

    def test_static_nested_failed_extra_auth_or_expiration_preserves_output(
        self,
    ):
        # Encrypted source nonce parity, second AUTH response, target expiration.
        for fault in (25, 26, 27):
            with self.subTest(fault=fault):
                self.open(fault=fault)
                source = Source(0, False, (c.c_uint8 * 6)(*([255] * 6)))
                record = Observation(
                    0xAAAAAAAA, 0xBBBBBBBB, (c.c_uint8 * 4)(1, 0, 1, 0)
                )
                before = bytes(record)
                self.assertNotEqual(
                    self.native.nfca_mifare_collect_static(
                        c.byref(source), 4, False, True, c.byref(record)
                    ),
                    0,
                )
                self.assertEqual(bytes(record), before)
                self.assertFalse(self.native.host_mifare_interrupts_masked())
                self.assertEqual(self.native.host_nfca_classic_writes(), 0)
