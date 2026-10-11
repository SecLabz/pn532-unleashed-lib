"""Frozen synthetic images validated by the reference Rust CLI.

This helper reads JSON fixture sections; it neither parses nor validates PNU1
or Flipper files. Mutated sections go directly to the C image validator.
"""

from dataclasses import dataclass
from enum import IntEnum
import json
from pathlib import Path


class Model(IntEnum):
    CLASSIC_1K = 1
    NTAG215 = 2
    SLIX = 3
    SLIX_L = 4
    CLASSIC_MINI = 5
    CLASSIC_2K = 6
    CLASSIC_4K = 7
    NTAG213 = 8
    NTAG216 = 9
    ULTRALIGHT = 10
    ULTRALIGHT_EV1_48 = 11
    ULTRALIGHT_EV1_128 = 12
    ISO15693 = 13
    ISO_DEP = 14
    ISO4 = 15
    ST25TA = 16
    MIFARE_PLUS = 17
    DESFIRE = 18


CLASSIC_TYPES = {
    Model.CLASSIC_MINI: ("Mini", 20, b"\x00\x04", b"\x09"),
    Model.CLASSIC_1K: ("1K", 64, b"\x00\x04", b"\x08"),
    Model.CLASSIC_2K: ("2K", 128, b"\x00\x04", b"\x08"),
    Model.CLASSIC_4K: ("4K", 256, b"\x00\x02", b"\x18"),
}
TYPE2_TYPES = {
    Model.ULTRALIGHT: ("Mifare Ultralight", 16, 0, b""),
    Model.ULTRALIGHT_EV1_48: (
        "Mifare Ultralight 11",
        20,
        16,
        bytes.fromhex("00 04 03 01 01 00 0B 03"),
    ),
    Model.ULTRALIGHT_EV1_128: (
        "Mifare Ultralight 21",
        41,
        37,
        bytes.fromhex("00 04 03 01 01 00 0E 03"),
    ),
    Model.NTAG213: (
        "NTAG213",
        45,
        41,
        bytes.fromhex("00 04 04 02 01 00 0F 03"),
    ),
    Model.NTAG215: (
        "NTAG215",
        135,
        131,
        bytes.fromhex("00 04 04 02 01 00 11 03"),
    ),
    Model.NTAG216: (
        "NTAG216",
        231,
        227,
        bytes.fromhex("00 04 04 02 01 00 13 03"),
    ),
}

ISO4_TYPES = {
    Model.ISO_DEP: "UID-only",
    Model.ISO4: "ISO4 local",
    Model.ST25TA: "ST25TA local",
    Model.MIFARE_PLUS: "Mifare Plus activation",
}


@dataclass(frozen=True)
class Fixture:
    model: Model
    uid: bytes
    memory: bytes
    metadata: bytes
    secrets: bytes
    binary: bytes


def sample(model: Model) -> Fixture:
    path = Path(__file__).resolve().parents[1] / "fixtures/images"
    fields = json.loads((path / (model.name.lower() + ".json")).read_text())
    return Fixture(
        model,
        *(
            bytes.fromhex(fields[key])
            for key in ("uid", "memory", "metadata", "secrets", "binary")
        )
    )
