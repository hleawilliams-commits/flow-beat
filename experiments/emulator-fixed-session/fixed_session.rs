// SPDX-License-Identifier: GPL-3.0-only
//! Inactive, one-attempt owner for an injected backend and explicit directory.
//!
//! Retention exists before any backend call. One captured baseline is bound to
//! the actual settings bridge and served once to the route. The returned route
//! starts unarmed: this owner never confirms release/adoption or starts a worker.
//! Later baseline requests never query the underlying backend, so this route
//! cannot rearm a newer epoch after a failed settings callback.
//!
//! This is custody and local ordering, not receipt authentication, native-session
//! acquisition, exclusion of competing file users, cancellation, or recovery.
//! A supplied backend is trusted. Existing callbacks can block and a known FRAME
//! commit cannot be undone by a settings failure. Final Drop performs no restore.
//!
//! Snapshots copy journals separately, never call into the route/backend/sink,
//! and do no I/O. Returned backend outcomes are observations before the frozen
//! route validates them, not proof of producer acceptance or settings completion.
//! Panic propagates. Armed phases are retained as uncertainty, never retry credit.
//! Resources form a DAG: route proxies retain only backend+journal or settings;
//! neither proxy retains the owner or its route slot.
#![forbid(unsafe_code)]

use crate::mixer_producer::{BaselineReceipt, Envelope, FrameAck, MAX_QUEUE};
use crate::mixer_route::{BackendOutcome, MixerBackend, MixerRoute, SettingsSink};
use crate::settings_mapper::Selection;
use crate::settings_sink::{self, SettingsSinkBridge, SinkError};
use std::fs::File;
use std::sync::{Arc, Mutex};

/// A positive local non-commit reason. It is used only before forwarding submit.
pub const LOCAL_REFUSAL_REASON: i32 = 90;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Config {
    pub capacity: usize,
    pub namespace: [u8; 16],
    pub absent_mode: u32,
    pub selection: Selection,
}

