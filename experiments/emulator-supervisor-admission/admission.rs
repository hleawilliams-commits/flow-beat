// SPDX-License-Identifier: GPL-3.0-only
//! Inactive binding between retained child observations and terminal port admission.
//!
//! The caller owns both journals. The startup scope borrows the actual retained
//! Child and the exact lifecycle port; this adapter never owns either resource.
//! A loss event closes terminal port admission before polling. A newly observed
//! error/exit closes it after the exact poll return is retained. Poll unwind and
//! every scope exit also latch admission, with the first cause/return preserved.
//!
//! The latch is the existing atomic stop plus nonblocking wake. It takes no
//! mailbox/route/settings lock and never calls blocking request_stop, join, or
//! child lifecycle operations. The terminal wake can cause the worker to exit.
//! It does not cancel a reserved/in-flight request, suppress an already-started
//! settings callback, or prove worker exit/native quiescence. An already-reserved
//! request can race the final worker terminal check and still reach the backend;
//! its late positive ACK/settings may complete before the producer gate closes.
//! No latency, process-death, nonlocal-exit, allocation-failure or durable-journal
//! guarantee is made. Dropping this adapter releases borrows after latching.
#![forbid(unsafe_code)]

use crate::mixer_transport::{MixerPort, StopReason};
use crate::supervisor_observation::{self as observation, ChildObservation,
    ControllerEvent, Disposition, ObserveError};
use std::process::Child;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CloseCause {
    ControllerLoss(ControllerEvent),
    ObservedHold,
    PollUnwind,
    ScopeEnd,
    ScopeUnwind,
    ScopeRefused(ScopeError),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ScopeError {
    Busy,
    StartupNotReady,
    FinishRequested,
    StartupJournalPoisoned,
    ProductsPoisoned,
    OwnerMissing,
    SupervisorMissing,
    SupervisorBusy,
    SupervisorPoisoned,
    PollJournalAlreadyBound,
    AdmissionJournalAlreadyBound,
    UnreturnedLatch,
    AdmissionNotTerminal,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AdmissionError {
    Poll(ObserveError),
    UnreturnedLatch,
    AdmissionNotTerminal,
}

/// The returned reason is the actual first terminal reason already in the port.
/// It can predate this observer and is not a cancellation or completion receipt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LatchRecord {
    armed: bool,
    returned: Option<StopReason>,
}
impl LatchRecord {
    pub fn armed(&self) -> bool { self.armed }
    pub fn returned(&self) -> Option<StopReason> { self.returned }
}

#[derive(Debug, Default)]
pub struct AdmissionJournal {
    bound: bool,
    first_cause: Option<CloseCause>,
    latch: Option<LatchRecord>,
}
impl AdmissionJournal {
    pub fn new() -> Self { Self::default() }
    pub fn bound(&self) -> bool { self.bound }
    pub fn first_cause(&self) -> Option<CloseCause> { self.first_cause }
    pub fn latch(&self) -> Option<&LatchRecord> { self.latch.as_ref() }

    /// The startup scope has resolved its exact owner/port but cannot borrow
    /// the retained child. Consume this journal before the one terminal latch;
    /// do not bind/poll the child or salvage its poisoned mutex operationally.
    pub(crate) fn refuse_scope(&mut self, port: &MixerPort, error: ScopeError) -> ScopeError {
        self.bound = true;
        let _ = latch_once(port, self, CloseCause::ScopeRefused(error));
        // The original typed refusal is never turned into observation success.
        // Latch outcome remains separate: an absent/nonterminal return is not
        // evidence of closure and is not retried by this path.
        error
    }
}

fn latch_status(record: &LatchRecord) -> Result<(), AdmissionError> {
    match record.returned {
        None => Err(AdmissionError::UnreturnedLatch),
        Some(StopReason::None) => Err(AdmissionError::AdmissionNotTerminal),
        Some(_) => Ok(()),
    }
}

fn latch_once(port: &MixerPort, journal: &mut AdmissionJournal, cause: CloseCause)
    -> Result<(), AdmissionError>
{
    // Even an armed-unreturned latch is never replaced or replayed. Its atomic
    // effect is not inferred from an absent return value.
    if let Some(record) = &journal.latch { return latch_status(record); }
    journal.first_cause = Some(cause);
    journal.latch = Some(LatchRecord { armed: true, returned: None });
    let returned = port.latch_terminal_admission();
    journal.latch.as_mut().expect("armed latch retained").returned = Some(returned);
    latch_status(journal.latch.as_ref().expect("returned latch retained"))
}

/// Only the startup module constructs this binding. No caller-supplied port or
/// child can be substituted through its public API. Fresh caller journals can
/// create a later diagnostic scope; the prior terminal port latch never reopens.
pub struct AdmissionObservation<'a> {
    observation: ChildObservation<'a>,
    admission: &'a mut AdmissionJournal,
    port: &'a MixerPort,
}

