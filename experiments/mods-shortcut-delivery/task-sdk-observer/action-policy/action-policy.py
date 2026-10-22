#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pure rules for supplied host settings and copied mixer frames.

No ambient I/O, device access, transport, clock, run ownership or readiness logic.
A matching frame is an offered-controller observation, not native consumption.
"""
import hashlib
import json
import re

DRIVER_SHA256 = '6f88798ec54785dc2ce9180e5c2839a1a8e8ec370c997bc7f48e8d1cc06e640f'
IDLE_SHA256 = '885b5464cd62b520bf336abb40af2016c2d84f73abbeaced7c649dddb048e7a3'
PROTECTED_SHA256 = 'd570e444f35aff7584058fac4b5241b4ecf56cd70f0998c4d3a4af831cec0067'
# Exact MIXER_KNOBS value set plus FX_BYTE from the pinned driver; not arbitrary offsets.
MIXER_OFFSETS = frozenset((10,11,12,13,15,22,36,37,38,39,41,42,43,44,
                         46,47,48,49,51,52,53,54,56,61,62,63,64,78))
FX_BYTE = 22
CRC_BYTES = frozenset((96,97))
COMPARISON_EXCLUDED = CRC_BYTES | {34}
PROTECTED_EXCLUDED = MIXER_OFFSETS | COMPARISON_EXCLUDED
MAX_STATE_BYTES = 4096


def _require(value, reason):
    if not value:
        raise ValueError(reason)


def _state(state):
    _require(type(state) is dict, 'mixer state must be a dict')
    _require(len(state) <= len(MIXER_OFFSETS), 'too many mixer settings')
    for key, value in state.items():
        _require(type(key) is int and key in MIXER_OFFSETS, 'unreviewed mixer offset')
        _require(type(value) is int and 0 <= value <= 255, 'mixer value must be an integer byte')
        _require(key != FX_BYTE or value & 1 == 0, 'FX ON must be released')


def parse_mixer_state(raw):
    """Missing file is None; otherwise exact UTF-8 JSON object with canonical keys."""
    if raw is None:
        return {}
    _require(type(raw) is bytes and 0 < len(raw) <= MAX_STATE_BYTES, 'invalid mixer-state bytes')
    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError as error:
        raise ValueError('mixer state is not UTF-8') from error

    def pairs(items):
        result = {}
        for key, value in items:
            _require(type(key) is str and re.fullmatch(r'0|[1-9][0-9]*', key) is not None,
                     'mixer key must be canonical decimal')
            _require(key not in result, 'duplicate mixer key')
            result[key] = value
        return result

    def constant(_):
        raise ValueError('nonfinite JSON value')

    try:
        decoded = json.loads(text, object_pairs_hook=pairs, parse_constant=constant)
    except (json.JSONDecodeError, RecursionError) as error:
        raise ValueError('invalid mixer JSON') from error
    _require(type(decoded) is dict, 'mixer JSON must be an object')
    result = {int(key): value for key, value in decoded.items()}
    _state(result)
    return result


def _counter(value):
    _require(type(value) is int and 1 <= value <= 254, 'counter outside 1..254')


def _frame(frame):
    _require(type(frame) is bytes and len(frame) == 152, 'expected exactly 152 immutable bytes')


def _neutral_shape(frame):
    _frame(frame)
    _counter(frame[34])
    _require(frame[6] == 8, 'press latch must be released')
    _require(frame[31] == frame[32] == frame[33] == frame[35] == 0,
             'panel and knob controls must be neutral')
    _require(frame[24] & 0x10 == 0, 'MIXER SETTINGS must be released')
    _require(frame[FX_BYTE] & 1 == 0, 'FX ON must be released')
    _require(frame[29] == 255, 'X-PAD must be released')
    protected = bytes(value for offset, value in enumerate(frame) if offset not in PROTECTED_EXCLUDED)
    _require(hashlib.sha256(protected).hexdigest() == PROTECTED_SHA256,
             'non-mixer bytes differ from pinned neutral template')


def neutral_frame(idle152, state, counter):
    """Build only reviewed mixer overrides/counter on the exact supplied template.

    Original cosmetic CRC bytes remain; the kernel stamps its own read result.
    Does not read settings/counter files or update either of them.
    """
    _frame(idle152)
    _require(hashlib.sha256(idle152).hexdigest() == IDLE_SHA256, 'idle template hash mismatch')
    _state(state)
    _counter(counter)
    result = bytearray(idle152)
    for offset, value in state.items():
        result[offset] = value
    result[34] = counter
    frame = bytes(result)
    _neutral_shape(frame)
    return frame


def crc16_x25(data):
    crc = 0xffff
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return crc ^ 0xffff


def validate_frames(first152, second152, planned152):
    """Return the repeated observed counter, not freshness/ownership permission.

    Both copies have independently valid prefix CRCs. All152 bytes must match
    the planned neutral frame except b34 and the kernel-stamped b96/b97. The
    planned counter may precede host alignment; the returned value is the copied
    baseline. The caller separately proves fresh acquisitions and owned alignment.
    """
    _neutral_shape(planned152)
    for frame in (first152, second152):
        _frame(frame)
        _require(int.from_bytes(frame[96:98], 'little') == crc16_x25(frame[:96]),
                 'mixer CRC-16/X25 mismatch')
        _neutral_shape(frame)
    _require(first152[34] == second152[34], 'observed counters differ')
    for frame in (first152, second152):
        _require(all(frame[offset] == planned152[offset] for offset in range(152)
                     if offset not in COMPARISON_EXCLUDED), 'observed frame differs from planned neutral frame')
    return first152[34]
