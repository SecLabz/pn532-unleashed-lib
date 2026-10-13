"""Export real mutable card state and reload it through the public image API."""

import ctypes
from dataclasses import replace
from pathlib import Path
import shutil
import unittest

from ..support.card_state import Image, fixture_view, fixture_sections
from ..support.classic_peer import crc_frame
from ..support.fixtures import (
    CLASSIC_TYPES,
    ISO4_TYPES,
    TYPE2_TYPES,
    Model,
    sample,
)
from . import test_behavior as behavior


class SnapshotTests(unittest.TestCase):
    native_name = "snapshot.dylib"
    extra_sources = (
        "tests/cards/snapshot_bridge.c",
        "src/core.c",
        "tests/api/sniff_stub.c",
        "src/board/board_config.c",
        "src/emulation/emulation.c",
        "src/emulation/snapshot.c",
    )

    @classmethod
    def setUpClass(cls):
        behavior.CardBehaviorTests.setUpClass.__func__(cls)

    def setUp(self):
        folder = Path(__file__).resolve().parents[2] / ".cache/tests"
        target = folder / (self._testMethodName + ".dylib")
        shutil.copyfile(folder / self.native_name, target)
        self.native = ctypes.CDLL(str(target))
        self.native.emulation_start.argtypes = (ctypes.POINTER(Image),)
        self.native.emulation_snapshot.argtypes = (
            ctypes.POINTER(Image),
            ctypes.c_void_p,
            ctypes.c_size_t,
        )
        self.native.host_tag_load_view.argtypes = (ctypes.POINTER(Image),)
        self.native.host_tag_load_view.restype = ctypes.c_bool
        self.native.host_tag_exchange.argtypes = (
            ctypes.c_char_p,
            ctypes.c_uint16,
            ctypes.c_bool,
            ctypes.c_bool,
            ctypes.c_void_p,
            ctypes.c_size_t,
        )
        self.native.host_tag_exchange.restype = ctypes.c_size_t
        self.native.host_snapshot_clock.argtypes = (ctypes.c_uint32,)
        self.native.host_snapshot_arm_failure.argtypes = (ctypes.c_bool,)
        self.native.host_snapshot_custom.argtypes = (ctypes.c_bool,)

    exchange = behavior.CardBehaviorTests.exchange
    select_ntag = behavior.CardBehaviorTests.select_ntag
    type2_image = behavior.CardBehaviorTests.type2_image
    classic_peer = behavior.CardBehaviorTests.classic_peer
    select_iso4 = behavior.CardBehaviorTests.select_iso4

    def load(self, source):
        self.assertEqual(self.native.nfc_stop(), 0)
        self.assertEqual(self.native.nfc_init(None), 0)
        image = fixture_view(source)
        self.assertTrue(self.native.host_tag_load_view(ctypes.byref(image)))
        self.assertEqual(self.native.emulation_start(ctypes.byref(image)), 0)
        for buffer in image._buffers:
            ctypes.memset(buffer, 0xCC, ctypes.sizeof(buffer))

    def capture(self, capacity=8314, expected=0):
        output = ctypes.create_string_buffer(b"\xa5" * 8314)
        image = Image()
        original = bytes(image)
        state = self.native.nfc_state()
        hardware_calls = self.native.host_snapshot_hardware_calls()
        self.assertEqual(
            self.native.emulation_snapshot(
                ctypes.byref(image), output, capacity
            ),
            expected,
        )
        self.assertEqual(self.native.nfc_state(), state)
        self.assertEqual(
            self.native.host_snapshot_hardware_calls(), hardware_calls
        )
        if expected:
            self.assertEqual(bytes(image), original)
            self.assertEqual(output.raw, b"\xa5" * 8314 + b"\0")
            return None
        return fixture_sections(image)

    def decode(self, source):
        return source

    def reload(self, source):
        self.load(source)
        self.assertEqual(self.capture(), source)
        return source

    def vicinity(self, image, code, params=b"", *, vendor=False, option=False):
        data = bytes((0x62 if option else 0x22, code))
        if vendor:
            data += b"\x04"
        return self.exchange(data + image.uid[::-1] + params + b"\0\0")

    def masked_password(self, image, password):
        reply = self.vicinity(image, 0xB2, vendor=True)
        self.assertEqual(len(reply), 3)
        word = int.from_bytes(reply[1:], "big")
        return (password ^ word ^ (word << 16)).to_bytes(4, "big")

    def authorize(self, image, password_id, password):
        masked = self.masked_password(image, password)
        self.assertEqual(
            self.vicinity(
                image, 0xB3, bytes((password_id,)) + masked, vendor=True
            ),
            b"\0",
        )

    def test_every_model_round_trips_exactly_and_checks_capacity(self):
        for model in Model:
            with self.subTest(model=model):
                source = sample(model)
                self.load(source)
                self.capture(capacity=0, expected=13)
                self.capture(capacity=3, expected=13)
                binary = self.capture(capacity=len(source.binary))
                self.assertEqual(binary, source)
                self.reload(binary)

    def test_invalid_arguments_lifecycle_and_clock(self):
        self.capture(expected=3)  # NOT_INITIALIZED
        output = ctypes.create_string_buffer(8314)
        self.assertEqual(
            self.native.emulation_snapshot(None, output, len(output)), 1
        )
        self.assertEqual(
            self.native.emulation_snapshot(ctypes.byref(Image()), None, 0), 1
        )
        self.assertEqual(self.native.nfc_init(None), 0)
        self.capture(expected=4)  # IDLE is not an active image.
        self.assertEqual(self.native.nfc_operation_begin(6), 0)  # NFC-A reader
        self.capture(expected=4)
        self.assertEqual(self.native.nfc_stop(), 0)
        self.load(sample(Model.NTAG215))
        self.native.host_snapshot_clock(54240000)
        self.capture(expected=5)
        self.native.host_snapshot_clock(135600000)
        self.assertEqual(self.native.nfc_stop(), 0)
        self.capture(expected=4)
        self.native.host_snapshot_arm_failure(True)
        source = sample(Model.NTAG215)
        view = fixture_view(source)
        self.assertEqual(self.native.emulation_start(ctypes.byref(view)), 6)
        self.capture(expected=6)
        self.native.host_snapshot_arm_failure(False)
        self.load(sample(Model.SLIX))
        self.assertEqual(self.capture(), sample(Model.SLIX))

    def test_custom_handlers_do_not_export_a_previous_builtin_image(self):
        for nfcv in (False, True):
            self.load(sample(Model.CLASSIC_1K))
            self.assertEqual(self.native.nfc_stop(), 0)
            self.assertEqual(self.native.host_snapshot_custom(nfcv), 0)
            self.capture(expected=9)
            self.assertEqual(self.native.nfc_stop(), 0)

    def test_classic_writes_and_new_trailer_key_survive_reload(self):
        for model in CLASSIC_TYPES:
            with self.subTest(model=model):
                source, peer = self.classic_peer(model)
                self.assertTrue(peer.authenticate(1, source.memory[48:54]))
                self.assertEqual(peer.write(1, b"\x5a" * 16), (0xA, 0xA))
                new_key = bytes.fromhex("010203040506")
                trailer = new_key + source.memory[54:64]
                self.assertEqual(peer.write(3, trailer), (0xA, 0xA))
                binary = self.capture()
                exported = self.decode(binary)
                self.assertEqual(exported.memory[16:32], b"\x5a" * 16)
                self.assertEqual(exported.memory[48:64], trailer)
                # Export does not reset the selected/authenticated RF session.
                self.assertEqual(peer.read(1), crc_frame(b"\x5a" * 16))
                self.reload(binary)
                _, peer = self.classic_peer(model, load=False)
                self.assertTrue(peer.authenticate(1, new_key))
                self.assertEqual(peer.read(1), crc_frame(b"\x5a" * 16))

    def test_type2_writes_counters_auth_attempts_and_passwords(self):
        for model, (_, _, config, _) in TYPE2_TYPES.items():
            with self.subTest(model=model):
                source = self.type2_image(model)
                if config:
                    memory = bytearray(source.memory)
                    memory[(config + 1) * 4] = 7  # Count failed passwords.
                    source = replace(source, memory=bytes(memory))
                self.select_ntag(source)
                self.assertEqual(
                    self.exchange(crc_frame(b"\xa2\x04DATA")), b"\x0a"
                )
                if config:
                    new_password = b"PASS"
                    self.assertEqual(
                        self.exchange(
                            crc_frame(bytes((0xA2, config + 2)) + new_password)
                        ),
                        b"\x0a",
                    )
                    # An incorrect authentication updates persistent attempts.
                    self.assertEqual(
                        self.exchange(crc_frame(b"\x1bFAIL")), b"\x04"
                    )
                    if model in (
                        Model.ULTRALIGHT_EV1_48,
                        Model.ULTRALIGHT_EV1_128,
                    ):
                        self.assertEqual(
                            self.exchange(crc_frame(b"\xa5\x01\x05\0\0\0")),
                            b"\x0a",
                        )
                binary = self.capture()
                exported = self.decode(binary)
                self.assertEqual(exported.memory[16:20], b"DATA")
                if config:
                    self.assertEqual(
                        exported.memory[(config + 2) * 4 : (config + 3) * 4],
                        b"PASS",
                    )
                    self.assertEqual(
                        int.from_bytes(exported.metadata[56:60], "little"),
                        int.from_bytes(source.metadata[56:60], "little") + 1,
                    )
                    if model in (
                        Model.ULTRALIGHT_EV1_48,
                        Model.ULTRALIGHT_EV1_128,
                    ):
                        self.assertEqual(
                            int.from_bytes(exported.metadata[47:50], "little"),
                            128,
                        )
                # The same selected tag continues to answer after export.
                self.assertEqual(
                    self.exchange(crc_frame(b"\x30\x04"))[:4], b"DATA"
                )
                self.reload(binary)
                self.select_ntag(exported, load=False)
                self.assertEqual(
                    self.exchange(crc_frame(b"\x30\x04"))[:4], b"DATA"
                )

    def test_invalid_classic_access_bits_do_not_publish_an_unloadable_image(
        self,
    ):
        source, peer = self.classic_peer()
        self.assertTrue(peer.authenticate(1, source.memory[48:54]))
        trailer = bytearray(source.memory[48:64])
        trailer[6] ^= 1
        self.assertEqual(peer.write(3, bytes(trailer)), (0xA, 0xA))
        self.capture(expected=2)
        self.load(source)
        self.assertEqual(self.decode(self.capture()).memory, source.memory)

    def test_type2_lock_and_read_counter_persist(self):
        source = self.type2_image(Model.NTAG215)
        memory = bytearray(source.memory)
        memory[130 * 4 : 131 * 4] = bytes(4)
        memory[132 * 4] = 0x10  # Enable the counter increment on READ.
        source = replace(source, memory=bytes(memory))
        self.select_ntag(source)
        self.assertEqual(self.exchange(crc_frame(b"\xa2\x82\1\0\0\0")), b"\x0a")
        self.exchange(crc_frame(b"\x30\x04"))
        binary = self.capture()
        exported = self.decode(binary)
        self.assertEqual(exported.memory[520], 1)
        self.assertEqual(
            int.from_bytes(exported.metadata[50:53], "little"), 124
        )
        self.reload(binary)
        self.select_ntag(exported, load=False)
        self.assertEqual(self.exchange(crc_frame(b"\xa2\x10DATA")), b"\0")

    def test_vicinity_memory_locks_dsfid_afi_and_eas_survive_reload(self):
        for model in (Model.ISO15693, Model.SLIX, Model.SLIX_L):
            with self.subTest(model=model):
                source = sample(model)
                self.load(source)
                for code, params in (
                    (0x21, b"\0DATA"),
                    (0x22, b"\0"),
                    (0x27, b"\x55"),
                    (0x28, b""),
                    (0x29, b"\x77"),
                    (0x2A, b""),
                ):
                    self.assertEqual(self.vicinity(source, code, params), b"\0")
                if model != Model.ISO15693:
                    self.assertEqual(
                        self.vicinity(source, 0xA3, vendor=True), b"\0"
                    )
                    self.assertEqual(
                        self.vicinity(source, 0xA4, vendor=True), b"\0"
                    )
                binary = self.capture()
                exported = self.decode(binary)
                self.assertEqual(exported.memory[:4], b"DATA")
                self.assertEqual(exported.metadata[12], 1)
                self.assertEqual(exported.metadata[5:7], b"\x77\x55")
                self.assertEqual(exported.metadata[4] & 3, 3)
                if model != Model.ISO15693:
                    self.assertEqual(exported.metadata[4] & 12, 8)
                self.reload(binary)
                self.assertEqual(self.vicinity(source, 0x20, b"\0"), b"\0DATA")
                self.assertEqual(
                    self.vicinity(source, 0x21, b"\0FAIL"), b"\1\x12"
                )
                self.assertEqual(self.vicinity(source, 0x29, b"\0"), b"\1\x12")

    def test_slix_password_changes_protections_and_privacy(self):
        for model in (Model.SLIX, Model.SLIX_L):
            source = sample(model)
            self.load(source)
            ids = (0x10,) if model == Model.SLIX else (4, 8, 0x10)
            updated = b""
            for index, password_id in enumerate(ids):
                password = int.from_bytes(
                    source.secrets[index * 4 : index * 4 + 4], "little"
                )
                self.authorize(source, password_id, password)
                new_password = 0x10203040 + index
                self.assertEqual(
                    self.vicinity(
                        source,
                        0xB4,
                        bytes((password_id,)) + new_password.to_bytes(4, "big"),
                        vendor=True,
                    ),
                    b"\0",
                )
                updated += new_password.to_bytes(4, "little")
            self.authorize(source, 0x10, int.from_bytes(updated[-4:], "little"))
            for option in (False, True):
                self.assertEqual(
                    self.vicinity(source, 0xA6, vendor=True, option=option),
                    b"\0",
                )
            if model == Model.SLIX_L:
                masked = self.masked_password(
                    source, int.from_bytes(updated[:4], "little")
                )
                self.assertEqual(
                    self.vicinity(source, 0xBA, masked, vendor=True), b"\0"
                )
            binary = self.capture()
            exported = self.decode(binary)
            self.assertEqual(exported.secrets, updated)
            self.assertEqual(exported.metadata[4] & 0x60, 0x60)
            if model == Model.SLIX_L:
                self.assertEqual(exported.metadata[4] & 0x10, 0x10)
            self.reload(binary)
            if model == Model.SLIX_L:
                self.assertEqual(self.exchange(b"\x26\x01\0\0\0"), b"")
                self.authorize(source, 4, int.from_bytes(updated[:4], "little"))
            # Authentication was not serialized; AFI remains protected.
            self.assertEqual(self.vicinity(source, 0x27, b"\x99"), b"")
            self.authorize(source, 0x10, int.from_bytes(updated[-4:], "little"))
            self.assertEqual(self.vicinity(source, 0x27, b"\x99"), b"\0")

    def test_destroyed_slix_l_returns_unsupported(self):
        source = sample(Model.SLIX_L)
        self.load(source)
        password = int.from_bytes(source.secrets[4:8], "little")
        masked = self.masked_password(source, password)
        self.assertEqual(
            self.vicinity(source, 0xB9, masked, vendor=True), b"\0"
        )
        self.capture(expected=9)
        self.assertEqual(self.vicinity(source, 0x20, b"\0"), b"")
        self.load(source)
        self.assertEqual(self.capture(), source)

    def test_iso4_ten_byte_uid_and_full_ats_are_retained(self):
        for model in ISO4_TYPES:
            source = sample(model)
            metadata = bytearray(source.metadata[:8])
            metadata[4] = (metadata[4] & 0x3F) | 0x80
            metadata[7] = 32
            ats = bytes((32, 0)) + bytes(range(30))
            source = replace(
                source, uid=bytes(range(10)), metadata=bytes(metadata) + ats
            )
            self.load(source)
            exported = self.decode(self.capture())
            self.assertEqual(exported.uid, source.uid)
            self.assertEqual(exported.metadata, source.metadata)
            self.reload(exported)

    def test_previous_copy_is_independent_of_later_nfc_writes(self):
        source = self.type2_image(Model.NTAG213)
        self.select_ntag(source)
        before = self.capture()
        self.assertEqual(self.exchange(crc_frame(b"\xa2\x04DATA")), b"\x0a")
        after = self.capture()
        self.assertNotEqual(before, after)
        self.assertEqual(self.decode(before).memory, source.memory)
        self.assertEqual(self.decode(after).memory[16:20], b"DATA")

    def test_desfire_maximum_capture_memory_and_public_file_remain_readable(
        self,
    ):
        source = sample(Model.DESFIRE)
        payload = bytes(index % 256 for index in range(8160))
        settings = b"\0\0\xee\xee" + len(payload).to_bytes(3, "little")
        memory = (
            b"DFD1\1\1"
            + b"\x12\x34\x56\0\2\x0f\x81\1\x37\1"
            + b"\1\0\7\0"
            + settings
            + b"\0"
            + len(payload).to_bytes(4, "little")
            + payload
        )
        self.assertEqual(len(memory), 8192)
        self.load(replace(source, memory=memory))
        binary = self.capture()
        exported = self.decode(binary)
        self.assertEqual(exported.memory, memory)
        self.assertEqual(exported.metadata, source.metadata)
        self.reload(binary)
        self.select_iso4(exported)
        self.assertEqual(
            self.exchange(crc_frame(b"\x02\x5a\x12\x34\x56")),
            crc_frame(b"\x02\0"),
        )
        # Read a bounded public file slice through the reloaded protocol state.
        command = b"\x02\xbd\1\0\0\0\x10\0\0"
        self.assertEqual(
            self.exchange(crc_frame(command)),
            crc_frame(b"\x02\0" + payload[:16]),
        )
