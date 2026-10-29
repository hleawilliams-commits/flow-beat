// SPDX-License-Identifier: GPL-3.0-only
//! Inactive retained composition of fixed route, real worker and lifecycle.
//!
//! No guest is started and no backend is acquired here. The supplied backend,
//! original directory and optional supervisor value are retained before startup.
//! Every returned product is published before later fallible hooks. A surviving
//! handle owns actual resources, not only copied evidence. Final Drop and process
//! death remain ordinary destruction, not recovery or permission for teardown.
//!
//! Start and explicit finish share a nonblocking operation lease. A busy finish
//! returns InProgress without requesting cancellation. Unwind releases only that
//! lease; it does not implicitly stop, join, restore, release or retry anything.
//! A later explicit finish may promote an intact returned pair and join it.
//! Poisoned evidence is inspectable, never an operational recovery authority.
//!
//! The frozen channel can start a thread before returning its pair. If it unwinds
//! internally before returning, ChannelArmed alone cannot prove whether a worker
//! exists. Such a gap remains StartupUnproven; this owner invents no join receipt.
//! This is local custody, not native-stop, authoritative acquisition, transport,
//! callback deadlines, durable recovery, platform termination or writer exclusion.
#![forbid(unsafe_code)]

use crate::fixed_session::{self, Config, FixedSession};
use crate::mixer_route::{MixerBackend, MixerRoute};
use crate::mixer_transport::{self, MixerPort, MixerWorker, TransportError};
use crate::owned_lifecycle::{FinishStatus, LifecycleControl, LifecycleSnapshot,
    OwnedLifecycle, RejectedOwner};
