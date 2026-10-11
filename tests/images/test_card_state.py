"""Complete typed card states, including hidden data and malformed captures."""

import ctypes
import unittest
from ..api import test_library as library
from ..support.card_state import fixture_view, fixture_sections
from ..support.fixtures import Model, sample


class CardStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        library.LibraryTests.setUpClass.__func__(cls)

    def setUp(self):
        library.LibraryTests.setUp(self)

    def test_all_typed_states_validate_and_preserve_fixture_sections(self):
        for model in Model:
            with self.subTest(model=model):
                source = sample(model)
                state = fixture_view(source)
                self.assertEqual(
                    self.native.image_validate(ctypes.byref(state)), 0
                )
                self.assertEqual(fixture_sections(state), source)

    def test_type2_requires_version_and_bounded_hidden_counters(self):
        state = fixture_view(sample(Model.NTAG215))
        state.state.type2.counters[1] = 0x1000000
        self.assertNotEqual(self.native.image_validate(ctypes.byref(state)), 0)
        state.state.type2.counters[1] = 0
        state.state.type2.version[6] ^= 1
        self.assertNotEqual(self.native.image_validate(ctypes.byref(state)), 0)

    def test_nfcv_lock_and_secret_state_is_model_specific(self):
        state = fixture_view(sample(Model.ISO15693))
        state.state.nfcv.eas_active = True
        self.assertNotEqual(self.native.image_validate(ctypes.byref(state)), 0)
        state = fixture_view(sample(Model.SLIX))
        state.state.nfcv.privacy_password[0] = 1
        self.assertNotEqual(self.native.image_validate(ctypes.byref(state)), 0)

    def test_capture_rejects_null_nonempty_blobs_and_invalid_ranges(self):
        state = fixture_view(sample(Model.DESFIRE))
        state.state.iso4.capture.file_count = 65
        self.assertNotEqual(self.native.image_validate(ctypes.byref(state)), 0)
        state = fixture_view(sample(Model.DESFIRE))
        state.state.iso4.capture.metadata.picc_key_settings = None
        state.state.iso4.capture.metadata.picc_key_settings_len = 2
        self.assertNotEqual(self.native.image_validate(ctypes.byref(state)), 0)
