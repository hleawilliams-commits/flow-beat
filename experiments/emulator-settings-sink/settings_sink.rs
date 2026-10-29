// SPDX-License-Identifier: GPL-3.0-only
//! Inactive Unix bridge from a validated FRAME callback to retained host settings.
//!
//! One fixed baseline and explicit control selection are bound at construction.
//! The full ACK, last verified mixer image and mapped byte plan are retained
//! before their corresponding operations. The pair writer and evidence handle
//! are created together and never exposed for independent mutation.
//!
//! FrameAck remains a value, not an authentication capability. A trusted owner
//! must validate native receipt provenance and exclude competing file readers
//! and writers. This bridge adds ordering checks, not that missing authority.
//! Its error cannot undo or make replayable an already committed native FRAME.
//! A frozen route can rearm a newer epoch after callback failure; this permanently
//! closed bridge then refuses its callback only AFTER that later native commit.
//! The future owner must prevent that rearm. Bridge closure is not native closure.
//!
//! Evidence is bounded process memory, not a durable crash journal. Snapshots
//! copy the bridge then pair journals separately; they are not atomic/coherent
//! observations and do no filesystem I/O. Pair current images are last verified
//! images, potentially a partial pair, not a fresh observation of named files.
//! Panic propagates. An armed phase remains diagnostic evidence, never permission
//! to retry. A panic after pair success may retain PairArmed beside Committed;
//! neither implies that the callback returned. Drop does no I/O or restoration.
#![forbid(unsafe_code)]

