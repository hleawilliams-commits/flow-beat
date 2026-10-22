#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pure, separately versioned copied-overlay rules. No ambient I/O or grants.

Settings are untrusted, regardless of which host program may have saved them.
This module neither samples a controller nor establishes native consumption.
"""
import hashlib
import json
import re

POLICY_VERSION = 2
DRIVER_SHA256 = '6f88798ec54785dc2ce9180e5c2839a1a8e8ec370c997bc7f48e8d1cc06e640f'
CONTROLS_SHA256 = 'b9145e4b41439761abbfdec461b854412748af2fe95183fd5843ad34d4298445'
CONFIG_SHA256 = '0a1986611ec2642379a42f1f517d8d1decf1764e0cee8ab2f37c3e6c04782992'
IDLE_SHA256 = '885b5464cd62b520bf336abb40af2016c2d84f73abbeaced7c649dddb048e7a3'
PROTECTED_SHA256 = '55a4ab3c964d726b5e30709c5f1ecc674737f9e6a79db4dac89f965187e51460'
ANALOG_HIGH = frozenset((10,11,12,13,15,36,37,38,39,41,42,43,44,46,47,48,49,
                        51,52,53,54,56,57,58,59,61,62,63,64,66,67,68,69,71,72,
                        73,74,76,77,78))
ANALOG_LOW_MASKS = {14:0xff,16:0x03,40:0xff,45:0xff,50:0xff,55:0xff,
                    60:0xff,65:0xff,70:0xff,75:0xff,80:0xf0}
FX_BYTE = 22
FX_TIME_BYTES = (26,27)
MIXER_OFFSETS = ANALOG_HIGH | frozenset(ANALOG_LOW_MASKS) | {FX_BYTE} | set(FX_TIME_BYTES)
# The two partial packed bytes have zero reserved bits in the pinned template.
VALUE_MASKS = {**{offset:0xff for offset in ANALOG_HIGH}, **ANALOG_LOW_MASKS,
               FX_BYTE:0xf0,26:0xff,27:0xff}
CRC_BYTES = frozenset((96,97))
COMPARISON_EXCLUDED = CRC_BYTES | {34}
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
        _require(value & ~VALUE_MASKS[key] == 0, 'active or reserved mixer bits')
    _require((26 in state) == (27 in state), 'FX TIME requires both bytes or neither')


def parse_mixer_state(raw):
    """Parse optional bounded UTF-8 JSON; reject ambiguity, never normalize."""
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
    _require(frame[FX_BYTE] & 0x0f == 0, 'FX ON and frequency buttons must be released')
    _require(frame[16] & 0xfc == 0 and frame[80] & 0x0f == 0, 'reserved analog bits differ')
    # Include all 152 positions. Mask only reviewed field bits and independent
    # browse/CRC bytes; every remaining bit must equal the exact idle template.
    protected = bytes(value & ~(0xff if offset in COMPARISON_EXCLUDED else VALUE_MASKS.get(offset, 0))
                      for offset, value in enumerate(frame))
    _require(hashlib.sha256(protected).hexdigest() == PROTECTED_SHA256,
             'protected bits differ from pinned neutral template')


def neutral_frame(idle152, state, counter):
    """Construct complete reviewed settings on the exact template, without I/O.

    Absent settings use the pinned template. Partial analog settings are allowed;
    present packed bytes are validated as a whole. FX TIME is all-or-none. No
    observed frame is normalized by this constructor; comparison must still pass.
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


def _observed(frame):
    _frame(frame)
    _require(int.from_bytes(frame[96:98], 'little') == crc16_x25(frame[:96]),
             'mixer CRC-16/X25 mismatch')
    _neutral_shape(frame)


def validate_frames(first152, second152, planned152):
    """Compare two supplied neutral observations; return a copied counter only.

    Both CRCs must pass. Only browse counter and CRC may differ from the plan;
    FX TIME and every analog bit must match. No freshness or ownership grant.
    """
    _neutral_shape(planned152)
    for frame in (first152, second152):
        _observed(frame)
    _require(first152[34] == second152[34], 'observed counters differ')
    for frame in (first152, second152):
        _require(all(frame[offset] == planned152[offset] for offset in range(152)
                     if offset not in COMPARISON_EXCLUDED), 'observed frame differs from planned neutral frame')
    return first152[34]


def action_payloads(observed152, kind):
    """Return (first152, release152) copied from an observed neutral baseline.

    No bus prefix, transport, clock, permit or sequence policy. Browse preserves
    its counter; CW/CCW change it by exactly one with no wrap. CRC bytes remain
    exactly observed, not recomputed: a reviewed controller path must restamp
    them. These returned payloads are not claims of valid observed-input CRCs.
    """
    _require(type(kind) is str and kind in ('browse','cw','ccw'), 'unsupported action')
    _observed(observed152)
    target = observed152[34] + {'browse':0,'cw':1,'ccw':-1}[kind]
    _counter(target)
    first, release = bytearray(observed152), bytearray(observed152)
    first[6] = 0
    if kind == 'browse':
        first[32] = 0x40
    first[34] = release[34] = target
    return bytes(first), bytes(release)
