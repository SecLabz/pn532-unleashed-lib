"""Trailer rights from NXP MF1S50YYX_V1 table 7, independently of emulation."""

import ctypes
import unittest

from . import test_behavior


class TrailerAccessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_behavior.CardBehaviorTests.setUpClass.__func__(cls)
        cls.native.mifare_trailer_key_permissions.argtypes = (
            ctypes.c_void_p,
            ctypes.c_uint8,
        )
        cls.native.mifare_trailer_key_permissions.restype = ctypes.c_uint8

    def test_all_eight_trailer_conditions_match_documented_field_permissions(
        self,
    ):
        # Bits: write A, read access, write access, read B, write B.
        # Rows use the published C1/C2/C3 order; readable B cannot authenticate.
        expected = (
            (27, 0),
            (31, 0),
            (10, 0),
            (2, 23),
            (2, 19),
            (2, 6),
            (2, 2),
            (2, 2),
        )
        for condition, rights in enumerate(expected):
            c1, c2, c3 = (condition >> 2 & 1, condition >> 1 & 1, condition & 1)
            trailer = bytearray(16)
            trailer[6] = ((7 if c2 else 15) << 4) | (7 if c1 else 15)
            trailer[7] = (c1 << 7) | (7 if c3 else 15)
            trailer[8] = (c3 << 7) | (c2 << 3)
            for key, permission in enumerate(rights):
                with self.subTest(condition=condition, key=key):
                    self.assertEqual(
                        self.native.mifare_trailer_key_permissions(
                            bytes(trailer), key
                        ),
                        permission,
                    )