use std::fs::File;
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicBool, Ordering};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StartPhase {
    Retained, FixedArmed, RouteRetained, ChannelArmed, PairRetained,
    OwnerRetained, Ready,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PoisonTarget { Journal, Products, FixedSession, Supervisor }

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ChannelFailure { InvalidCapacity, SpawnFailed, Other(String) }

impl From<TransportError> for ChannelFailure {
    fn from(error: TransportError) -> Self {
        match error {
            TransportError::InvalidCapacity => Self::InvalidCapacity,
            TransportError::SpawnFailed => Self::SpawnFailed,
            other => Self::Other(format!("{other:?}")),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ChannelReturn { Succeeded, Failed(ChannelFailure) }

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StartError {
    Busy,
    AlreadyAttempted,
    FinishRequested,
    Fixed(fixed_session::InitError),
    Channel(ChannelFailure),
    Poisoned(PoisonTarget),
    PairRejected,
    Interrupted,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FinishResult {
    /// No lease was acquired; this does not request cancellation.
    InProgress,
    /// No worker was returned/retained, not native quiescence or a join receipt.
    NoWorkerRetained,
    /// A channel attempt may have spawned before returning; no join is invented.
    StartupUnproven,
    Poisoned(PoisonTarget),
    PairRejected,
    Lifecycle(FinishStatus),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct JournalEvidence {
    pub poisoned: bool,
    pub config: Option<Config>,
    /// Last startup publication; explicit finish does not pretend start returned.
    pub phase: StartPhase,
    pub finish_requested: bool,
    pub terminal: Option<StartError>,
    pub first_refusal: Option<StartError>,
    pub channel_return: Option<ChannelReturn>,
    /// Actual observed result of the latest nonbusy explicit finish.
    pub last_finish: Option<FinishResult>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StartupSnapshot {
    pub journal: JournalEvidence,
    pub operation_active: bool,
    pub fixed_session_poisoned: bool,
    pub products_poisoned: bool,
    pub supervisor_poisoned: bool,
    pub supervisor_retained: bool,
    pub route_retained: bool,
    pub pending_pair_retained: bool,
    pub owner_retained: bool,
    pub rejected_inputs_retained: bool,
    pub fixed: fixed_session::Evidence,
    pub lifecycle: Option<Result<LifecycleSnapshot, FinishStatus>>,
}

/// Never locks or calls G; it only extends custody through the frozen owner.
struct SupervisorRef<G> { _value: Arc<Mutex<G>> }

struct PendingPair { port: MixerPort, worker: MixerWorker }

struct Products<G> {
    route: Option<Arc<MixerRoute>>,
    pending: Option<PendingPair>,
    owner: Option<Arc<OwnedLifecycle<SupervisorRef<G>>>>,
    rejected: Option<RejectedOwner<SupervisorRef<G>>>,
}
impl<G> Default for Products<G> {
    fn default() -> Self {
        Self { route: None, pending: None, owner: None, rejected: None }
    }
}

struct Shared<G> {
    fixed: Mutex<FixedSession>,
    fixed_evidence: fixed_session::EvidenceHandle,
    supervisor: Option<Arc<Mutex<G>>>,
    products: Mutex<Products<G>>,
    journal: Mutex<JournalEvidence>,
    operation: AtomicBool,
}

/// The lease controls competing constructor/finish calls, not external callers
/// of an already returned LifecycleControl. Its destructor only clears the bit.
struct OperationLease<'a> { flag: &'a AtomicBool }
impl<'a> OperationLease<'a> {
    fn acquire(flag: &'a AtomicBool) -> Option<Self> {
        flag.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .ok().map(|_| Self { flag })
    }
}
impl Drop for OperationLease<'_> {
    fn drop(&mut self) { self.flag.store(false, Ordering::Release); }
}

pub struct StartupHandle<G: Send + 'static> { shared: Arc<Shared<G>> }
impl<G: Send + 'static> Clone for StartupHandle<G> {
    fn clone(&self) -> Self { Self { shared: self.shared.clone() } }
}

impl<G: Send + 'static> StartupHandle<G> {
    /// Copies each layer separately. This is neither a coherent instant nor a
    /// fresh native/filesystem observation. The mutable fixed session, backend,
    /// supervisor and worker are never locked or invoked by this method.
    pub fn snapshot(&self) -> StartupSnapshot {
        let journal = match self.shared.journal.lock() {
            Ok(journal) => journal.clone(),
            Err(error) => {
                let mut journal = error.into_inner().clone();
                journal.poisoned = true;
                journal
            }
        };
        let (route_retained, pending_pair_retained, owner, rejected_inputs_retained,
            products_poisoned) = match self.shared.products.lock() {
            Ok(products) => (products.route.is_some(), products.pending.is_some(),
                products.owner.clone(), products.rejected.is_some(), false),
            Err(error) => {
                let products = error.into_inner();
                (products.route.is_some(), products.pending.is_some(),
                    products.owner.clone(), products.rejected.is_some(), true)
            }
        };
        // No products lock spans another layer's snapshot.
        let lifecycle = owner.as_ref().map(|owner| owner.snapshot());
        let fixed = self.shared.fixed_evidence.snapshot();
        StartupSnapshot {
            journal,
            operation_active: self.shared.operation.load(Ordering::Acquire),
            fixed_session_poisoned: self.shared.fixed.is_poisoned(),
            products_poisoned,
            supervisor_poisoned: self.shared.supervisor.as_ref()
                .is_some_and(|supervisor| supervisor.is_poisoned()),
            supervisor_retained: self.shared.supervisor.is_some(),
            route_retained,
            pending_pair_retained,
            owner_retained: owner.is_some(),
            rejected_inputs_retained,
            fixed,
            lifecycle,
        }
    }

    /// Explicit local completion. It never starts a new worker or reinitializes
    /// a fixed session. A busy call has no cancellation/finish-request effect.
    pub fn finish(&self) -> FinishResult {
        let Some(_lease) = OperationLease::acquire(&self.shared.operation) else {
            return FinishResult::InProgress;
        };
        let (phase, channel_return) = {
            let mut journal = match self.shared.journal.lock() {
                Ok(journal) => journal,
                Err(_) => return FinishResult::Poisoned(PoisonTarget::Journal),
            };
            journal.finish_requested = true;
            (journal.phase, journal.channel_return.clone())
        };
        if self.shared.products.is_poisoned() {
            return self.shared.record_finish(FinishResult::Poisoned(PoisonTarget::Products));
        }
        // A pending exact pair may survive a startup hook unwind. Promotion
        // calls no external factory, hook or callback and spawns no new worker.
        let owner = match self.shared.promote() {
            Ok(Some(owner)) => owner,
            Ok(None) => {
                let unknown_channel = matches!(phase, StartPhase::ChannelArmed
                    | StartPhase::PairRetained | StartPhase::OwnerRetained | StartPhase::Ready)
                    && !matches!(channel_return, Some(ChannelReturn::Failed(_)));
                return self.shared.record_finish(if unknown_channel {
                    FinishResult::StartupUnproven
                } else { FinishResult::NoWorkerRetained });
            }
            Err(StartError::Poisoned(target)) => {
                return self.shared.record_finish(FinishResult::Poisoned(target));
            }
            Err(_) => return self.shared.record_finish(FinishResult::PairRejected),
        };
        // Stop/join and inherited callback limitations remain outside all
        // startup locks. The frozen lifecycle retains the exact joined session.
        let result = owner.finish();
        self.shared.record_finish(FinishResult::Lifecycle(result))
    }

    #[cfg(test)]
    pub fn poison_journal_for_test(&self) { poison_for_test(&self.shared.journal); }

    #[cfg(test)]
    pub fn poison_products_for_test(&self) { poison_for_test(&self.shared.products); }

    #[cfg(test)]
    pub fn original_directory_for_test(&self) -> Arc<File> {
        self.shared.fixed_evidence.original_directory_for_test()
    }
}

impl<G: Send + 'static> Shared<G> {
    fn record_finish(&self, result: FinishResult) -> FinishResult {
        match self.journal.lock() {
            Ok(mut journal) => { journal.last_finish = Some(result.clone()); result }
            Err(error) => {
                // Retain an actually observed result without clearing poison or
                // treating that diagnostic salvage as a successful operation.
                let mut journal = error.into_inner();
                journal.poisoned = true;
                journal.last_finish = Some(result);
                FinishResult::Poisoned(PoisonTarget::Journal)
            }
        }
    }

    fn fail<T>(&self, error: StartError) -> Result<T, StartError> {
        let mut journal = match self.journal.lock() {
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

    fn set_phase(&self, phase: StartPhase) -> Result<(), StartError> {
        let mut journal = match self.journal.lock() {
            Ok(journal) => journal,
            Err(error) => {
                let mut journal = error.into_inner();
                journal.poisoned = true;
                let failure = StartError::Poisoned(PoisonTarget::Journal);
                if journal.terminal.is_none() { journal.terminal = Some(failure.clone()); }
                return Err(failure);
            }
        };
        journal.phase = phase;
        Ok(())
    }

    fn ensure_startable(&self) -> Result<(), StartError> {
        if self.journal.is_poisoned() {
            return self.fail(StartError::Poisoned(PoisonTarget::Journal));
        }
        if self.products.is_poisoned() {
            return self.fail(StartError::Poisoned(PoisonTarget::Products));
        }
        if self.fixed.is_poisoned() {
            return self.fail(StartError::Poisoned(PoisonTarget::FixedSession));
        }
        if self.supervisor.as_ref().is_some_and(|supervisor| supervisor.is_poisoned()) {
            return self.fail(StartError::Poisoned(PoisonTarget::Supervisor));
        }
        Ok(())
    }

    /// No-hook, no-callback ownership transfer while products remain locked.
    /// The frozen pairing check is identity-only and returns every input on Err.
    /// Allocation abort/process death is not covered by retained Rust ownership.
    fn promote(&self) -> Result<Option<Arc<OwnedLifecycle<SupervisorRef<G>>>>, StartError> {
        if self.journal.is_poisoned() {
            return Err(StartError::Poisoned(PoisonTarget::Journal));
        }
        let mut products = self.products.lock()
            .map_err(|_| StartError::Poisoned(PoisonTarget::Products))?;
        if products.rejected.is_some() { return Err(StartError::PairRejected); }
        if let Some(owner) = &products.owner { return Ok(Some(owner.clone())); }
        let Some(pending) = products.pending.take() else { return Ok(None); };
        let supervisor = self.supervisor.as_ref()
            .map(|value| SupervisorRef { _value: value.clone() });
        let port = Arc::new(pending.port);
        match OwnedLifecycle::try_new(port, pending.worker, supervisor) {
            Ok(owner) => {
                let owner = Arc::new(owner);
                products.owner = Some(owner.clone());
                Ok(Some(owner))
            }
            Err(rejected) => {
                products.rejected = Some(rejected);
                Err(StartError::PairRejected)
            }
        }
    }

    fn record_channel_return(&self, returned: ChannelReturn) -> Result<(), StartError> {
        match self.journal.lock() {
            Ok(mut journal) => { journal.channel_return = Some(returned); Ok(()) }
            Err(error) => {
                let mut journal = error.into_inner();
                journal.poisoned = true;
                journal.channel_return = Some(returned);
                let failure = StartError::Poisoned(PoisonTarget::Journal);
                if journal.terminal.is_none() { journal.terminal = Some(failure.clone()); }
                Err(failure)
            }
        }
    }
}

#[cfg(test)]
fn poison_for_test<T>(mutex: &Mutex<T>) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let _guard = mutex.lock().unwrap();
        panic!("intentional owned-startup test poison");
    }));
}

