"""Public session ownership, wire contracts and no-retry mutation boundaries."""

import ctypes
import unittest

from ..api import test_library as library_tests
from .test_doc_recipes import Tag as NfcaTag
from ..api.test_library import Tag, OK, BUSY, RF_ERROR, NO_TAG, ERROR


class Options(ctypes.Structure):
    _fields_ = [
        ("expect_response", ctypes.c_int),
        ("timeout_us", ctypes.c_uint32),
    ]


class NfcaTx(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p),
        ("bits", ctypes.c_uint16),
        ("parity", ctypes.c_int),
        ("parity_bits", ctypes.c_void_p),
    ]


class NfcaRx(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p),
        ("capacity", ctypes.c_size_t),
        ("parity", ctypes.c_void_p),
        ("parity_capacity", ctypes.c_size_t),
        ("bits", ctypes.c_uint16),
        ("parity_valid", ctypes.c_bool),
    ]


class NfcaReply(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p),
        ("capacity", ctypes.c_size_t),
        ("size", ctypes.c_size_t),
        ("short", ctypes.c_bool),
        ("short4", ctypes.c_uint8),
    ]


class NfcvTx(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("size", ctypes.c_size_t)]


class NfcvRx(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p),
        ("capacity", ctypes.c_size_t),
        ("size", ctypes.c_size_t),
        ("crc", ctypes.c_bool),
    ]


class NfcvReply(ctypes.Structure):
    _fields_ = NfcvRx._fields_[:-1]


class Identity(ctypes.Structure):
    _fields_ = [("uid", ctypes.c_uint8 * 8), ("dsfid", ctypes.c_uint8)]


NO_RESPONSE, INVALID_RESPONSE, BUFFER_TOO_SMALL = 11, 12, 13


class PublicReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        library_tests.LibraryTests.setUpClass.__func__(cls)

    def setUp(self):
        library_tests.LibraryTests.setUp(self)
        n = self.native
        n.host_public_reply.argtypes = (
            ctypes.c_void_p,
            ctypes.c_uint16,
            ctypes.c_bool,
            ctypes.c_int,
            ctypes.c_bool,
        )
        n.host_public_tx.argtypes = (ctypes.c_void_p,)
        n.host_public_crc.argtypes = (
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_bool,
        )
        n.nfca_reader_select.argtypes = (ctypes.POINTER(Tag),)
        n.nfcv_reader_inventory.argtypes = (ctypes.POINTER(Identity),)
        for name, tx, rx in [
            ("nfca", NfcaTx, NfcaRx),
            ("nfcv", NfcvTx, NfcvRx),
        ]:
            getattr(n, f"{name}_reader_exchange_frame").argtypes = (
                ctypes.POINTER(tx),
                ctypes.POINTER(rx),
                ctypes.POINTER(Options),
            )
        for name, reply in [("nfca", NfcaReply), ("nfcv", NfcvReply)]:
            getattr(n, f"{name}_reader_exchange").argtypes = (
                ctypes.c_void_p,
                ctypes.c_size_t,
                ctypes.POINTER(reply),
                ctypes.POINTER(Options),
            )
        self.assertEqual(n.nfc_init(None), OK)

    def response(self, data, nfcv=False, parity=True, status=OK, bits=None):
        self.native.host_public_reply(
            data, len(data) * 8 if bits is None else bits, parity, status, nfcv
        )

    def crc(self, data, nfcv=False):
        buf = ctypes.create_string_buffer(data + b"\0\0", len(data) + 2)
        self.native.host_public_crc(buf, len(data), nfcv)
        return bytes(buf)

    def test_roles_and_discovery_silence_preserve_output(self):
        n = self.native
        self.assertEqual(n.nfca_reader_start(), OK)
        self.assertEqual(n.nfcv_reader_start(), BUSY)
        tag = Tag()
        ctypes.memset(ctypes.byref(tag), 0xA5, ctypes.sizeof(tag))
        before = bytes(tag)
        self.assertEqual(n.nfca_reader_select(ctypes.byref(tag)), NO_TAG)
        self.assertEqual(bytes(tag), before)
        self.assertEqual(n.nfc_state(), 6)
        self.assertEqual(n.nfc_stop(), OK)
        self.assertEqual(n.nfcv_reader_start(), OK)
        n.host_reader_config(1, 1, 0)
        identity = Identity()
        ctypes.memset(ctypes.byref(identity), 0xA5, ctypes.sizeof(identity))
        before = bytes(identity)
        self.assertEqual(
            n.nfcv_reader_inventory(ctypes.byref(identity)), NO_TAG
        )
        self.assertEqual(bytes(identity), before)
        self.assertEqual(n.nfc_state(), 4)
        self.assertEqual(n.host_public_count(), 1)
        n.host_reader_config(1, 0, 0)
        self.assertEqual(n.nfcv_reader_inventory(ctypes.byref(identity)), OK)
        self.assertEqual(bytes(identity.uid), bytes.fromhex("E004010102030405"))
        self.assertEqual(n.host_library_count(5), 0)  # Continuous field.
        self.assertEqual(n.nfc_stop(), OK)

    def test_plain_nfca_crc_short_nack_and_aliasing(self):
        n = self.native
        self.assertEqual(n.nfca_reader_start(), OK)
        buf = ctypes.create_string_buffer(b"\x30\x04" + b"\xA5" * 30, 32)
        reply = NfcaReply(ctypes.addressof(buf), 32)
        payload = bytes(range(16))
        self.response(self.crc(payload))
        self.assertEqual(
            n.nfca_reader_exchange(buf, 2, ctypes.byref(reply), None), OK
        )
        self.assertEqual(bytes(buf)[: reply.size], payload)
        sent = ctypes.create_string_buffer(32)
        self.assertEqual(n.host_public_tx(sent), 32)
        self.assertEqual(bytes(sent)[:4], bytes.fromhex("30 04 26 EE"))
        self.response(b"\x04", bits=4)
        self.assertEqual(
            n.nfca_reader_exchange(b"\x30\x04", 2, ctypes.byref(reply), None),
            OK,
        )
        self.assertEqual((reply.short, reply.short4, reply.size), (True, 4, 0))
        self.assertEqual(n.nfc_state(), 6)

    def test_raw_nfca_preserves_exact_bits_and_parity_observations(self):
        n = self.native
        self.assertEqual(n.nfca_reader_start(), OK)
        request = ctypes.create_string_buffer(b"\x26")
        tx = NfcaTx(ctypes.addressof(request), 7, 2, None)
        output = ctypes.create_string_buffer(34)
        rx = NfcaRx(ctypes.addressof(output), 34)
        self.response(b"\x44\x00", parity=False)
        self.assertEqual(
            n.nfca_reader_exchange_frame(
                ctypes.byref(tx),
                ctypes.byref(rx),
                ctypes.byref(Options(0, 1234)),
            ),
            OK,
        )
        self.assertEqual((rx.bits, rx.parity_valid), (16, False))
        sent = ctypes.create_string_buffer(32)
        self.assertEqual(n.host_public_tx(sent), 7)
        self.assertEqual(bytes(sent)[0], 0x26)
        self.assertEqual(n.host_public_timeout(), 1234)

    def test_invalid_inputs_never_transmit_and_small_output_never_retries(self):
        n = self.native
        self.assertEqual(n.nfca_reader_start(), OK)
        buf = ctypes.create_string_buffer(b"\xA5", 1)
        reply = NfcaReply(ctypes.addressof(buf), 1)
        for size, options in [
            (0, None),
            (31, None),
            (2, Options(0, 5001)),
            (2, Options(9, 0)),
        ]:
            ptr = None if options is None else ctypes.byref(options)
            self.assertEqual(
                n.nfca_reader_exchange(
                    b"\x30\x04" * 16, size, ctypes.byref(reply), ptr
                ),
                1,
            )
        self.assertEqual(n.host_public_count(), 0)
        self.response(self.crc(b"1234"))
        self.assertEqual(
            n.nfca_reader_exchange(b"\x30\x04", 2, ctypes.byref(reply), None),
            BUFFER_TOO_SMALL,
        )
        self.assertEqual(
            (bytes(buf), reply.size, n.host_public_count()), (b"\xA5", 0, 1)
        )
        self.assertEqual(n.nfc_state(), 6)

    def test_expected_silence_and_unknown_outcome_are_distinct(self):
        n = self.native
        self.assertEqual(n.nfca_reader_start(), OK)
        self.response(b"", status=NO_RESPONSE)
        reply = NfcaReply()
        self.assertEqual(
            n.nfca_reader_exchange(
                b"\x50\0", 2, ctypes.byref(reply), ctypes.byref(Options(1, 0))
            ),
            OK,
        )
        self.assertEqual(n.nfc_state(), 6)
        data = ctypes.create_string_buffer(32)
        reply = NfcaReply(ctypes.addressof(data), 32)
        self.assertEqual(
            n.nfca_reader_exchange(b"\x30\0", 2, ctypes.byref(reply), None),
            NO_RESPONSE,
        )
        self.assertEqual(n.nfc_state(), ERROR)
        self.assertEqual(n.nfc_init(None), RF_ERROR)
        self.assertEqual(
            n.nfca_reader_exchange(b"\x30\0", 2, ctypes.byref(reply), None),
            RF_ERROR,
        )
        self.assertEqual(n.host_public_count(), 2)
        self.assertEqual(n.nfc_stop(), OK)

    def test_nfcv_card_error_and_raw_invalid_crc_are_exposed(self):
        n = self.native
        self.assertEqual(n.nfcv_reader_start(), OK)
        output = ctypes.create_string_buffer(b"\xA5" * 24, 24)
        reply = NfcvReply(ctypes.addressof(output), 24)
        self.response(self.crc(b"\x01\x10", True), nfcv=True)
        self.assertEqual(
            n.nfcv_reader_exchange(
                b"\x02\x20\xFF", 3, ctypes.byref(reply), None
            ),
            OK,
        )
        self.assertEqual(bytes(output)[: reply.size], b"\x01\x10")
        command = ctypes.create_string_buffer(self.crc(b"\x02\x20\0", True))
        tx = NfcvTx(ctypes.addressof(command), 5)
        rx = NfcvRx(ctypes.addressof(output), 24)
        self.response(b"\0\x01\x02\x03", nfcv=True)
        self.assertEqual(
            n.nfcv_reader_exchange_frame(
                ctypes.byref(tx), ctypes.byref(rx), None
            ),
            OK,
        )
        self.assertEqual((rx.size, rx.crc, n.nfc_state()), (4, False, 4))
        self.assertEqual(
            n.nfcv_reader_exchange(b"\x02\x20\0", 3, ctypes.byref(reply), None),
            INVALID_RESPONSE,
        )
        self.assertEqual(n.nfc_state(), ERROR)
        self.assertEqual(n.host_public_count(), 3)

    def test_nfcv_unsupported_flags_and_expected_silence(self):
        n = self.native
        self.assertEqual(n.nfcv_reader_start(), OK)
        reply = NfcvReply()
        for flags in (0, 3, 10):
            self.assertEqual(
                n.nfcv_reader_exchange(
                    bytes([flags, 2]),
                    2,
                    ctypes.byref(reply),
                    ctypes.byref(Options(1, 0)),
                ),
                9,
            )
        self.assertEqual(n.host_public_count(), 0)
        self.assertEqual(
            n.nfcv_reader_exchange(
                b"\x02\x02", 2, ctypes.byref(reply), ctypes.byref(Options(1, 0))
            ),
            OK,
        )
        self.assertEqual(n.host_public_count(), 1)
        self.assertEqual(n.host_library_count(5), 0)

    def test_custom_start_validates_handler_and_preserves_active_role(self):
        n = self.native
        handler_type = ctypes.CFUNCTYPE(
            None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p
        )
        handler = handler_type(lambda context, request, response: None)
        for name in ("nfca", "nfcv"):
            getattr(n, f"{name}_emulation_start").argtypes = (
                ctypes.c_void_p,
                ctypes.c_void_p,
            )
        self.assertEqual(n.nfca_emulation_start(None, None), 1)
        self.assertEqual(n.nfca_emulation_start(handler, None), OK)
        self.assertEqual(n.nfcv_emulation_start(None, None), 1)
        self.assertEqual(n.nfcv_emulation_start(handler, None), BUSY)
        self.assertEqual(n.nfc_state(), 2)
        self.assertEqual(n.nfc_stop(), OK)
        self.assertEqual(n.nfcv_emulation_start(handler, None), OK)
        self.assertEqual(n.nfc_state(), 3)
        self.assertEqual(n.nfca_reader_start(), BUSY)
        self.assertEqual(n.nfc_stop(), OK)
        n.host_library_fail(False, False, True)
        self.assertEqual(n.nfca_emulation_start(handler, None), RF_ERROR)
        self.assertEqual(n.nfc_state(), ERROR)

    def test_recovery_session_does_not_grant_public_reader_ownership(self):
        n = self.native
        n.host_reader_config(1, 9, 0)
        self.assertEqual(
            n.nfca_select(ctypes.byref(NfcaTag(uid_size_bytes=4, sak=8)), 1), OK
        )
        self.assertEqual(n.nfc_state(), 6)
        self.assertEqual(n.nfca_reader_select(ctypes.byref(Tag())), BUSY)
        self.assertEqual(n.host_public_count(), 0)


if __name__ == "__main__":
    unittest.main()
