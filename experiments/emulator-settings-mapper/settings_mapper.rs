// SPDX-License-Identifier: GPL-3.0-only
//! Inactive, value-only projection of an exact FRAME receipt into host settings.
//!
//! This module performs no I/O and implements no SettingsSink. FrameAck is a
//! value, not an authentication capability: the producer/broker must validate
//! provenance, generation and ordering before calling a future persistence sink.
//! The complete ACK is retained even when different frames project to the same
//! persistent values. No current UI state or cached frame is consulted.
//!
//! A supported existing mixer map may be deliberately reserialized. Exact
//! original bytes, absence, file identity and permissions remain the separate
//! settings-pair recovery record. Unsupported or ambiguous input is refused,
//! including existing transient selector bits and an incomplete TIME pair,
//! even when this request would overwrite those entries.
//!
//! Whole analog bytes preserve shared low bits. Only the selector high nibble
//! is persisted; held buttons, latches, X-PAD and CRC fields are not mapped.
//! CRC validation is not performed: the native module restamps CRC fields.
//! Receipt authenticity, native consumption and filesystem ownership are not
//! inferred from any projection result.
#![forbid(unsafe_code)]

use crate::mixer_producer::FrameAck;
use crate::settings_pair::PairTargets;

/// Bound applies to the raw input before whitespace scanning or parsing.
pub const MAX_INPUT_BYTES: usize = 65_536;

/// Both optional controls require an explicit caller choice; there is no Default.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Selection {
    pub selector: bool,
    pub fx_time: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MapError {
    TooLarge,
    Syntax,
    Number,
    DuplicateKey,
    UnsupportedKey,
    SelectorMomentaryBits,
    IncompleteFxTime,
}

/// Immutable full-receipt association. Borrowed getters do not permit retagging.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MappedSettings {
    ack: FrameAck,
    targets: PairTargets,
}

impl MappedSettings {
    pub fn ack(&self) -> &FrameAck {
        &self.ack
    }

    pub fn targets(&self) -> &PairTargets {
        &self.targets
    }

    pub fn into_parts(self) -> (FrameAck, PairTargets) {
        (self.ack, self.targets)
    }
}

const SELECTOR: usize = 0x16;
const TIME_LO: usize = 0x1a;
const TIME_HI: usize = 0x1b;
const KNOB: usize = 0x22;

fn analog_key(key: usize) -> bool {
    matches!(key, 0x0a..=0x10 | 0x24..=0x4e | 0x50)
}

fn supported_key(key: usize) -> bool {
    analog_key(key) || matches!(key, SELECTOR | TIME_LO | TIME_HI)
}

struct SavedMap {
    values: [Option<u8>; 256],
}

impl SavedMap {
    fn parse(saved: Option<&[u8]>) -> Result<Self, MapError> {
        let mut map = Self {
            values: [None; 256],
        };
        let Some(input) = saved else {
            return Ok(map);
        };
        if input.len() > MAX_INPUT_BYTES {
            return Err(MapError::TooLarge);
        }

        let mut parser = Parser { input, at: 0 };
        parser.whitespace();
        parser.expect(b'{')?;
        parser.whitespace();
        if parser.take(b'}') {
            parser.finish()?;
            return Ok(map);
        }

        loop {
            parser.expect(b'"')?;
            let key = usize::from(parser.number()?);
            parser.expect(b'"')?;
            parser.whitespace();
            parser.expect(b':')?;
            parser.whitespace();
            let value = parser.number()?;
            if !supported_key(key) {
                return Err(MapError::UnsupportedKey);
            }
            if map.values[key].is_some() {
                return Err(MapError::DuplicateKey);
            }
            map.values[key] = Some(value);
            parser.whitespace();
            if parser.take(b'}') {
                parser.finish()?;
                break;
            }
            parser.expect(b',')?;
            parser.whitespace();
        }

        if map.values[SELECTOR].is_some_and(|value| value & 0x0f != 0) {
            return Err(MapError::SelectorMomentaryBits);
        }
        if map.values[TIME_LO].is_some() != map.values[TIME_HI].is_some() {
            return Err(MapError::IncompleteFxTime);
        }
        Ok(map)
    }

    fn encode(&self) -> Vec<u8> {
        let mut output = String::from("{");
        let mut first = true;
        for (key, value) in self.values.iter().enumerate() {
            if let Some(value) = value {
                if !first {
                    output.push_str(", ");
                }
                first = false;
                output.push('"');
                output.push_str(&key.to_string());
                output.push_str("\": ");
                output.push_str(&value.to_string());
            }
        }
        output.push('}');
        output.into_bytes()
    }
}

/// Strict bounded JSON subset: quoted canonical decimal byte keys and
/// canonical decimal byte values. No escapes, duplicate keys, sign, aliases,
/// leading zeros, fractional/exponential values, comments or trailing data.
struct Parser<'a> {
    input: &'a [u8],
    at: usize,
}

impl Parser<'_> {
    fn peek(&self) -> Option<u8> {
        self.input.get(self.at).copied()
    }

    fn whitespace(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.at += 1;
        }
    }

    fn take(&mut self, expected: u8) -> bool {
        if self.peek() == Some(expected) {
            self.at += 1;
            true
        } else {
            false
        }
    }

    fn expect(&mut self, expected: u8) -> Result<(), MapError> {
        if self.take(expected) {
            Ok(())
        } else {
            Err(MapError::Syntax)
        }
    }

    fn number(&mut self) -> Result<u8, MapError> {
        let first = self.peek().ok_or(MapError::Number)?;
        if !first.is_ascii_digit() {
            return Err(MapError::Number);
        }
        self.at += 1;
        let mut value = u16::from(first - b'0');
        if first == b'0' && self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
            return Err(MapError::Number);
        }
        while let Some(byte @ b'0'..=b'9') = self.peek() {
            // value <=255 before this step, so the intermediate cannot wrap.
            value = value * 10 + u16::from(byte - b'0');
            if value > u16::from(u8::MAX) {
                return Err(MapError::Number);
            }
            self.at += 1;
        }
        Ok(value as u8)
    }

    fn finish(&mut self) -> Result<(), MapError> {
        self.whitespace();
        if self.at == self.input.len() {
            Ok(())
        } else {
            Err(MapError::Syntax)
        }
    }
}

/// Derive knob and every analog byte from this ACK. Explicitly selected extras
/// come from that same frame; valid supported unselected extras retain their
/// saved values/absence. The TIME pair is copied as raw little-endian bytes,
/// without interpreting, clamping or wrapping the signed counter.
pub fn map(
    ack: FrameAck,
    saved: Option<&[u8]>,
    selection: Selection,
) -> Result<MappedSettings, MapError> {
    let mut map = SavedMap::parse(saved)?;
    let frame = ack.request.frame();
    for (key, value) in map.values.iter_mut().enumerate() {
        if analog_key(key) {
            *value = Some(frame[key]);
        }
    }
    if selection.selector {
        map.values[SELECTOR] = Some(frame[SELECTOR] & 0xf0);
    }
    if selection.fx_time {
        map.values[TIME_LO] = Some(frame[TIME_LO]);
        map.values[TIME_HI] = Some(frame[TIME_HI]);
    }
    let targets = PairTargets {
        knob: frame[KNOB].to_string().into_bytes(),
        mixer: map.encode(),
    };
    Ok(MappedSettings { ack, targets })
}

#[cfg(test)]
mod tests {
    include!("settings_mapper_tests.rs");
}