use crate::mixer_producer::{BaselineReceipt, FrameAck};
use crate::mixer_route::SettingsSink;
use crate::settings_mapper::{self, MapError, Selection};
use crate::settings_pair::{self, CommitReceipt, FileImage, PairError, PairTargets, SettingsPair};
use std::fs::File;
use std::sync::{Arc, Mutex};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SinkError {
    Pair(PairError),
    Map(MapError),
    WrongKey,
    OutOfOrder,
    DuplicateOrStale,
    Closed,
    Overflow,
    Poisoned,
    Interrupted,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Refusal {
    pub ack: FrameAck,
    pub error: SinkError,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum AttemptPhase {
    MappingArmed,
    PairArmed,
    /// Bridge completion record, not independent proof of return to the producer.
    ReturnedOk(CommitReceipt),
    Failed(SinkError),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AcceptedAttempt {
    pub ack: FrameAck,
    /// Exact last-verified mixer image used by this mapping, including absence.
    pub source_mixer: FileImage,
    /// Present only after mapping succeeded; retained before calling the pair.
    pub targets: Option<PairTargets>,
    pub phase: AttemptPhase,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct JournalEvidence {
    /// A salvaged poisoned copy is diagnostic; poisoning is never cleared.
    pub poisoned: bool,
    pub baseline: BaselineReceipt,
    pub selection: Selection,
    /// Successful bridge-persistence progression, not latest known native commit.
    /// On failure the later native ACK remains in last_accepted_attempt.
    pub next_request_id: u64,
    pub kernel_sequence: u64,
    /// First error/capacity closure. Later refusals do not overwrite its cause.
    pub terminal: Option<SinkError>,
    /// First pre-admission rejection; accepted failures remain in the attempt.
    pub first_refusal: Option<Refusal>,
    pub last_accepted_attempt: Option<AcceptedAttempt>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Evidence {
    pub journal: JournalEvidence,
    pub pair: settings_pair::Evidence,
}

#[derive(Clone)]
pub struct EvidenceHandle {
    journal: Arc<Mutex<JournalEvidence>>,
    pair: settings_pair::EvidenceHandle,
}

impl EvidenceHandle {
    /// No I/O, repair, poison clearing or admission. Retains the pair directory.
    /// The two sequential copies need not describe the same instant.
    pub fn snapshot(&self) -> Evidence {
        let journal = match self.journal.lock() {
            Ok(value) => value.clone(),
            Err(error) => {
                let mut value = error.into_inner().clone();
                value.poisoned = true;
                value
            }
        };
        Evidence {
            journal,
            pair: self.pair.snapshot(),
        }
    }
}

pub struct SettingsSinkBridge {
    pair: SettingsPair,
    evidence: EvidenceHandle,
    #[cfg(test)]
    hook: Option<Box<dyn FnMut(HookPoint) + Send>>,
}

#[cfg(test)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HookPoint {
    AfterMappingIntent,
    AfterPlan,
    AfterPairReturn,
}

macro_rules! hook {
    ($self:expr, $point:expr) => {{
        #[cfg(test)]
        if let Some(callback) = $self.hook.as_mut() {
            callback($point);
        }
    }};
}

impl SettingsSinkBridge {
    /// Consume one explicit directory handle. No ambient/default path or factory.
    /// Pair attach observes both original files and performs no settings writes.
    pub fn attach(
        directory: File,
        baseline: BaselineReceipt,
        namespace: [u8; 16],
        absent_mode: u32,
        selection: Selection,
    ) -> Result<(Self, EvidenceHandle), SinkError> {
        let (pair, pair_evidence) =
            SettingsPair::attach(directory, baseline, namespace, absent_mode)
                .map_err(SinkError::Pair)?;
        let evidence = EvidenceHandle {
            journal: Arc::new(Mutex::new(JournalEvidence {
                poisoned: false,
                baseline,
                selection,
                next_request_id: 1,
                kernel_sequence: baseline.kernel_sequence,
                terminal: None,
                first_refusal: None,
                last_accepted_attempt: None,
            })),
            pair: pair_evidence,
        };
        Ok((
            Self {
                pair,
                evidence: evidence.clone(),
                #[cfg(test)]
                hook: None,
            },
            evidence,
        ))
    }

    /// At most one attempt per contiguous authenticated callback. Even an exact
    /// successful duplicate is terminally refused without mapper or pair I/O.
    /// Typed errors preserve their cause; the trait adapter only formats them.
    pub fn persist(&mut self, ack: FrameAck) -> Result<CommitReceipt, SinkError> {
        let journal = self.read_journal()?;
        if journal.terminal.is_some() {
            return self.refuse(ack, SinkError::Closed);
        }
        if journal
            .last_accepted_attempt
            .as_ref()
            .is_some_and(|attempt| {
                matches!(
                    attempt.phase,
                    AttemptPhase::MappingArmed | AttemptPhase::PairArmed
                )
            })
        {
            return self.refuse(ack, SinkError::Interrupted);
        }
        if ack.request.key() != journal.baseline.key {
            return self.refuse(ack, SinkError::WrongKey);
        }
        let Some(next_sequence) = journal.kernel_sequence.checked_add(1) else {
            return self.refuse(ack, SinkError::Overflow);
        };
        if journal.next_request_id == u64::MAX {
            return self.refuse(ack, SinkError::Overflow);
        }
        if ack.request.request_id() < journal.next_request_id {
            return self.refuse(ack, SinkError::DuplicateOrStale);
        }
        if ack.request.request_id() != journal.next_request_id
            || ack.kernel_sequence != next_sequence
        {
            return self.refuse(ack, SinkError::OutOfOrder);
        }

        // This handle was created with the private pair writer at attach.
        // No filesystem read occurs here. Never map a poisoned diagnostic image.
        let pair = self.evidence.pair.snapshot();
        if pair.poisoned {
            return self.refuse(
                ack,
                SinkError::Pair(PairError {
                    kind: settings_pair::ErrorKind::Poisoned,
                    operation: "sink pair evidence poisoned",
                    errno: None,
                }),
            );
        }
        let source_mixer = pair.current.mixer;
        self.update(|value| {
            value.last_accepted_attempt = Some(AcceptedAttempt {
                ack,
                source_mixer: source_mixer.clone(),
                targets: None,
                phase: AttemptPhase::MappingArmed,
            });
        })?;
        hook!(self, HookPoint::AfterMappingIntent);

        let saved = match &source_mixer {
            FileImage::Absent => None,
            FileImage::Present { bytes, .. } => Some(bytes.as_slice()),
        };
        let mapped = match settings_mapper::map(ack, saved, journal.selection) {
            Ok(mapped) => mapped,
            Err(error) => return self.fail(SinkError::Map(error)),
        };
        let (mapped_ack, targets) = mapped.into_parts();
        self.update(|value| {
            let attempt = value
                .last_accepted_attempt
                .as_mut()
                .expect("accepted mapping intent");
            attempt.targets = Some(targets.clone());
            attempt.phase = AttemptPhase::PairArmed;
        })?;
        hook!(self, HookPoint::AfterPlan);

        let outcome = self.pair.commit(mapped_ack, targets);
        // Deliberately before either bridge completion record. An unwind here
        // preserves PairArmed and the independent actual pair outcome.
        hook!(self, HookPoint::AfterPairReturn);
        match outcome {
            Ok(receipt) => {
                self.update(|value| {
                    value
                        .last_accepted_attempt
                        .as_mut()
                        .expect("accepted pair intent")
                        .phase = AttemptPhase::ReturnedOk(receipt.clone());
                    value.next_request_id = value
                        .next_request_id
                        .checked_add(1)
                        .expect("reserved bounded request ID");
                    value.kernel_sequence = ack.kernel_sequence;
                    if value.next_request_id == u64::MAX || value.kernel_sequence == u64::MAX {
                        value.terminal = Some(SinkError::Overflow);
                    }
                })?;
                Ok(receipt)
            }
            Err(error) => self.fail(SinkError::Pair(error)),
        }
    }

    fn read_journal(&self) -> Result<JournalEvidence, SinkError> {
        self.evidence
            .journal
            .lock()
            .map(|value| value.clone())
            .map_err(|_| SinkError::Poisoned)
    }

    fn update(&self, update: impl FnOnce(&mut JournalEvidence)) -> Result<(), SinkError> {
        let mut value = self
            .evidence
            .journal
            .lock()
            .map_err(|_| SinkError::Poisoned)?;
        update(&mut value);
        Ok(())
    }

    fn refuse<T>(&self, ack: FrameAck, error: SinkError) -> Result<T, SinkError> {
        self.update(|value| {
            if value.terminal.is_none() {
                value.terminal = Some(error.clone());
            }
            if value.first_refusal.is_none() {
                value.first_refusal = Some(Refusal {
                    ack,
                    error: error.clone(),
                });
            }
        })?;
        Err(error)
    }

    fn fail<T>(&self, error: SinkError) -> Result<T, SinkError> {
        self.update(|value| {
            value
                .last_accepted_attempt
                .as_mut()
                .expect("accepted failed attempt")
                .phase = AttemptPhase::Failed(error.clone());
            if value.terminal.is_none() {
                value.terminal = Some(error.clone());
            }
        })?;
        Err(error)
    }

    #[cfg(test)]
    pub fn set_hook(&mut self, hook: Box<dyn FnMut(HookPoint) + Send>) {
        self.hook = Some(hook);
    }

    #[cfg(test)]
    pub fn set_pair_fault_hook(
        &mut self,
        hook: Box<dyn FnMut(settings_pair::FaultPoint) -> settings_pair::FaultAction + Send>,
    ) {
        self.pair.set_fault_hook(hook);
    }

    #[cfg(test)]
    pub fn poison_journal_for_test(&self) {
        let _held = self.evidence.journal.lock().unwrap();
        panic!("intentional settings-sink journal poison");
    }

    #[cfg(test)]
    pub fn poison_pair_journal_for_test(&self) {
        self.pair.poison_journal_for_test();
    }
}

impl SettingsSink for SettingsSinkBridge {
    fn commit(&mut self, ack: FrameAck) -> Result<(), String> {
        self.persist(ack)
            .map(|_| ())
            .map_err(|error| format!("settings persistence refused: {error:?}"))
    }
}

#[cfg(test)]
mod tests {
    include!("settings_sink_tests.rs");
}