/// Last published construction boundary. Failure does not erase this boundary.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InitPhase {
    Retained,
    Preflight,
    BaselineArmed,
    BaselineCaptured,
    SettingsRetained,
    RouteRetained,
    Ready,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PoisonTarget { Journal, Slots, Backend, Settings }

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum InitError {
    InvalidCapacity,
    InvalidNamespace,
    InvalidMode,
    AlreadyAttempted,
    Backend(String),
    DirectoryClone(Option<i32>),
    Settings(SinkError),
    Route(String),
    Poisoned(PoisonTarget),
    NotReady,
    BaselineAlreadyServed,
    BaselineNotServed,
    SubmissionInterrupted,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RefusalOperation { Initialize, Baseline, Submit }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Refusal {
    pub operation: RefusalOperation,
    pub error: InitError,
    pub request: Option<Envelope>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SubmitPhase {
    Armed,
    /// Exact wrapper-observed return, not producer ACK or callback completion.
    Returned(BackendOutcome),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SubmitAttempt {
    pub request: Envelope,
    pub phase: SubmitPhase,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct JournalEvidence {
    pub poisoned: bool,
    pub config: Option<Config>,
    pub phase: InitPhase,
    /// The complete result of the sole underlying baseline call, when returned.
    pub captured_baseline: Option<Result<BaselineReceipt, String>>,
    /// Set before returning the cached receipt to the route.
    pub baseline_served: bool,
    /// First initialization/poison failure, never replaced by later refusals.
    pub terminal: Option<InitError>,
    pub first_refusal: Option<Refusal>,
    pub last_submit: Option<SubmitAttempt>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Evidence {
    pub journal: JournalEvidence,
    pub slots_poisoned: bool,
    pub backend_poisoned: bool,
    pub settings_poisoned: bool,
    pub settings_retained: bool,
    pub route_retained: bool,
    pub settings: Option<settings_sink::Evidence>,
}

struct SettingsStorage {
    writer: Mutex<SettingsSinkBridge>,
    evidence: settings_sink::EvidenceHandle,
}

#[derive(Default)]
struct Slots {
    settings: Option<Arc<SettingsStorage>>,
    route: Option<Arc<MixerRoute>>,
}

struct Retention {
    backend: Arc<Mutex<Box<dyn MixerBackend>>>,
    original_directory: Arc<File>,
    slots: Mutex<Slots>,
    journal: Arc<Mutex<JournalEvidence>>,
}

#[derive(Clone)]
pub struct EvidenceHandle { retention: Arc<Retention> }

impl EvidenceHandle {
    /// Neither backend nor writer locks are taken, even while callbacks block.
    /// Separately copied journals and poison flags are not one coherent instant.
    pub fn snapshot(&self) -> Evidence {
        let journal = match self.retention.journal.lock() {
            Ok(journal) => journal.clone(),
            Err(error) => {
                let mut journal = error.into_inner().clone();
                journal.poisoned = true;
                journal
            }
        };
        let (settings, route_retained, slots_poisoned) = match self.retention.slots.lock() {
            Ok(slots) => (slots.settings.clone(), slots.route.is_some(), false),
            Err(error) => {
                let slots = error.into_inner();
                (slots.settings.clone(), slots.route.is_some(), true)
            }
        };
        let settings_poisoned = settings.as_ref().is_some_and(|s| s.writer.is_poisoned());
        let settings_evidence = settings.as_ref().map(|s| s.evidence.snapshot());
        Evidence {
            journal,
            slots_poisoned,
            backend_poisoned: self.retention.backend.is_poisoned(),
            settings_poisoned,
            settings_retained: settings.is_some(),
            route_retained,
            settings: settings_evidence,
        }
    }

    #[cfg(test)]
    pub fn poison_journal_for_test(&self) {
        poison_for_test(&self.retention.journal);
    }

    #[cfg(test)]
    pub fn poison_slots_for_test(&self) {
        poison_for_test(&self.retention.slots);
    }

    #[cfg(test)]
    pub fn poison_backend_for_test(&self) {
        poison_for_test(&self.retention.backend);
    }

    #[cfg(test)]
    pub fn poison_settings_for_test(&self) {
        let settings = self.retention.slots.lock().unwrap().settings.clone().unwrap();
        poison_for_test(&settings.writer);
    }

    #[cfg(test)]
    pub fn original_directory_for_test(&self) -> Arc<File> {
        self.retention.original_directory.clone()
    }

    #[cfg(test)]
    pub fn directory_identity_for_test(&self) -> std::io::Result<(u64, u64)> {
        use std::os::unix::fs::MetadataExt;
        let metadata = self.retention.original_directory.metadata()?;
        Ok((metadata.dev(), metadata.ino()))
    }
}

#[cfg(test)]
fn poison_for_test<T>(mutex: &Mutex<T>) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let _guard = mutex.lock().unwrap();
        panic!("intentional fixed-session test poison");
    }));
}

#[cfg(test)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HookPoint {
    AfterBaselineArmed,
    BaselineCaptured,
    SettingsRetained,
    RouteRetained,
    BaselineServed,
    /// After underlying return but before journal completion. A panic here
    /// leaves Armed; nonpanicking journal poison preserves the exact result.
    AfterUnderlyingSubmitReturn,
}

#[cfg(test)]
type Hooks = Arc<Mutex<Option<Box<dyn FnMut(HookPoint) + Send>>>>;

macro_rules! hook {
    ($self:expr, $point:expr) => {{
        #[cfg(test)]
        if let Some(callback) = $self.hooks.lock().unwrap().as_mut() {
            callback($point);
        }
    }};
}

pub struct FixedSession {
    retention: Arc<Retention>,
    #[cfg(test)]
    hooks: Hooks,
}

impl FixedSession {
    /// Retain the actual backend and original directory before any validation,
    /// directory duplication, baseline call, settings attach, or route creation.
    pub fn retain(backend: Box<dyn MixerBackend>, directory: File) -> (Self, EvidenceHandle) {
        let retention = Arc::new(Retention {
            backend: Arc::new(Mutex::new(backend)),
            original_directory: Arc::new(directory),
            slots: Mutex::new(Slots::default()),
            journal: Arc::new(Mutex::new(JournalEvidence {
                poisoned: false,
                config: None,
                phase: InitPhase::Retained,
                captured_baseline: None,
                baseline_served: false,
                terminal: None,
                first_refusal: None,
                last_submit: None,
            })),
        });
        (
            Self {
                retention: retention.clone(),
                #[cfg(test)]
                hooks: Arc::new(Mutex::new(None)),
            },
            EvidenceHandle { retention },
        )
    }

