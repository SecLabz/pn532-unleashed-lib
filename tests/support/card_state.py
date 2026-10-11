"""Map frozen fixture sections to typed state; no file framing or validation.

These test-only field assignments keep standalone card tests independent of
application codecs. Binary-format compatibility is tested by the parent repo.
"""

import ctypes as c
from dataclasses import replace
from .fixtures import Model, TYPE2_TYPES, sample


class Type2State(c.Structure):
    _fields_ = [
        ("version", c.c_uint8 * 8),
        ("signature", c.c_uint8 * 32),
        ("counters", c.c_uint32 * 3),
        ("tearing", c.c_uint8 * 3),
        ("authentication_attempts", c.c_uint32),
    ]


FLAGS = (
    "dsfid_locked",
    "afi_locked",
    "eas_active",
    "eas_locked",
    "privacy_active",
    "eas_password_protected",
    "afi_password_protected",
)


class NfcvState(c.Structure):
    _fields_ = [
        ("locks", c.c_void_p),
        ("block_count", c.c_uint8),
        ("dsfid", c.c_uint8),
        ("afi", c.c_uint8),
        ("ic_ref", c.c_uint8),
        *[(n, c.c_bool) for n in FLAGS],
        *[
            (n + "_password", c.c_uint8 * 4)
            for n in ("privacy", "destroy", "eas_afi")
        ],
    ]


class App(c.Structure):
    _fields_ = [
        ("aid", c.c_uint8 * 3),
        ("settings_state", c.c_uint8),
        ("key_settings", c.c_void_p),
        ("key_settings_len", c.c_uint8),
        ("key_versions", c.c_void_p),
        ("key_versions_len", c.c_uint8),
        ("files_state", c.c_uint8),
        ("first_file", c.c_uint8),
        ("file_count", c.c_uint8),
    ]


class File(c.Structure):
    _fields_ = [
        ("id", c.c_uint8),
        ("settings_state", c.c_uint8),
        ("settings", c.c_void_p),
        ("settings_len", c.c_uint16),
        ("data_state", c.c_uint8),
        ("data", c.c_void_p),
        ("data_len", c.c_uint32),
    ]


class Metadata(c.Structure):
    _fields_ = [
        ("reports_unavailable", c.c_bool),
        ("version", c.c_uint8 * 28),
        ("free_memory_present", c.c_bool),
        ("free_memory", c.c_uint32),
        ("picc_settings_state", c.c_uint8),
        ("picc_key_settings", c.c_void_p),
        ("picc_key_settings_len", c.c_uint8),
        ("picc_key_versions", c.c_void_p),
        ("picc_key_versions_len", c.c_uint8),
        ("applications_state", c.c_uint8),
    ]


class Capture(c.Structure):
    _fields_ = [
        ("metadata", Metadata),
        ("apps", App * 28),
        ("app_count", c.c_uint8),
        ("files", File * 64),
        ("file_count", c.c_uint8),
    ]


class Iso4State(c.Structure):
    _fields_ = [
        ("atqa", c.c_uint8 * 2),
        ("sak", c.c_uint8),
        ("ats", c.c_uint8 * 32),
        ("ats_size_bytes", c.c_uint8),
        ("capture", Capture),
    ]


class State(c.Union):
    _fields_ = [("type2", Type2State), ("nfcv", NfcvState), ("iso4", Iso4State)]


class Image(c.Structure):
    _fields_ = [
        ("model", c.c_int),
        ("uid", c.c_void_p),
        ("uid_size_bytes", c.c_uint16),
        ("memory", c.c_void_p),
        ("memory_size_bytes", c.c_uint16),
        ("state", State),
    ]