#[cfg(test)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HookPoint { FixedArmed, RouteRetained, ChannelArmed, PairRetained, OwnerRetained }

pub struct OwnedStartup<G: Send + 'static> {
    shared: Arc<Shared<G>>,
    #[cfg(test)]
    hook: Option<Box<dyn FnMut(HookPoint) + Send>>,
    #[cfg(test)]
    fail_spawn: bool,
    #[cfg(test)]
    sink_hook: Option<Box<dyn FnMut(crate::settings_sink::HookPoint) + Send>>,
}

macro_rules! hook {
    ($self:expr, $point:expr) => {{
        #[cfg(test)]
        if let Some(callback) = $self.hook.as_mut() { callback($point); }
    }};
}

impl<G: Send + 'static> OwnedStartup<G> {
    /// No callbacks, file observations or worker/guest creation happen here.
    pub fn retain(
        backend: Box<dyn MixerBackend>, directory: File, supervisor: Option<G>,
    ) -> (Self, StartupHandle<G>) {
        let (fixed, fixed_evidence) = FixedSession::retain(backend, directory);
        let shared = Arc::new(Shared {
            fixed: Mutex::new(fixed),
            fixed_evidence,
            supervisor: supervisor.map(|value| Arc::new(Mutex::new(value))),
            products: Mutex::new(Products::default()),
            journal: Mutex::new(JournalEvidence {
                poisoned: false, config: None, phase: StartPhase::Retained,
                finish_requested: false, terminal: None, first_refusal: None,
                channel_return: None, last_finish: None,
            }),
            operation: AtomicBool::new(false),
        });
        (Self {
            shared: shared.clone(),
            #[cfg(test)] hook: None,
            #[cfg(test)] fail_spawn: false,
            #[cfg(test)] sink_hook: None,
        }, StartupHandle { shared })
    }

    /// One startup attempt. The returned lifecycle starts with an unarmed port;
    /// neither baseline refresh nor release/adoption is performed here.
    pub fn start(&mut self, config: Config) -> Result<Arc<dyn LifecycleControl>, StartError> {
        let shared = self.shared.clone();
        let Some(_lease) = OperationLease::acquire(&shared.operation) else {
            return Err(StartError::Busy);
        };
        {
            let mut journal = match shared.journal.lock() {
                Ok(journal) => journal,
                Err(error) => {
                    let mut journal = error.into_inner();
                    journal.poisoned = true;
                    let failure = StartError::Poisoned(PoisonTarget::Journal);
                    if journal.terminal.is_none() { journal.terminal = Some(failure.clone()); }
                    return Err(failure);
                }
            };
            let refusal = if journal.finish_requested { Some(StartError::FinishRequested) }
                else if journal.phase != StartPhase::Retained { Some(StartError::AlreadyAttempted) }
                else { None };
            if let Some(refusal) = refusal {
                if journal.first_refusal.is_none() { journal.first_refusal = Some(refusal.clone()); }
                return Err(refusal);
            }
            journal.config = Some(config);
            journal.phase = StartPhase::FixedArmed;
        }
        shared.ensure_startable()?;
        hook!(self, HookPoint::FixedArmed);
        shared.ensure_startable()?;
        let route_result = {
            let mut fixed = match shared.fixed.lock() {
                Ok(fixed) => fixed,
                Err(_) => return shared.fail(StartError::Poisoned(PoisonTarget::FixedSession)),
            };
            let result = fixed.initialize(config.capacity, config.namespace,
                config.absent_mode, config.selection);
            #[cfg(test)]
            if result.is_ok() {
                if let Some(callback) = self.sink_hook.take() {
                    if let Err(error) = fixed.set_sink_hook(callback) {
                        return shared.fail(StartError::Fixed(error));
                    }
                }
            }
            result
        };
        let route = match route_result {
            Ok(route) => route,
            Err(error) => return shared.fail(StartError::Fixed(error)),
        };
        let products_poisoned = match shared.products.lock() {
            Ok(mut products) => { products.route = Some(route.clone()); false }
            Err(error) => {
                // Returned-resource custody only; never promote through poison.
                error.into_inner().route = Some(route.clone());
                true
            }
        };
        if products_poisoned { return shared.fail(StartError::Poisoned(PoisonTarget::Products)); }
        shared.set_phase(StartPhase::RouteRetained)?;
        hook!(self, HookPoint::RouteRetained);
        shared.ensure_startable()?;
        shared.set_phase(StartPhase::ChannelArmed)?;
        hook!(self, HookPoint::ChannelArmed);
        shared.ensure_startable()?;
        #[cfg(not(test))]
        let channel_result = mixer_transport::channel(route, config.capacity);
        #[cfg(test)]
        let channel_result = if self.fail_spawn {
            mixer_transport::fail_spawn_for_owned_startup(route, config.capacity)
        } else { mixer_transport::channel(route, config.capacity) };
        let (port, worker) = match channel_result {
            Ok(pair) => pair,
            Err(error) => {
                let failure = ChannelFailure::from(error);
                shared.record_channel_return(ChannelReturn::Failed(failure.clone()))?;
                return shared.fail(StartError::Channel(failure));
            }
        };
        let pending = PendingPair { port, worker };
        let products_poisoned = match shared.products.lock() {
            Ok(mut products) => { products.pending = Some(pending); false }
            Err(error) => {
                // Even poisoned custody must retain this actual returned worker.
                error.into_inner().pending = Some(pending);
                true
            }
        };
        shared.record_channel_return(ChannelReturn::Succeeded)?;
        if products_poisoned { return shared.fail(StartError::Poisoned(PoisonTarget::Products)); }
        shared.set_phase(StartPhase::PairRetained)?;
        hook!(self, HookPoint::PairRetained);
        shared.ensure_startable()?;
        let owner = match shared.promote() {
            Ok(Some(owner)) => owner,
            Ok(None) => return shared.fail(StartError::Interrupted),
            Err(error) => return shared.fail(error),
        };
        shared.set_phase(StartPhase::OwnerRetained)?;
        hook!(self, HookPoint::OwnerRetained);
        shared.ensure_startable()?;
        shared.set_phase(StartPhase::Ready)?;
        Ok(owner)
    }

    #[cfg(test)]
    pub fn set_hook(&mut self, hook: Box<dyn FnMut(HookPoint) + Send>) {
        self.hook = Some(hook);
    }

    #[cfg(test)]
    pub fn fail_spawn_for_test(&mut self) { self.fail_spawn = true; }

    #[cfg(test)]
    pub fn set_sink_hook_for_test(
        &mut self, hook: Box<dyn FnMut(crate::settings_sink::HookPoint) + Send>,
    ) {
        self.sink_hook = Some(hook);
    }
}

#[cfg(test)]
#[path = "owned_startup_tests.rs"]
mod tests;