    /// Exactly one attempt, including invalid options and unwind. A later call
    /// records a refusal without invalidating an already returned healthy route.
    pub fn initialize(
        &mut self,
        capacity: usize,
        namespace: [u8; 16],
        absent_mode: u32,
        selection: Selection,
    ) -> Result<Arc<MixerRoute>, InitError> {
        let config = Config { capacity, namespace, absent_mode, selection };
        {
            let mut journal = match self.retention.journal.lock() {
                Ok(journal) => journal,
                Err(error) => {
                    let mut journal = error.into_inner();
                    journal.poisoned = true;
                    let failure = InitError::Poisoned(PoisonTarget::Journal);
                    if journal.terminal.is_none() { journal.terminal = Some(failure.clone()); }
                    return Err(failure);
                }
            };
            if journal.phase != InitPhase::Retained {
                let error = InitError::AlreadyAttempted;
                record_refusal(&mut journal, RefusalOperation::Initialize, error.clone(), None);
                return Err(error);
            }
            journal.config = Some(config);
            journal.phase = InitPhase::Preflight;
        }
        let invalid = if capacity == 0 || capacity > MAX_QUEUE {
            Some(InitError::InvalidCapacity)
        } else if namespace == [0; 16] {
            Some(InitError::InvalidNamespace)
        } else if absent_mode & !0o777 != 0 {
            Some(InitError::InvalidMode)
        } else { None };
        if let Some(error) = invalid { return self.fail(error); }
        if self.retention.slots.is_poisoned() {
            return self.fail(InitError::Poisoned(PoisonTarget::Slots));
        }
        self.set_phase(InitPhase::BaselineArmed)?;
        hook!(self, HookPoint::AfterBaselineArmed);
        let baseline_result = {
            let mut backend = match self.retention.backend.lock() {
                Ok(backend) => backend,
                Err(_) => return self.fail(InitError::Poisoned(PoisonTarget::Backend)),
            };
            backend.baseline()
        };
        // Salvage only to preserve the actual returned result. Poison is never
        // cleared and cannot grant initialization or admission.
        let poisoned = match self.retention.journal.lock() {
            Ok(mut journal) => {
                journal.captured_baseline = Some(baseline_result.clone());
                journal.phase = InitPhase::BaselineCaptured;
                false
            }
            Err(error) => {
                let mut journal = error.into_inner();
                journal.captured_baseline = Some(baseline_result.clone());
                journal.phase = InitPhase::BaselineCaptured;
                journal.poisoned = true;
                true
            }
        };
        if poisoned { return self.fail(InitError::Poisoned(PoisonTarget::Journal)); }
        hook!(self, HookPoint::BaselineCaptured);
        let baseline = match baseline_result {
            Ok(baseline) => baseline,
            Err(error) => return self.fail(InitError::Backend(error)),
        };
        self.ensure_initializing()?;
        let directory = match self.retention.original_directory.try_clone() {
            Ok(directory) => directory,
            Err(error) => return self.fail(InitError::DirectoryClone(error.raw_os_error())),
        };
        let (writer, evidence) = match SettingsSinkBridge::attach(
            directory, baseline, namespace, absent_mode, selection,
        ) {
            Ok(settings) => settings,
            Err(error) => return self.fail(InitError::Settings(error)),
        };
        let settings = Arc::new(SettingsStorage { writer: Mutex::new(writer), evidence });
        let poisoned = match self.retention.slots.lock() {
            Ok(mut slots) => { slots.settings = Some(settings.clone()); false }
            Err(error) => {
                // Custody publication survives poisoned slots; it grants no use.
                error.into_inner().settings = Some(settings.clone());
                true
            }
        };
        if poisoned { return self.fail(InitError::Poisoned(PoisonTarget::Slots)); }
        self.set_phase(InitPhase::SettingsRetained)?;
        hook!(self, HookPoint::SettingsRetained);
        self.ensure_initializing()?;
        let backend = BackendProxy {
            backend: self.retention.backend.clone(),
            journal: self.retention.journal.clone(),
            #[cfg(test)]
            hooks: self.hooks.clone(),
        };
        let route = match MixerRoute::new(capacity, Box::new(backend), Box::new(SinkProxy {
            settings: settings.clone(),
        })) {
            Ok(route) => Arc::new(route),
            Err(error) => return self.fail(InitError::Route(format!("{error:?}"))),
        };
        let poisoned = match self.retention.slots.lock() {
            Ok(mut slots) => { slots.route = Some(route.clone()); false }
            Err(error) => {
                error.into_inner().route = Some(route.clone());
                true
            }
        };
        if poisoned { return self.fail(InitError::Poisoned(PoisonTarget::Slots)); }
        self.set_phase(InitPhase::RouteRetained)?;
        hook!(self, HookPoint::RouteRetained);
        self.ensure_initializing()?;
        self.set_phase(InitPhase::Ready)?;
        Ok(route)
    }

