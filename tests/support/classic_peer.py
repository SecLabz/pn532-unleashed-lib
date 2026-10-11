"""Reader-side integration peer using the firmware's Crypto1 primitives.

This verifies card/session orchestration, not an independent Crypto1 algorithm
or RF/parity timing. The deferred card producer is run by the C host bridge.
"""

import ctypes
from collections.abc import Callable


class State(ctypes.Structure):
    _fields_ = [("odd", ctypes.c_uint32), ("even", ctypes.c_uint32)]


def crc_frame(payload: bytes) -> bytes:
    crc = 0x6363
    for value in payload:
        value ^= crc & 255
        value ^= (value << 4) & 255
        crc = (crc >> 8) ^ (value << 8) ^ (value << 3) ^ (value >> 4)
        crc &= 65535
    return payload + crc.to_bytes(2, "little")


def successor(nonce: bytes, clocks: int) -> bytes:
    value = int.from_bytes(nonce, "little")
    for _ in range(clocks):
        bit = (
            (value >> 16) ^ (value >> 18) ^ (value >> 19) ^ (value >> 21)
        ) & 1
        value = (value >> 1) | (bit << 31)
    return value.to_bytes(4, "little")


class ClassicPeer:
    def __init__(self, native: ctypes.CDLL, exchange: Callable, uid: bytes):
        self.native = native
        self.exchange = exchange
        self.uid = uid
        self.state = State()
        pointer = ctypes.POINTER(State)
        native.crypto1_init.argtypes = (pointer, ctypes.c_uint64)
        native.crypto1_init.restype = None
        native.crypto1_word.argtypes = (pointer, ctypes.c_uint32, ctypes.c_bool)
        native.crypto1_word.restype = ctypes.c_uint32
        native.crypto1_byte.argtypes = (pointer, ctypes.c_uint8, ctypes.c_bool)
        native.crypto1_byte.restype = ctypes.c_uint8
        native.crypto1_bit.argtypes = (pointer, ctypes.c_uint8, ctypes.c_bool)
        native.crypto1_bit.restype = ctypes.c_uint8

    def crypt(self, data: bytes, feedback: bool = False) -> bytes:
        return bytes(
            value
            ^ self.native.crypto1_byte(
                ctypes.byref(self.state), value if feedback else 0, False
            )
            for value in data
        )

    def authenticate(
        self, block: int, key: bytes, nested: bool = False
    ) -> bool:
        command = crc_frame(bytes((0x60, block)))
        if nested:
            command = self.crypt(command)
        challenge = self.exchange(command, crc=not nested)
        if len(challenge) != 4:
            return False
        self.native.crypto1_init(
            ctypes.byref(self.state), int.from_bytes(key, "big")
        )
        input_word = int.from_bytes(challenge, "big") ^ int.from_bytes(
            self.uid, "big"
        )
        stream = self.native.crypto1_word(
            ctypes.byref(self.state), input_word, nested
        )
        nonce = (
            (stream ^ int.from_bytes(challenge, "big")).to_bytes(4, "big")
            if nested
            else challenge
        )
        reader_nonce = self.crypt(b"\x12\x34\x56\x78", feedback=True)
        answer = self.crypt(successor(nonce, 64))
        response = self.exchange(reader_nonce + answer, crc=False)
        return len(response) == 4 and self.crypt(response) == successor(
            nonce, 96
        )

    def command(self, payload: bytes) -> bytes:
        return self.exchange(self.crypt(crc_frame(payload)), crc=False)

    def read(self, block: int) -> bytes:
        return self.crypt(self.command(bytes((0x30, block))))

    def nibble(self, data: bytes) -> int:
        if len(data) != 1:
            raise ValueError("Expected a four-bit card response")
        return data[0] ^ sum(
            self.native.crypto1_bit(ctypes.byref(self.state), 0, False) << bit
            for bit in range(4)
        )

    def write(self, block: int, data: bytes) -> tuple[int, int]:
        ack = self.nibble(self.command(bytes((0xA0, block))))
        response = self.exchange(self.crypt(crc_frame(data)), crc=False)
        return ack, self.nibble(response)
