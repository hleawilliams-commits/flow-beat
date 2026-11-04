// SPDX-License-Identifier: GPL-3.0-only
//! Inactive observation slice for a caller-retained supervisor child.
//!
//! A scoped binding borrows the actual Child and journal exclusively. It never
//! spawns, signals, kills, restarts, calls blocking wait, closes stdin, or owns
//! a destructor. Actual try_wait may reap an exited child.
//! One observation calls try_wait at most once. A returned exit status is an
//! actual child-status observation, not native quiescence or cleanup authority.
//! The Hold disposition is diagnostic only: no route admission is wired here.
//!
//! All retained records are bounded: first loss, first poll error, and latest
//! poll. Arcs share immutable result values only, never child/resource custody.
//! This is in-memory evidence while the caller keeps the values alive. It makes
//! no process-death, nonlocal-exit, allocation-failure, or durable-journal claim.
#![forbid(unsafe_code)]

use std::io::{self, Read};
use std::process::{Child, ExitStatus};
use std::sync::Arc;

/// The reader's returned outcome and reporter loss remain distinct. ReaderError
/// has no copied-byte count: io::copy does not return that count on error. The
/// complete actual error object remains available in the separate ReaderJournal.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ControllerEvent {
    None,
    ReaderEof { copied: u64 },
    ReaderError { kind: io::ErrorKind, raw_os_error: Option<i32> },
    ReporterDisconnected,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Disposition {
    Observing,
    Hold,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BindError {
    AlreadyBound,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ObserveError {
    UnreturnedPoll,
    Exited,
    CounterOverflow,
}

/// First controller loss is immutable even if subsequent calls supply None.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LossRecord {
    pub attempt: u64,
    pub event: ControllerEvent,
}

/// The exact returned value is published before it is classified. None means
/// this particular poll was armed but has not returned; it is never Running.
#[derive(Clone, Debug)]
pub struct PollRecord {
    attempt: u64,
    event: ControllerEvent,
    returned: Option<Arc<io::Result<Option<ExitStatus>>>>,
}

impl PollRecord {
    pub fn attempt(&self) -> u64 { self.attempt }
    pub fn event(&self) -> ControllerEvent { self.event }
    pub fn returned(&self) -> Option<&io::Result<Option<ExitStatus>>> {
        self.returned.as_deref()
    }
}

#[derive(Debug)]
pub struct Journal {
    bound: bool,
    child_id: Option<u32>,
    attempts: u64,
    hold_latched: bool,
    exited: bool,
    first_loss: Option<LossRecord>,
    first_poll_error: Option<PollRecord>,
    last_poll: Option<PollRecord>,
}

impl Default for Journal {
    fn default() -> Self { Self::new() }
}

impl Journal {
    pub fn new() -> Self {
        Self {
            bound: false,
            child_id: None,
            attempts: 0,
            hold_latched: false,
            exited: false,
            first_loss: None,
            first_poll_error: None,
            last_poll: None,
        }
    }

    pub fn bound(&self) -> bool { self.bound }

    /// Diagnostic only. A PID never proves identity, permits rebinding, or
    /// supplies signal authority.
    pub fn child_id(&self) -> Option<u32> { self.child_id }
    pub fn attempts(&self) -> u64 { self.attempts }
    pub fn exited(&self) -> bool { self.exited }
    pub fn last_poll(&self) -> Option<&PollRecord> { self.last_poll.as_ref() }
    pub fn first_loss(&self) -> Option<LossRecord> { self.first_loss }
    pub fn first_poll_error(&self) -> Option<&PollRecord> { self.first_poll_error.as_ref() }

    /// Loss, returned poll error, and known exit latch Hold permanently. An
    /// unreturned armed call independently implies Hold immediately after a
    /// caught panic; the observer refuses to replace or replay that record.
    /// During an ordinary in-flight call this is an unresolved diagnostic, not
    /// a sticky latch that is temporarily set and then cleared.
    pub fn disposition(&self) -> Disposition {
        if self.hold_latched || self.exited || self.attempts == u64::MAX
            || self.last_poll.as_ref().is_some_and(|call| call.returned.is_none())
        {
            Disposition::Hold
        } else {
            Disposition::Observing
        }
    }
}

/// Both exclusive borrows last for this binding's entire scope. Releasing the
/// binding merely releases borrows; the caller still owns Child and Journal.
/// The journal is permanently bound and cannot be reused for any later child.
pub struct ChildObservation<'a> {
    child: &'a mut Child,
    journal: &'a mut Journal,
}

impl<'a> ChildObservation<'a> {
    pub fn bind(child: &'a mut Child, journal: &'a mut Journal) -> Result<Self, BindError> {
        if journal.bound { return Err(BindError::AlreadyBound); }
        journal.bound = true;
        journal.child_id = Some(child.id());
        Ok(Self { child, journal })
    }