    fn fail<T>(&self, error: InitError) -> Result<T, InitError> {
        let mut journal = match self.retention.journal.lock() {
            Ok(journal) => journal,
            Err(error) => {
                let mut journal = error.into_inner();
                journal.poisoned = true;
                journal
            }
        };
        if journal.terminal.is_none() { journal.terminal = Some(error.clone()); }
        Err(error)
    }

    fn set_phase(&self, phase: InitPhase) -> Result<(), InitError> {
        let mut journal = match self.retention.journal.lock() {
            Ok(journal) => journal,
            Err(error) => {
                let mut journal = error.into_inner();
                journal.poisoned = true;
                let failure = InitError::Poisoned(PoisonTarget::Journal);
                if journal.terminal.is_none() { journal.terminal = Some(failure.clone()); }
                return Err(failure);
            }
        };
        journal.phase = phase;
        Ok(())
    }

    fn ensure_initializing(&self) -> Result<(), InitError> {
        if self.retention.slots.is_poisoned() {
            return self.fail(InitError::Poisoned(PoisonTarget::Slots));
        }
        if self.retention.backend.is_poisoned() {
            return self.fail(InitError::Poisoned(PoisonTarget::Backend));
        }
        if self.retention.journal.is_poisoned() {
            return self.fail(InitError::Poisoned(PoisonTarget::Journal));
        }
        let settings = match self.retention.slots.lock() {
            Ok(slots) => slots.settings.clone(),
            Err(_) => return self.fail(InitError::Poisoned(PoisonTarget::Slots)),
        };
        if settings.as_ref().is_some_and(|settings| settings.writer.is_poisoned()) {
            return self.fail(InitError::Poisoned(PoisonTarget::Settings));
        }
        Ok(())
    }

    #[cfg(test)]
    pub fn set_hook(&mut self, callback: Box<dyn FnMut(HookPoint) + Send>) {
        *self.hooks.lock().unwrap() = Some(callback);
    }

    #[cfg(test)]
    fn unused_settings(&self) -> Result<Arc<SettingsStorage>, InitError> {
        {
            let journal = self.retention.journal.lock()
                .map_err(|_| InitError::Poisoned(PoisonTarget::Journal))?;
            if journal.phase != InitPhase::Ready || journal.terminal.is_some()
                || journal.baseline_served || journal.last_submit.is_some()
            { return Err(InitError::NotReady); }
        }
        self.retention.slots.lock().map_err(|_| InitError::Poisoned(PoisonTarget::Slots))?
            .settings.clone().ok_or(InitError::NotReady)
    }

    #[cfg(test)]
    pub fn set_sink_hook(
        &mut self, callback: Box<dyn FnMut(settings_sink::HookPoint) + Send>,
    ) -> Result<(), InitError> {
        let settings = self.unused_settings()?;
        settings.writer.lock().map_err(|_| InitError::Poisoned(PoisonTarget::Settings))?
            .set_hook(callback);
        Ok(())
    }

    #[cfg(test)]
    pub fn set_pair_fault_hook(
        &mut self,
        callback: Box<dyn FnMut(crate::settings_pair::FaultPoint) -> crate::settings_pair::FaultAction + Send>,
    ) -> Result<(), InitError> {
        let settings = self.unused_settings()?;
        settings.writer.lock().map_err(|_| InitError::Poisoned(PoisonTarget::Settings))?
            .set_pair_fault_hook(callback);
        Ok(())
    }
}

fn record_refusal(
    journal: &mut JournalEvidence, operation: RefusalOperation,
    error: InitError, request: Option<Envelope>,
) {
    if journal.first_refusal.is_none() {
        journal.first_refusal = Some(Refusal { operation, error, request });
    }
}

struct BackendProxy {
    backend: Arc<Mutex<Box<dyn MixerBackend>>>,
    journal: Arc<Mutex<JournalEvidence>>,
    #[cfg(test)]
    hooks: Hooks,
}