struct PollUnwind<'a> {
    port: &'a MixerPort,
    journal: &'a mut AdmissionJournal,
    armed: bool,
}
impl Drop for PollUnwind<'_> {
    fn drop(&mut self) {
        if self.armed { let _ = latch_once(self.port, self.journal, CloseCause::PollUnwind); }
    }
}

impl<'a> AdmissionObservation<'a> {
    pub(crate) fn bind(
        child: &'a mut Child,
        poll: &'a mut observation::Journal,
        admission: &'a mut AdmissionJournal,
        port: &'a MixerPort,
    ) -> Result<Self, ScopeError> {
        // Check both before changing either. Binding performs no callback/I/O.
        if poll.bound() { return Err(ScopeError::PollJournalAlreadyBound); }
        if admission.bound { return Err(ScopeError::AdmissionJournalAlreadyBound); }
        let observation = ChildObservation::bind(child, poll)
            .map_err(|_| ScopeError::PollJournalAlreadyBound)?;
        admission.bound = true;
        Ok(Self { observation, admission, port })
    }

    pub fn poll_journal(&self) -> &observation::Journal { self.observation.journal() }
    pub fn admission_journal(&self) -> &AdmissionJournal { self.admission }

    pub fn observe(&mut self, event: ControllerEvent) -> Result<Disposition, AdmissionError> {
        self.observe_using(event, |observation, event| observation.observe(event))
    }

    fn observe_using<F>(&mut self, event: ControllerEvent, poll: F)
        -> Result<Disposition, AdmissionError>
    where
        F: FnOnce(&mut ChildObservation<'a>, ControllerEvent) -> Result<Disposition, ObserveError>,
    {
        // An unreturned or nonterminal latch is retained, never retried, and
        // refuses even another status poll before any new event is recorded.
        if let Some(record) = &self.admission.latch { latch_status(record)?; }
        if event != ControllerEvent::None {
            latch_once(self.port, self.admission, CloseCause::ControllerLoss(event))?;
        } else if self.observation.journal().disposition() == Disposition::Hold {
            latch_once(self.port, self.admission, CloseCause::ObservedHold)?;
        }
        // A callback may catch this poll's unwind inside the enclosing scope.
        // This inner guard therefore closes admission before that catch returns.
        let mut guard = PollUnwind { port: self.port, journal: self.admission, armed: true };
        let result = poll(&mut self.observation, event);
        guard.armed = false;
        // The frozen observer retains the actual result before classifying Hold.
        if self.observation.journal().disposition() == Disposition::Hold {
            latch_once(guard.port, guard.journal, CloseCause::ObservedHold)?;
        }
        result.map_err(AdmissionError::Poll)
    }

    /// Scripted polling outcomes/panics exercise ordering, not OS failure. This
    /// seam and its temporary Child borrow are absent from production builds.
    #[cfg(test)]
    pub fn observe_with_for_test<F>(&mut self, event: ControllerEvent, poll: F)
        -> Result<Disposition, AdmissionError>
    where
        F: FnOnce(&mut Child) -> std::io::Result<Option<std::process::ExitStatus>>,
    {
        self.observe_using(event, |observation, event| {
            observation.observe_with_for_test(event, poll)
        })
    }
}

impl Drop for AdmissionObservation<'_> {
    fn drop(&mut self) {
        let cause = if std::thread::panicking() {
            CloseCause::ScopeUnwind
        } else {
            CloseCause::ScopeEnd
        };
        // No blocking stop/join or resource extraction belongs in this guard.
        let _ = latch_once(self.port, self.admission, cause);
    }
}

#[cfg(test)]
#[path = "supervisor_admission_tests.rs"]
mod tests;