    pub fn journal(&self) -> &Journal { self.journal }

    /// Observe once, preserving caller custody on every returned error. Hold
    /// does not prohibit a later explicit status observation. Actual Exited or
    /// an armed-unreturned call does prohibit all subsequent polls. No event or
    /// prior record is replaced on those refusals or on counter overflow.
    pub fn observe(&mut self, event: ControllerEvent) -> Result<Disposition, ObserveError> {
        self.observe_once(event, Child::try_wait)
    }

    fn observe_once<F>(&mut self, event: ControllerEvent, poll: F)
        -> Result<Disposition, ObserveError>
    where
        F: FnOnce(&mut Child) -> io::Result<Option<ExitStatus>>,
    {
        if self.journal.last_poll.as_ref().is_some_and(|call| call.returned.is_none()) {
            return Err(ObserveError::UnreturnedPoll);
        }
        if self.journal.exited { return Err(ObserveError::Exited); }
        let attempt = self.journal.attempts.checked_add(1)
            .ok_or(ObserveError::CounterOverflow)?;

        // Store the supplied value before arming the one actual poll. A
        // controller loss never implies permission to stop/dispose the child.
        self.journal.attempts = attempt;
        self.journal.last_poll = Some(PollRecord { attempt, event, returned: None });
        if event != ControllerEvent::None {
            self.journal.hold_latched = true;
            if self.journal.first_loss.is_none() {
                self.journal.first_loss = Some(LossRecord { attempt, event });
            }
        }

        let result = poll(self.child);
        // Retain the complete actual result before any error/exit decision.
        // Sharing this value preserves the first error object across later
        // polls without cloning/flattening the underlying io::Error.
        let returned = Arc::new(result);
        self.journal.last_poll.as_mut().expect("armed poll retained").returned =
            Some(Arc::clone(&returned));

        match returned.as_ref() {
            Ok(None) => {}
            Ok(Some(_)) => {
                self.journal.exited = true;
                self.journal.hold_latched = true;
            }
            Err(_) => {
                self.journal.hold_latched = true;
                if self.journal.first_poll_error.is_none() {
                    self.journal.first_poll_error = self.journal.last_poll.clone();
                }
            }
        }
        Ok(self.journal.disposition())
    }

    /// Scripted poll outcomes/panics test journal transitions only. They are
    /// not OS polling-failure evidence. Absent from production compilation.
    #[cfg(test)]
    pub fn observe_with_for_test<F>(&mut self, event: ControllerEvent, poll: F)
        -> Result<Disposition, ObserveError>
    where
        F: FnOnce(&mut Child) -> io::Result<Option<ExitStatus>>,
    {
        self.observe_once(event, poll)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReaderError {
    AlreadyAttempted,
}

#[derive(Debug)]
pub struct ReaderJournal {
    attempted: bool,
    returned: Option<io::Result<u64>>,
}

impl Default for ReaderJournal {
    fn default() -> Self { Self::new() }
}

impl ReaderJournal {
    pub fn new() -> Self { Self { attempted: false, returned: None } }
    pub fn attempted(&self) -> bool { self.attempted }
    pub fn returned(&self) -> Option<&io::Result<u64>> { self.returned.as_ref() }

    /// Converts only an actually returned copy result. An attempted-unreturned
    /// reader has no EOF/error event; reporter loss is a separate observation.
    pub fn event(&self) -> Option<ControllerEvent> {
        match self.returned.as_ref()? {
            Ok(copied) => Some(ControllerEvent::ReaderEof { copied: *copied }),
            Err(error) => Some(ControllerEvent::ReaderError {
                kind: error.kind(), raw_os_error: error.raw_os_error(),
            }),
        }
    }
}

/// One-shot actual io::copy to io::sink, preserving its complete returned
/// result before conversion to a typed event. This helper can block and
/// io::copy can make multiple reads/retry internally: it has no latency or
/// one-syscall bound. A panic leaves attempted=true, returned=None and prevents
/// another copy. The caller owns the reader and journal throughout; nothing
/// closes the reader, sends a channel event, or performs child lifecycle work.
pub fn copy_controller<R: Read + ?Sized>(
    reader: &mut R,
    journal: &mut ReaderJournal,
) -> Result<ControllerEvent, ReaderError> {
    if journal.attempted { return Err(ReaderError::AlreadyAttempted); }
    journal.attempted = true;
    let result = io::copy(reader, &mut io::sink());
    journal.returned = Some(result);
    Ok(journal.event().expect("copy result retained"))
}

#[cfg(test)]
#[path = "tests.rs"]
mod tests;