impl BackendProxy {
    fn refused(&self, request: Envelope, error: InitError) -> BackendOutcome {
        if let Ok(mut journal) = self.journal.lock() {
            record_refusal(&mut journal, RefusalOperation::Submit, error, Some(request));
        }
        BackendOutcome::Rejected { request, reason: LOCAL_REFUSAL_REASON }
    }
}

impl MixerBackend for BackendProxy {
    fn baseline(&mut self) -> Result<BaselineReceipt, String> {
        let baseline = {
            let mut journal = self.journal.lock()
                .map_err(|_| format!("{:?}", InitError::Poisoned(PoisonTarget::Journal)))?;
            let invalid = if self.backend.is_poisoned() {
                Some(InitError::Poisoned(PoisonTarget::Backend))
            } else if journal.phase != InitPhase::Ready || journal.terminal.is_some() {
                Some(InitError::NotReady)
            } else if journal.baseline_served {
                Some(InitError::BaselineAlreadyServed)
            } else { None };
            if let Some(error) = invalid {
                record_refusal(&mut journal, RefusalOperation::Baseline, error.clone(), None);
                return Err(format!("{error:?}"));
            }
            let baseline = match journal.captured_baseline.as_ref() {
                Some(Ok(baseline)) => *baseline,
                _ => {
                    record_refusal(&mut journal, RefusalOperation::Baseline, InitError::NotReady, None);
                    return Err(format!("{:?}", InitError::NotReady));
                }
            };
            journal.baseline_served = true;
            baseline
        };
        hook!(self, HookPoint::BaselineServed);
        Ok(baseline)
    }

    fn submit(&mut self, request: Envelope) -> BackendOutcome {
        {
            let mut journal = match self.journal.lock() {
                Ok(journal) => journal,
                Err(_) => return BackendOutcome::Rejected { request, reason: LOCAL_REFUSAL_REASON },
            };
            let invalid = if journal.phase != InitPhase::Ready || journal.terminal.is_some() {
                Some(InitError::NotReady)
            } else if !journal.baseline_served {
                Some(InitError::BaselineNotServed)
            } else if journal.last_submit.is_some_and(|attempt| attempt.phase == SubmitPhase::Armed) {
                Some(InitError::SubmissionInterrupted)
            } else if self.backend.is_poisoned() {
                Some(InitError::Poisoned(PoisonTarget::Backend))
            } else { None };
            if let Some(error) = invalid {
                record_refusal(&mut journal, RefusalOperation::Submit, error, Some(request));
                return BackendOutcome::Rejected { request, reason: LOCAL_REFUSAL_REASON };
            }
            journal.last_submit = Some(SubmitAttempt { request, phase: SubmitPhase::Armed });
        }
        // No journal lock spans the injected call. A panic poisons the actual
        // retained backend mutex and leaves the submitted envelope Armed.
        let outcome = {
            let mut backend = match self.backend.lock() {
                Ok(backend) => backend,
                Err(_) => return self.refused(request, InitError::Poisoned(PoisonTarget::Backend)),
            };
            backend.submit(request)
        };
        hook!(self, HookPoint::AfterUnderlyingSubmitReturn);
        match self.journal.lock() {
            Ok(mut journal) => {
                journal.last_submit = Some(SubmitAttempt { request, phase: SubmitPhase::Returned(outcome) });
                outcome
            }
            Err(error) => {
                // Diagnostic salvage preserves what actually returned. It does
                // not accept that receipt: UNKNOWN conservatively closes route.
                let mut journal = error.into_inner();
                journal.poisoned = true;
                journal.last_submit = Some(SubmitAttempt { request, phase: SubmitPhase::Returned(outcome) });
                if journal.terminal.is_none() {
                    journal.terminal = Some(InitError::Poisoned(PoisonTarget::Journal));
                }
                BackendOutcome::Unknown
            }
        }
    }
}

struct SinkProxy { settings: Arc<SettingsStorage> }

impl SettingsSink for SinkProxy {
    fn commit(&mut self, ack: FrameAck) -> Result<(), String> {
        let mut writer = self.settings.writer.lock()
            .map_err(|_| format!("{:?}", InitError::Poisoned(PoisonTarget::Settings)))?;
        writer.commit(ack)
    }
}

#[cfg(test)]
#[path = "fixed_session_tests.rs"]
mod tests;
