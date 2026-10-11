import ctypes
from pathlib import Path
import platform
import subprocess
import unittest


class Activation(ctypes.Structure):
    _fields_ = [
        ("state", ctypes.c_int),
        ("uid", ctypes.c_uint8 * 10),
        ("uid_size_bytes", ctypes.c_uint8),
        ("atqa", ctypes.c_uint8 * 2),
        ("sak", ctypes.c_uint8),
        ("selected_cascade", ctypes.c_uint8),
        ("halt_started_us", ctypes.c_uint32),
    ]


class Request(ctypes.Structure):
    _fields_ = [
        ("bytes", ctypes.c_void_p),
        ("parity", ctypes.c_void_p),
        ("size_bytes", ctypes.c_uint16),
        ("short_frame", ctypes.c_bool),
        ("parity_ok", ctypes.c_bool),
        ("crc_ok", ctypes.c_bool),
        ("timestamp_us", ctypes.c_uint32),
    ]


class Response(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("bytes", ctypes.c_uint8 * 542),
        ("parity", ctypes.c_uint8 * 542),
        ("size_bytes", ctypes.c_uint16),
        ("short4", ctypes.c_uint8),
    ]


class Store(ctypes.Structure):
    _fields_ = [
        ("block_size_bytes", ctypes.c_uint8),
        ("block_count", ctypes.c_uint8),
        ("writable", ctypes.c_bool),
        ("memory", ctypes.c_uint8 * 256),
        ("locks", ctypes.c_uint8 * 64),
    ]


class VicinityResponse(ctypes.Structure):
    _fields_ = [
        ("bytes", ctypes.c_uint8 * 323),
        ("size_bytes", ctypes.c_uint16),
    ]


class PublicProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        output = root / ".cache/tests"
        output.mkdir(parents=True, exist_ok=True)
        library = output / "public-protocols.dylib"
        sources = (
            "tests/api/public_protocol_bridge.c",
            "src/protocols/nfca/nfca_activation.c",
            "src/protocols/nfcv/frame.c",
            "src/cards/nfcv/nfcv_memory_store.c",
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
                "-I" + str(root / "include"),
                "-I" + str(root / "src"),
                *(str(root / path) for path in sources),
                "-o",
                str(library),
            ],
            check=True,
        )
        cls.native = ctypes.CDLL(str(library))
        cls.native.nfca_activation_init.argtypes = (
            ctypes.POINTER(Activation),
            ctypes.c_void_p,
            ctypes.c_uint8,
            ctypes.c_void_p,
            ctypes.c_uint8,
        )
        cls.native.nfca_activation_init.restype = ctypes.c_bool
        cls.native.nfca_activation_handle.argtypes = (
            ctypes.POINTER(Activation),
            ctypes.POINTER(Request),
            ctypes.POINTER(Response),
            ctypes.POINTER(ctypes.c_int),
        )
        cls.native.nfcv_memory_store_init.argtypes = (
            ctypes.POINTER(Store),
            ctypes.c_uint8,
            ctypes.c_uint8,
            ctypes.c_bool,
            ctypes.c_void_p,
            ctypes.c_uint16,
            ctypes.c_void_p,
        )
        cls.native.nfcv_memory_store_init.restype = ctypes.c_bool
        for name in ("write", "lock", "security"):
            getattr(cls.native, "nfcv_memory_store_" + name).argtypes = (
                ctypes.POINTER(Store),
                ctypes.POINTER(VicinityResponse),
                ctypes.c_void_p,
                ctypes.c_uint8,
            )
        cls.native.host_protocol_crc.argtypes = (
            ctypes.c_bool,
            ctypes.c_void_p,
            ctypes.c_size_t,
        )
        cls.native.host_protocol_crc.restype = ctypes.c_uint16

    def exchange(
        self,
        activation,
        data,
        short=False,
        timestamp_us=0,
        parity_ok=True,
        crc_ok=True,
    ):
        storage = ctypes.create_string_buffer(data)
        request = Request(
            ctypes.addressof(storage),
            None,
            len(data),
            short,
            parity_ok,
            crc_ok,
            timestamp_us,
        )
        response = Response()
        event = ctypes.c_int()
        result = self.native.nfca_activation_handle(
            ctypes.byref(activation),
            ctypes.byref(request),
            ctypes.byref(response),
            ctypes.byref(event),
        )
        return result, bytes(response.bytes[: response.size_bytes]), event.value

    def test_activation_rejects_invalid_identity_without_partial_reset(self):
        activation = Activation()
        ctypes.memset(ctypes.byref(activation), 0xA5, ctypes.sizeof(activation))
        original = bytes(activation)
        for uid, length, atqa in (
            (b"123", 3, b"\4\0"),
            (bytes(9), 9, b"\4\0"),
            (None, 4, b"\4\0"),
            (b"1234", 4, None),
        ):
            self.assertFalse(
                self.native.nfca_activation_init(
                    ctypes.byref(activation), uid, length, atqa, 8
                )
            )
            self.assertEqual(bytes(activation), original)

    def test_activation_selection_halt_and_wakeup_use_public_responses(self):
        activation = Activation()
        self.assertTrue(
            self.native.nfca_activation_init(
                ctypes.byref(activation),
                bytes.fromhex("DEADBEEF"),
                4,
                b"\4\0",
                8,
            )
        )
        self.assertEqual(
            self.exchange(activation, b"\x26", True), (2, b"\4\0", 1)
        )
        anticollision = bytes.fromhex("DE AD BE EF 22")
        self.assertEqual(
            self.exchange(activation, b"\x93\x20"), (2, anticollision, 0)
        )
        self.assertEqual(
            self.exchange(activation, b"\x93\x70" + anticollision + b"\0\0"),
            (2, bytes.fromhex("08 B6 DD"), 2),
        )
        self.assertEqual(
            self.exchange(activation, b"\x50\0\0\0", timestamp_us=1000),
            (1, b"", 3),
        )
        self.assertEqual(
            self.exchange(activation, b"\x26", True, 1001), (1, b"", 0)
        )
        self.assertEqual(
            self.exchange(activation, b"\x52", True, 1002), (2, b"\4\0", 1)
        )

    def test_selection_validates_each_uid_cascade_before_changing_state(self):
        identities = (
            ("DE AD BE EF", ("DE AD BE EF",)),
            ("04 11 22 33 44 55 66", ("88 04 11 22", "33 44 55 66")),
            (
                "04 11 22 33 44 55 66 77 88 99",
                ("88 04 11 22", "88 33 44 55", "66 77 88 99"),
            ),
        )
        for identity, fragments in identities:
            uid = bytes.fromhex(identity)
            activation = Activation()
            self.assertTrue(
                self.native.nfca_activation_init(
                    ctypes.byref(activation), uid, len(uid), b"\4\0", 8
                )
            )
            self.exchange(activation, b"\x26", True)
            for cascade, fragment_text in enumerate(fragments):
                command = (0x93, 0x95, 0x97)[cascade]
                fragment = bytes.fromhex(fragment_text)
                bcc = fragment[0] ^ fragment[1] ^ fragment[2] ^ fragment[3]
                anticollision = fragment + bytes((bcc,))
                self.assertEqual(
                    self.exchange(activation, bytes((command, 0x20))),
                    (2, anticollision, 0),
                )
                select = bytes((command, 0x70)) + anticollision + b"\0\0"
                wrong_uid = bytearray(select)
                wrong_uid[2] ^= 1
                original = bytes(activation)
                for data, short, parity, crc in (
                    (select[:-1], False, True, True),
                    (select + b"\0", False, True, True),
                    (select, True, True, True),
                    (select, False, False, True),
                    (select, False, True, False),
                    (bytes(wrong_uid), False, True, True),
                    (bytes((command, 0x20)) + select[2:], False, True, True),
                ):
                    with self.subTest(uid=identity, cascade=cascade, data=data):
                        self.assertEqual(
                            self.exchange(
                                activation,
                                data,
                                short=short,
                                parity_ok=parity,
                                crc_ok=crc,
                            ),
                            (0, b"", 0),
                        )
                        self.assertEqual(bytes(activation), original)
                result, response, event = self.exchange(activation, select)
                last = cascade + 1 == len(fragments)
                self.assertEqual(result, 2)
                self.assertEqual(len(response), 3)
                self.assertEqual(response[0], 8 if last else 4)
                self.assertEqual(event, 2 if last else 0)

    def test_memory_init_requires_complete_known_geometry_and_lock_state(self):
        store = Store()
        ctypes.memset(ctypes.byref(store), 0xA5, ctypes.sizeof(store))
        original = bytes(store)
        for size, count, data, locks in (
            (0, 8, bytes(32), bytes(8)),
            (4, 29, bytes(116), bytes(29)),
            (4, 8, bytes(31), bytes(8)),
            (4, 8, None, bytes(8)),
            (4, 8, bytes(32), None),
            (4, 8, bytes(32), b"\2" * 8),
        ):
            self.assertFalse(
                self.native.nfcv_memory_store_init(
                    ctypes.byref(store),
                    size,
                    count,
                    True,
                    data,
                    len(data) if data is not None else 32,
                    locks,
                )
            )
            self.assertEqual(bytes(store), original)
        self.assertTrue(
            self.native.nfcv_memory_store_init(
                ctypes.byref(store), 4, 8, True, bytes(range(32)), 32, bytes(8)
            )
        )
        self.assertEqual(bytes(store.memory[:32]), bytes(range(32)))
        self.assertEqual(bytes(store.memory[32:]), bytes(224))

    def test_memory_locks_prevent_write_and_oversized_security_request(self):
        store, response = Store(), VicinityResponse()
        self.assertTrue(
            self.native.nfcv_memory_store_init(
                ctypes.byref(store), 4, 8, True, bytes(32), 32, bytes(8)
            )
        )
        self.native.nfcv_memory_store_write(
            ctypes.byref(store), ctypes.byref(response), b"\1ABCD", 5
        )
        self.assertEqual(bytes(store.memory[4:8]), b"ABCD")
        self.native.nfcv_memory_store_lock(
            ctypes.byref(store), ctypes.byref(response), b"\1", 1
        )
        self.native.nfcv_memory_store_write(
            ctypes.byref(store), ctypes.byref(response), b"\1EFGH", 5
        )
        self.assertEqual(
            bytes(response.bytes[: response.size_bytes]), b"\1\x12"
        )
        self.assertEqual(bytes(store.memory[4:8]), b"ABCD")
        self.native.nfcv_memory_store_security(
            ctypes.byref(store), ctypes.byref(response), b"\0\xff", 2
        )
        self.assertEqual(
            bytes(response.bytes[: response.size_bytes]), b"\1\x10"
        )

    def test_crc_matches_captured_inventory_and_classic_sak(self):
        inventory = bytes.fromhex("00 12 05 04 03 02 01 03 04 E0")
        self.assertEqual(
            self.native.host_protocol_crc(False, inventory, len(inventory)),
            0x9645,
        )
        self.assertEqual(
            self.native.host_protocol_crc(True, b"\x08", 1), 0xDDB6
        )


if __name__ == "__main__":
    unittest.main()
