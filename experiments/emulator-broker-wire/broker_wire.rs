// SPDX-License-Identifier: GPL-3.0-only
//! Inactive, bounded v1 broker transport over a caller-owned stream capability.
//! No endpoint opening, authentication, retry, reconnect, teardown or native
//! authority is supplied. Correlation labels and copied evidence are not permits.
#![forbid(unsafe_code)]
use crate::mixer_producer::{BaselineReceipt, Envelope, Frame, FrameAck, Key};
use crate::mixer_route::{BackendOutcome, MixerBackend};
use std::io::{self, Read, Write};
use std::sync::{Arc, Mutex};

pub trait WireStream: Read + Write + Send {}
impl<T: Read + Write + Send> WireStream for T {}
pub const HEADER: usize = 48;
pub const ADOPT_REQUEST: usize = 256;
pub const FRAME_REQUEST: usize = 248;
pub const ADOPT_RESPONSE: usize = 296;
pub const FRAME_RESPONSE: usize = 668;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct State {
    pub instance: [u64; 2], pub run_id: u64, pub epoch: u64,
    pub sequence: u64, pub flags: u32, pub crc_len: i32, pub frame: Frame,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Config { pub initial: State, pub producer: u64, pub session: [u8; 16] }
impl Config {
    fn valid(self) -> bool {
        self.initial.instance != [0, 0] && self.initial.run_id != 0
            && self.initial.epoch != 0 && self.producer != 0 && self.session != [0; 16]
            && self.initial.flags == 31 && self.initial.crc_len == 96
            && self.initial.sequence != u64::MAX
    }
    fn key(self) -> Key { Key { instance: self.initial.instance,
        run_id: self.initial.run_id, epoch: self.initial.epoch, producer_id: self.producer } }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase { Retained, AdoptArmed, Ready, FrameArmed, Terminal }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IoStage { Write, Flush, Read }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IoReturn {
    Written(usize), Read(usize), Flushed,
    Error { stage: IoStage, kind: io::ErrorKind, raw_os_error: Option<i32> },
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Fault {
    Config, Phase, Poisoned, Counter, LocalRequest, Header, Receipt, Cached,
    Api(i32), Unknown, Rejected, ZeroProgress(IoStage), InvalidCount(IoStage),
    Io(IoStage, io::ErrorKind),
}
/// Storage and lengths are bounded independently of untrusted length fields.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Record { pub bytes: [u8; FRAME_RESPONSE], pub len: usize }
impl Record {
    fn empty() -> Self { Self { bytes: [0; FRAME_RESPONSE], len: 0 } }
    pub fn as_bytes(&self) -> &[u8] { &self.bytes[..self.len] }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Evidence {
    pub config: Config, pub phase: Phase, pub poisoned: bool,
    pub terminal: Option<Fault>, pub pending: Option<Record>, pub response: Record,
    pub written: usize, pub read: usize, pub flush_started: bool, pub flush_completed: bool,
    pub last_io: Option<IoReturn>,
    pub api_result: Option<i32>, pub provenance: Option<u32>, pub outcome: Option<u32>,
    pub expected: State, pub next_exchange: u64, pub next_request: u64,
    pub last_local_refusal: Option<Envelope>,
}
#[derive(Clone)]
pub struct EvidenceHandle(Arc<Mutex<Evidence>>);
impl EvidenceHandle {
    /// Copied local diagnostics only. Never locks/calls the stream; this handle
    /// does not extend stream custody after the backend itself is dropped.
    pub fn snapshot(&self) -> Evidence {
        match self.0.lock() {
            Ok(e) => e.clone(),
            Err(error) => { let mut e = error.into_inner().clone(); e.poisoned = true; e }
        }
    }
}
pub struct WireBackend { stream: Box<dyn WireStream>, evidence: EvidenceHandle }
impl WireBackend {
    /// Invalid input produces an inert terminal owner, retaining the exact
    /// supplied stream until this backend is explicitly dropped. No I/O here.
    pub fn retain(stream: Box<dyn WireStream>, config: Config) -> (Self, EvidenceHandle) {
        let valid = config.valid();
        let evidence = EvidenceHandle(Arc::new(Mutex::new(Evidence {
            config, phase: if valid { Phase::Retained } else { Phase::Terminal },
            poisoned: false, terminal: if valid { None } else { Some(Fault::Config) },
            pending: None, response: Record::empty(), written: 0, read: 0,
            flush_started: false, flush_completed: false, last_io: None, api_result: None,
            provenance: None, outcome: None, expected: config.initial,
            next_exchange: 1, next_request: 1, last_local_refusal: None,
        })));
        (Self { stream, evidence: evidence.clone() }, evidence)
    }
    fn terminal(&self, fault: Fault) {
        match self.evidence.0.lock() {
            Ok(mut e) => { e.phase = Phase::Terminal; if e.terminal.is_none() { e.terminal = Some(fault); } }
            Err(error) => {
                let mut e = error.into_inner(); e.poisoned = true; e.phase = Phase::Terminal;
                if e.terminal.is_none() { e.terminal = Some(Fault::Poisoned); }
            }
        }
    }
    fn arm(&self, phase: Phase, record: Record) -> Result<(), Fault> {
        let mut e = self.evidence.0.lock().map_err(|_| Fault::Poisoned)?;
        e.phase = phase; e.pending = Some(record); e.response = Record::empty();
        e.written = 0; e.read = 0; e.flush_started = false; e.flush_completed = false;
        e.last_io = None; e.api_result = None; e.provenance = None; e.outcome = None;
        Ok(())
    }
    /// Retain only an actually observed trait return even if another observer
    /// poisoned the journal while I/O blocked. Poison never authorizes progress.
    fn observed(&self, update: impl FnOnce(&mut Evidence)) -> Result<(), Fault> {
        match self.evidence.0.lock() {
            Ok(mut e) => { update(&mut e); Ok(()) }
            Err(error) => {
                let mut e = error.into_inner(); update(&mut e); e.poisoned = true;
                Err(Fault::Poisoned)
            }
        }
    }
    fn io_fault(&self, stage: IoStage, error: io::Error) -> Fault {
        let kind = error.kind();
        let returned = IoReturn::Error { stage, kind, raw_os_error: error.raw_os_error() };
        match self.observed(|e| e.last_io = Some(returned)) {
            Ok(()) => Fault::Io(stage, kind),
            Err(fault) => fault,
        }
    }
    /// Continue positive short progress only within this immutable record.
    /// Errors (including Interrupted), zero progress and panic never retry it.
    fn exchange(&mut self, request: &Record, response_len: usize, kind: u32,
                config: Config, exchange: u64) -> Result<Record, Fault> {
        let mut written = 0;
        while written < request.len {
            let n = match self.stream.write(&request.bytes[written..request.len]) {
                Ok(n) => n, Err(error) => return Err(self.io_fault(IoStage::Write, error)),
            };
            self.observed(|e| {
                e.last_io = Some(IoReturn::Written(n));
                if n <= request.len - written { e.written = written + n; }
            })?;
            if n == 0 { return Err(Fault::ZeroProgress(IoStage::Write)); }
            if n > request.len - written { return Err(Fault::InvalidCount(IoStage::Write)); }
            written += n;
        }
        self.evidence.0.lock().map_err(|_| Fault::Poisoned)?.flush_started = true;
        if let Err(error) = self.stream.flush() {
            return Err(self.io_fault(IoStage::Flush, error));
        }
        self.observed(|e| { e.flush_completed = true; e.last_io = Some(IoReturn::Flushed); })?;
        let mut response = Record::empty();
        for end in [HEADER, response_len] {
            while response.len < end {
                let n = match self.stream.read(&mut response.bytes[response.len..end]) {
                    Ok(n) => n, Err(error) => return Err(self.io_fault(IoStage::Read, error)),
                };
                let valid_count = n <= end - response.len;
                if valid_count { response.len += n; }
                self.observed(|e| {
                    e.last_io = Some(IoReturn::Read(n));
                    if valid_count {
                        e.read = response.len; e.response = response.clone();
                        if response.len == response_len {
                            e.api_result = Some(i32_at(&response.bytes, 48));
                            e.provenance = Some(u32_at(&response.bytes, 52));
                            if kind == 0x80000002 { e.outcome = Some(u32_at(&response.bytes, 56)); }
                        }
                    }
                })?;
                if n == 0 { return Err(Fault::ZeroProgress(IoStage::Read)); }
                if !valid_count { return Err(Fault::InvalidCount(IoStage::Read)); }
            }
            if end == HEADER && !valid_header(&response.bytes, kind, response_len, config.session, exchange) {
                return Err(Fault::Header);
            }
        }
        Ok(response)
    }
}
impl MixerBackend for WireBackend {
    fn baseline(&mut self) -> Result<BaselineReceipt, String> {
        let before = self.evidence.snapshot();
        if before.poisoned || before.phase != Phase::Retained {
            self.terminal(Fault::Phase);
            return Err("wire baseline is unavailable; no I/O attempted".into());
        }
        let config = before.config;
        let mut request = header(1, ADOPT_REQUEST, config.session, 1);
        encode_state(&mut request.bytes[48..248], config.initial);
        put64(&mut request.bytes, 248, config.producer);
        if let Err(fault) = self.arm(Phase::AdoptArmed, request.clone()) {
            self.terminal(fault); return Err(format!("{fault:?}"));
        }
        let result = self.exchange(&request, ADOPT_RESPONSE, 0x80000001, config, 1)
            .and_then(|response| {
                let b = &response.bytes;
                if i32_at(b, 48) != 0 { return Err(Fault::Api(i32_at(b, 48))); }
                if u32_at(b, 52) != 0 { return Err(Fault::Cached); }
                let mut expected = [0; 240];
                encode_state(&mut expected[..200], config.initial);
                encode_key(&mut expected[200..], config.key());
                if b[56..296] != expected { return Err(Fault::Receipt); }
                Ok(BaselineReceipt { key: config.key(), frame: config.initial.frame,
                    kernel_sequence: config.initial.sequence })
            });
        match result {
            Ok(baseline) => {
                let updated = match self.evidence.0.lock() {
                    Ok(mut e) => { e.phase = Phase::Ready; e.next_exchange = 2; true }
                    Err(_) => false,
                };
                if updated { Ok(baseline) }
                else { self.terminal(Fault::Poisoned); Err("poisoned wire journal".into()) }
            }
            Err(fault) => { self.terminal(fault); Err(format!("{fault:?}")) }
        }
    }
    fn submit(&mut self, request: Envelope) -> BackendOutcome {
        let before = self.evidence.snapshot();
        // Armed survives a caught stream panic. No later no-I/O call may turn
        // that uncertainty into a known rejection for the same envelope.
        if before.poisoned || !matches!(before.phase, Phase::Retained | Phase::Ready) {
            return BackendOutcome::Unknown;
        }
        let fault = if before.phase != Phase::Ready { Some(Fault::Phase) }
            else if before.next_exchange == 0 || before.next_exchange == u64::MAX
                || before.next_request == 0 || before.next_request == u64::MAX
                || before.expected.sequence == u64::MAX { Some(Fault::Counter) }
            else if request.key() != before.config.key()
                || request.request_id() != before.next_request { Some(Fault::LocalRequest) }
            else { None };
        if let Some(fault) = fault {
            if let Ok(mut e) = self.evidence.0.lock() { e.last_local_refusal = Some(request); }
            self.terminal(fault);
            // No I/O on this call cannot negate an earlier successful attempt
            // of this same immutable envelope. Local refusals are never ACKs.
            return BackendOutcome::Unknown;
        }
        let mut canonical = header(2, FRAME_REQUEST, before.config.session, before.next_exchange);
        encode_request(&mut canonical.bytes[48..248], request);
        if let Err(fault) = self.arm(Phase::FrameArmed, canonical.clone()) {
            self.terminal(fault); return BackendOutcome::Unknown;
        }
        let mut after = before.expected;
        after.sequence += 1; after.frame = *request.frame();
        let outcome = self.exchange(&canonical, FRAME_RESPONSE, 0x80000002,
            before.config, before.next_exchange).and_then(|response| {
                let b = &response.bytes;
                if i32_at(b, 48) != 0 { return Err(Fault::Api(i32_at(b, 48))); }
                if u32_at(b, 52) != 0 { return Err(Fault::Cached); }
                if u32_at(b, 60) != 1 || u32_at(b, 68) != 0 { return Err(Fault::Receipt); }
                let mut command = [0; 196]; encode_command(&mut command, after);
                let mut state = [0; 200]; encode_state(&mut state, after);
                if b[72..272] != canonical.bytes[48..248]
                    || b[272..468] != command || b[468..668] != state {
                    return Err(Fault::Receipt);
                }
                match (u32_at(b, 56), i32_at(b, 64)) {
                    (1, 0) => Ok(BackendOutcome::Committed(FrameAck {
                        request, kernel_sequence: after.sequence })),
                    (2, reason) if reason > 0 => Ok(BackendOutcome::Rejected { request, reason }),
                    _ => Err(Fault::Unknown),
                }
            });
        match outcome {
            Ok(ack @ BackendOutcome::Committed(_)) => {
                let updated = match self.evidence.0.lock() {
                    Ok(mut e) => { e.expected = after; e.next_request += 1;
                        e.next_exchange += 1; e.phase = Phase::Ready; true }
                    Err(_) => false,
                };
                if updated { ack } else { self.terminal(Fault::Poisoned); BackendOutcome::Unknown }
            }
            Ok(rejected @ BackendOutcome::Rejected { .. }) => {
                self.terminal(Fault::Rejected); rejected
            }
            Err(fault) => { self.terminal(fault); BackendOutcome::Unknown }
            Ok(BackendOutcome::Unknown) => { self.terminal(Fault::Unknown); BackendOutcome::Unknown }
        }
    }
}
fn put32(b: &mut [u8], at: usize, value: u32) { b[at..at+4].copy_from_slice(&value.to_be_bytes()); }
fn put64(b: &mut [u8], at: usize, value: u64) { b[at..at+8].copy_from_slice(&value.to_be_bytes()); }
fn u32_at(b: &[u8], at: usize) -> u32 { u32::from_be_bytes(b[at..at+4].try_into().unwrap()) }
fn i32_at(b: &[u8], at: usize) -> i32 { i32::from_be_bytes(b[at..at+4].try_into().unwrap()) }
fn header(kind: u32, len: usize, session: [u8;16], exchange: u64) -> Record {
    let mut record = Record::empty(); record.len = len;
    record.bytes[..8].copy_from_slice(b"AZMBRPC1");
    put32(&mut record.bytes, 8, 1); put32(&mut record.bytes, 12, kind);
    put32(&mut record.bytes, 16, len as u32); record.bytes[24..40].copy_from_slice(&session);
    put64(&mut record.bytes, 40, exchange); record
}
fn valid_header(b: &[u8], kind: u32, len: usize, session: [u8;16], exchange: u64) -> bool {
    b[..8] == *b"AZMBRPC1" && u32_at(b, 8) == 1 && u32_at(b, 12) == kind
        && u32_at(b, 16) == len as u32 && u32_at(b, 20) == 0
        && b[24..40] == session && u64::from_be_bytes(b[40..48].try_into().unwrap()) == exchange
        && exchange != 0 && exchange != u64::MAX && session != [0;16]
}
fn encode_key(b: &mut [u8], key: Key) {
    for (at, value) in [key.instance[0],key.instance[1],key.run_id,key.epoch,key.producer_id]
        .into_iter().enumerate() { put64(b, at*8, value); }
}
fn encode_state(b: &mut [u8], state: State) {
    for (at, value) in [state.instance[0],state.instance[1],state.run_id,state.epoch,state.sequence]
        .into_iter().enumerate() { put64(b, at*8, value); }
    put32(b, 40, state.flags); b[44..48].copy_from_slice(&state.crc_len.to_be_bytes());
    b[48..200].copy_from_slice(&state.frame);
}
fn encode_command(b: &mut [u8], state: State) {
    for (at, value) in [state.instance[0],state.instance[1],state.run_id,state.epoch,state.sequence]
        .into_iter().enumerate() { put64(b, at*8, value); }
    put32(b, 40, 1); b[44..196].copy_from_slice(&state.frame);
}
fn encode_request(b: &mut [u8], request: Envelope) {
    encode_key(b, request.key()); put64(b, 40, request.request_id());
    b[48..200].copy_from_slice(request.frame());
}
#[cfg(test)]
#[path = "broker_wire_tests.rs"]
mod tests;