def fixture_view(source):
    buffers = []

    def address(data):
        buf = c.create_string_buffer(data)
        buffers.append(buf)
        return c.addressof(buf)

    image = Image(
        model=source.model,
        uid=address(source.uid),
        uid_size_bytes=len(source.uid),
        memory=address(source.memory),
        memory_size_bytes=len(source.memory),
    )
    meta = source.metadata
    if source.model in TYPE2_TYPES and source.model != Model.ULTRALIGHT:
        state = image.state.type2
        state.version[:] = meta[4:12]
        state.signature[:] = meta[12:44]
        state.counters[:] = [
            int.from_bytes(meta[i : i + 3], "little") for i in (44, 47, 50)
        ]
        state.tearing[:] = meta[53:56]
        state.authentication_attempts = int.from_bytes(meta[56:60], "little")
    elif source.model in (Model.ISO15693, Model.SLIX, Model.SLIX_L):
        state = image.state.nfcv
        state.locks = address(meta[12:])
        state.block_count = meta[9]
        state.dsfid, state.afi, state.ic_ref = meta[5:8]
        for i, name in enumerate(FLAGS):
            setattr(state, name, bool(meta[4] & (1 << i)))
        if source.model == Model.SLIX_L:
            state.privacy_password[:] = source.secrets[:4]
            state.destroy_password[:] = source.secrets[4:8]
        if source.secrets:
            state.eas_afi_password[:] = source.secrets[-4:]
    elif source.model >= Model.ISO_DEP:
        state = image.state.iso4
        offset = 5 if source.model == Model.DESFIRE else 4
        state.atqa[:] = meta[offset : offset + 2]
        state.sak, state.ats_size_bytes = meta[offset + 2 : offset + 4]
        state.ats[: state.ats_size_bytes] = meta[
            offset + 4 : offset + 4 + state.ats_size_bytes
        ]
        if source.model == Model.DESFIRE:
            image.memory = None
            image.memory_size_bytes = 0
            capture = state.capture
            observation = capture.metadata
            observation.reports_unavailable = meta[4] == 2
            pos = offset + 4 + state.ats_size_bytes
            observation.version[:] = meta[pos : pos + 28]
            observation.free_memory_present = bool(meta[pos + 28])
            observation.free_memory = int.from_bytes(
                meta[pos + 29 : pos + 33], "little"
            )
            observation.picc_settings_state = meta[pos + 33]

            def field(data, pos, obj, name, width=1):
                size = int.from_bytes(data[pos : pos + width], "little")
                pos += width
                setattr(obj, name + "_len", size)
                setattr(obj, name, address(data[pos : pos + size]))
                return pos + size

            pos = field(meta, pos + 34, observation, "picc_key_settings")
            pos = field(meta, pos, observation, "picc_key_versions")
            observation.applications_state = meta[pos]
            data = source.memory
            capture.app_count = data[5]
            pos, index = 6, 0
            for app in capture.apps[: capture.app_count]:
                app.aid[:] = data[pos : pos + 3]
                app.settings_state = data[pos + 3]
                pos = field(data, pos + 4, app, "key_settings")
                pos = field(data, pos, app, "key_versions")
                if observation.reports_unavailable:
                    app.files_state = data[pos]
                    pos += 1
                app.first_file, app.file_count = index, data[pos]
                pos += 1
                for file in capture.files[index : index + app.file_count]:
                    file.id, file.settings_state = data[pos : pos + 2]
                    pos = field(data, pos + 2, file, "settings", 2)
                    file.data_state = data[pos]
                    pos = field(data, pos + 1, file, "data", 4)
                index += app.file_count
            capture.file_count = index
    image._buffers = buffers
    return image


def fixture_sections(image):
    """Project typed state onto existing fixture fields for protocol assertions."""
    model = Model(image.model)
    uid = c.string_at(image.uid, image.uid_size_bytes)
    memory = c.string_at(image.memory, image.memory_size_bytes)
    meta, secrets = b"", b""

    def n(value, width=1):
        return int(value).to_bytes(width, "little")

    def blob(obj, name, width=1):
        size = getattr(obj, name + "_len")
        return n(size, width) + c.string_at(getattr(obj, name), size)

    if model in TYPE2_TYPES and model != Model.ULTRALIGHT:
        s = image.state.type2
        meta = (
            b"NTA1"
            + bytes(s.version)
            + bytes(s.signature)
            + b"".join(n(value, 3) for value in s.counters)
            + bytes(s.tearing)
            + n(s.authentication_attempts, 4)
        )
    elif model in (Model.ISO15693, Model.SLIX, Model.SLIX_L):
        s = image.state.nfcv
        flags = sum(int(getattr(s, name)) << i for i, name in enumerate(FLAGS))
        meta = (
            b"VIC1"
            + bytes((flags, s.dsfid, s.afi, s.ic_ref, 4, s.block_count, 0, 0))
            + c.string_at(s.locks, s.block_count)
        )
        if model == Model.SLIX_L:
            secrets = bytes(s.privacy_password) + bytes(s.destroy_password)
        if model != Model.ISO15693:
            secrets += bytes(s.eas_afi_password)
    elif model >= Model.ISO_DEP:
        s = image.state.iso4
        activation = (
            bytes(s.atqa)
            + bytes((s.sak, s.ats_size_bytes))
            + bytes(s.ats[: s.ats_size_bytes])
        )
        if model != Model.DESFIRE:
            meta = b"DFU1" + activation
        else:
            cap = s.capture
            m = cap.metadata
            version = n(2 if m.reports_unavailable else 1)
            meta = (
                b"DFA1"
                + version
                + activation
                + bytes(m.version)
                + n(m.free_memory_present)
                + n(m.free_memory, 4)
                + n(m.picc_settings_state)
                + blob(m, "picc_key_settings")
                + blob(m, "picc_key_versions")
                + n(m.applications_state)
            )
            memory = b"DFD1" + version + n(cap.app_count)
            for app in cap.apps[: cap.app_count]:
                memory += (
                    bytes(app.aid)
                    + n(app.settings_state)
                    + blob(app, "key_settings")
                    + blob(app, "key_versions")
                )
                if m.reports_unavailable:
                    memory += n(app.files_state)
                memory += n(app.file_count)
                for f in cap.files[
                    app.first_file : app.first_file + app.file_count
                ]:
                    memory += (
                        n(f.id)
                        + n(f.settings_state)
                        + blob(f, "settings", 2)
                        + n(f.data_state)
                        + blob(f, "data", 4)
                    )
    # Construct only fixture comparison bytes; never accepted as library input.
    binary = (
        b"PNU1"
        + bytes((1, model, len(uid), 0))
        + n(len(memory), 2)
        + n(len(meta), 2)
        + n(len(secrets), 2)
        + bytes(2)
        + uid
        + memory
        + meta
        + secrets
    )
    return replace(
        sample(model),
        uid=uid,
        memory=memory,
        metadata=meta,
        secrets=secrets,
        binary=binary,
    )
