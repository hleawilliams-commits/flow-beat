// SPDX-License-Identifier: GPL-3.0-only
//! Inactive one-shot terminal-reader report source. No Child, port or guest API.
//!
//! The supplied reader and frozen ReaderJournal stay in retained storage through
//! Read and unwinding. A spawn closure captures reader custody only through
//! another shared reference, so a returned spawn error retains that reader. The actual
//! JoinHandle is stored before a one-capacity start gate is released. The full
//! returned journal is published before a private unit notification is sent.
//!
//! Notification delivery, receiver observation, read outcome and actual join
//! are separate facts. EOF/error is derived only from the published journal.
//! No thread is restarted, reader retried, resource extracted, or child stopped.
//! Explicit join can block indefinitely. There is no custom Drop: ordinary
//! final destruction can close the supplied reader, and dropping an unfinished
//! JoinHandle detaches the thread. Retention lasts only while actual owners live;
//! allocation abort, process death and nonlocal exits remain outside this scope.
#![forbid(unsafe_code)]

use crate::supervisor_observation::{copy_controller, ControllerEvent, ReaderError, ReaderJournal};
use std::io::{self, Read};
use std::sync::{Arc, Mutex, OnceLock, TryLockError};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, RecvError, SendError, SyncSender, TryRecvError, TrySendError};
use std::thread::{self, JoinHandle};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StartPhase { Retained, SpawnArmed, SpawnFailed, HandleRetained, GateReleased, GateFailed }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StartError { AlreadyAttempted, SpawnFailed, GateReleaseFailed }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WorkerExit { GateDisconnected, ReaderCellPoisoned, CopyRefused, NotificationSent, NotificationFailed }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Notice { Received, Disconnected }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReportPoll {
    Pending,
    Completed { event: ControllerEvent, notice: Notice },
    ReporterDisconnected,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReportError { NotStarted, ReceiverUnavailable, CounterOverflow, AlreadyReported,
    PublicationMissing, CopyRefused }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum JoinPhase { NotAttempted, Armed, Returned }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum JoinOutcome { Returned(WorkerExit), Panicked }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum JoinError { NoWorker, AlreadyAttempted, CurrentWorker }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GateOutcome { Released, Full, Disconnected }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NotificationOutcome { Sent, ReceiverDisconnected }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ErrorSummary { pub kind: io::ErrorKind, pub raw_os_error: Option<i32> }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CellSnapshot {
    Busy,
    Available { poisoned: bool, journal_attempted: Option<bool>, journal_returned: Option<bool> },
}

/// Separately sampled bounded diagnostics, never a coherent instant or read
/// cancellation/worker-termination authority. No lock waits behind Read occur.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Snapshot {
    pub start_phase: StartPhase,
    pub spawn_returned: bool,
    pub spawn_error: Option<ErrorSummary>,
    pub handle_retained: bool,
    pub thread_finished: Option<bool>,
    pub gate_release_armed: bool,
    pub gate_return: Option<GateOutcome>,
    pub gate_wait_returned: bool,
    /// The call site was armed; the frozen journal may not yet be inspectable.
    pub copy_call_armed: bool,
    pub completed: bool,
    pub cell: CellSnapshot,
    pub notification_armed: bool,
    pub notification_return: Option<NotificationOutcome>,
    pub report_attempts: u64,
    pub last_receive: Option<Result<(), TryRecvError>>,
    pub terminal_report: Option<Result<ReportPoll, ReportError>>,
    pub join_phase: JoinPhase,
    pub join_return: Option<JoinOutcome>,
}

struct ReaderCell<R> { reader: R, journal: Option<ReaderJournal> }
struct CompletedRead { journal: ReaderJournal, copy_return: Result<ControllerEvent, ReaderError> }
struct Shared<R> {
    cell: Mutex<ReaderCell<R>>,
    gate_wait: OnceLock<Result<(), RecvError>>,
    copy_call_armed: AtomicBool,
    completed: OnceLock<CompletedRead>,
    notification_armed: AtomicBool,
    notification_return: OnceLock<Result<(), SendError<()>>>,
}

#[cfg(test)]
type NotificationBarrier = (mpsc::Sender<()>, Receiver<()>);

pub struct TerminalReader<R: Read + Send + 'static> {
    shared: Arc<Shared<R>>,
    phase: StartPhase,
    spawn_return: Option<io::Result<()>>,
    handle: Option<JoinHandle<WorkerExit>>,
    gate_tx: Option<SyncSender<()>>,
    gate_rx: Option<Receiver<()>>,
    gate_release_armed: bool,
    gate_return: Option<Result<(), TrySendError<()>>>,
    notification_tx: Option<mpsc::Sender<()>>,
    notification_rx: Option<Receiver<()>>,
    report_attempts: u64,
    last_receive: Option<Result<(), TryRecvError>>,
    terminal_report: Option<Result<ReportPoll, ReportError>>,
    join_phase: JoinPhase,
    join_return: Option<thread::Result<WorkerExit>>,
    #[cfg(test)] after_spawn: Option<(mpsc::Sender<Snapshot>, Receiver<()>)>,
    #[cfg(test)] before_notification: Option<NotificationBarrier>,
}

impl<R: Read + Send + 'static> TerminalReader<R> {
    /// Retains the actual supplied value before any thread/read call. This does
    /// not open, duplicate or establish exclusive native ownership of a reader.
    pub fn retain(reader: R) -> Self {
        let (gate_tx, gate_rx) = mpsc::sync_channel(1);
        let (notification_tx, notification_rx) = mpsc::channel();
        Self {
            shared: Arc::new(Shared {
                cell: Mutex::new(ReaderCell { reader, journal: Some(ReaderJournal::new()) }),
                gate_wait: OnceLock::new(), copy_call_armed: AtomicBool::new(false),
                completed: OnceLock::new(), notification_armed: AtomicBool::new(false),
                notification_return: OnceLock::new(),
            }),
            phase: StartPhase::Retained, spawn_return: None, handle: None,
            gate_tx: Some(gate_tx), gate_rx: Some(gate_rx), gate_release_armed: false,
            gate_return: None, notification_tx: Some(notification_tx),
            notification_rx: Some(notification_rx), report_attempts: 0,
            last_receive: None, terminal_report: None,
            join_phase: JoinPhase::NotAttempted, join_return: None,
            #[cfg(test)] after_spawn: None,
            #[cfg(test)] before_notification: None,
        }
    }

    pub fn start_once(&mut self) -> Result<(), StartError> {
        self.start_using(|job| thread::Builder::new().name("terminal-reader".into()).spawn(job))
    }

    fn start_using<F>(&mut self, spawn: F) -> Result<(), StartError>
    where F: FnOnce(Box<dyn FnOnce() -> WorkerExit + Send>) -> io::Result<JoinHandle<WorkerExit>> {
        if self.phase != StartPhase::Retained { return Err(StartError::AlreadyAttempted); }
        self.phase = StartPhase::SpawnArmed;
        let shared = self.shared.clone();
        let gate = self.gate_rx.take().expect("one retained start gate");
        let notify = self.notification_tx.take().expect("one private reporter sender");
        #[cfg(test)]
        let before_notification = self.before_notification.take();
        let job = Box::new(move || run_reader(shared, gate, notify,
            #[cfg(test)] before_notification));
        let returned = spawn(job);
        match returned {
            Err(error) => {
                self.spawn_return = Some(Err(error));
                self.phase = StartPhase::SpawnFailed;
                return Err(StartError::SpawnFailed);
            }
            Ok(handle) => {
                // Store the unique returned handle before permitting any Read.
                self.handle = Some(handle);
                self.spawn_return = Some(Ok(()));
                self.phase = StartPhase::HandleRetained;
            }
        }
        #[cfg(test)]
        if let Some((entered, proceed)) = self.after_spawn.take() {
            entered.send(self.snapshot()).expect("fixture spawn checkpoint receiver");
            proceed.recv_timeout(std::time::Duration::from_secs(15))
                .expect("fixture spawn checkpoint release deadline");
        }
        self.gate_release_armed = true;
        let returned = self.gate_tx.as_ref().expect("retained gate sender").try_send(());
        self.gate_return = Some(returned);
        if self.gate_return.as_ref().is_some_and(Result::is_ok) {
            self.phase = StartPhase::GateReleased;
            Ok(())
        } else {
            self.phase = StartPhase::GateFailed;
            Err(StartError::GateReleaseFailed)
        }
    }

    /// At most one actual try_recv per call. A terminal observation is retained
    /// once; later calls refuse without receiving again or relabeling EOF/error
    /// as channel loss. Pending is not a reader or liveness outcome.
    pub fn try_report(&mut self) -> Result<ReportPoll, ReportError> {
        if self.terminal_report.is_some() { return Err(ReportError::AlreadyReported); }
        if self.phase == StartPhase::Retained { return Err(ReportError::NotStarted); }
        let Some(receiver) = self.notification_rx.as_ref() else {
            return Err(ReportError::ReceiverUnavailable);
        };
        let attempt = self.report_attempts.checked_add(1).ok_or(ReportError::CounterOverflow)?;
        self.report_attempts = attempt;
        let returned = receiver.try_recv();
        self.last_receive = Some(returned);
        let notice = match returned {
            Err(TryRecvError::Empty) => return Ok(ReportPoll::Pending),
            Ok(()) => Notice::Received,
            Err(TryRecvError::Disconnected) => Notice::Disconnected,
        };
        let result = match self.shared.completed.get() {
            Some(completed) => match completed.copy_return {
                Ok(event) => Ok(ReportPoll::Completed { event, notice }),
                Err(_) => Err(ReportError::CopyRefused),
            },
            None if notice == Notice::Disconnected => Ok(ReportPoll::ReporterDisconnected),
            None => Err(ReportError::PublicationMissing),
        };
        self.terminal_report = Some(result);
        result
    }

    /// Explicit local thread join, never implicit cleanup. It can block for an
    /// indefinitely blocked supplied Read. A panic result is the actual payload,
    /// retained separately from read or notification results; no retry occurs.
    pub fn join(&mut self) -> Result<JoinOutcome, JoinError> {
        if self.join_phase != JoinPhase::NotAttempted { return Err(JoinError::AlreadyAttempted); }
        let Some(handle) = self.handle.as_ref() else { return Err(JoinError::NoWorker); };
        if handle.thread().id() == thread::current().id() { return Err(JoinError::CurrentWorker); }
        self.join_phase = JoinPhase::Armed;
        let handle = self.handle.take().expect("actual unique worker handle");
        let returned = handle.join();
        self.join_return = Some(returned);
        self.join_phase = JoinPhase::Returned;
        Ok(self.join_outcome().expect("actual join return retained"))
    }

    fn join_outcome(&self) -> Option<JoinOutcome> {
        self.join_return.as_ref().map(|result| match result {
            Ok(exit) => JoinOutcome::Returned(*exit), Err(_) => JoinOutcome::Panicked,
        })
    }
    pub fn completed_journal(&self) -> Option<&ReaderJournal> {
        self.shared.completed.get().map(|completed| &completed.journal)
    }
    pub fn copy_return(&self) -> Option<Result<ControllerEvent, ReaderError>> {
        self.shared.completed.get().map(|completed| completed.copy_return)
    }
    pub fn spawn_return(&self) -> Option<&io::Result<()>> { self.spawn_return.as_ref() }
    pub fn gate_return(&self) -> Option<&Result<(), TrySendError<()>>> { self.gate_return.as_ref() }
    pub fn gate_wait_return(&self) -> Option<&Result<(), RecvError>> { self.shared.gate_wait.get() }
    pub fn notification_return(&self) -> Option<&Result<(), SendError<()>>> {
        self.shared.notification_return.get()
    }
    pub fn join_return(&self) -> Option<&thread::Result<WorkerExit>> { self.join_return.as_ref() }

    pub fn snapshot(&self) -> Snapshot {
        let cell = match self.shared.cell.try_lock() {
            Err(TryLockError::WouldBlock) => CellSnapshot::Busy,
            result => {
                let (guard, poisoned) = match result {
                    Ok(guard) => (guard, false),
                    Err(TryLockError::Poisoned(error)) => (error.into_inner(), true),
                    Err(TryLockError::WouldBlock) => unreachable!(),
                };
                // Poison salvage is diagnostic only, never another read/retry.
                CellSnapshot::Available { poisoned,
                    journal_attempted: guard.journal.as_ref().map(ReaderJournal::attempted),
                    journal_returned: guard.journal.as_ref().map(|j| j.returned().is_some()) }
            }
        };
        Snapshot {
            start_phase: self.phase, spawn_returned: self.spawn_return.is_some(),
            spawn_error: self.spawn_return.as_ref().and_then(|r| r.as_ref().err()).map(|e|
                ErrorSummary { kind: e.kind(), raw_os_error: e.raw_os_error() }),
            handle_retained: self.handle.is_some(),
            thread_finished: self.handle.as_ref().map(JoinHandle::is_finished),
            gate_release_armed: self.gate_release_armed,
            gate_return: self.gate_return.as_ref().map(|r| match r {
                Ok(()) => GateOutcome::Released,
                Err(TrySendError::Full(())) => GateOutcome::Full,
                Err(TrySendError::Disconnected(())) => GateOutcome::Disconnected,
            }),
            gate_wait_returned: self.shared.gate_wait.get().is_some(),
            copy_call_armed: self.shared.copy_call_armed.load(Ordering::Acquire),
            completed: self.shared.completed.get().is_some(), cell,
            notification_armed: self.shared.notification_armed.load(Ordering::Acquire),
            notification_return: self.shared.notification_return.get().map(|r|
                if r.is_ok() { NotificationOutcome::Sent } else { NotificationOutcome::ReceiverDisconnected }),
            report_attempts: self.report_attempts, last_receive: self.last_receive,
            terminal_report: self.terminal_report, join_phase: self.join_phase,
            join_return: self.join_outcome(),
        }
    }

    /// A scripted spawn failure, not evidence of an OS thread-creation failure.
    /// The same returned-Err arm retains this exact io::Error object.
    #[cfg(test)]
    pub fn start_scripted_failure_for_test(&mut self, error: io::Error) -> Result<(), StartError> {
        self.start_using(move |_job| Err(error))
    }
    #[cfg(test)]
    pub fn drop_receiver_for_test(&mut self) { drop(self.notification_rx.take()); }
    /// Explicit fixture cleanup if the after-spawn checkpoint itself fails.
    /// Closing this private start gate does not close or read the supplied R.
    #[cfg(test)]
    pub fn drop_gate_for_test(&mut self) { drop(self.gate_tx.take()); }
    #[cfg(test)]
    pub fn set_after_spawn_barrier_for_test(&mut self, entered: mpsc::Sender<Snapshot>, proceed: Receiver<()>) {
        assert_eq!(self.phase, StartPhase::Retained);
        self.after_spawn = Some((entered, proceed));
    }
    #[cfg(test)]
    pub fn set_before_notification_barrier_for_test(&mut self, entered: mpsc::Sender<()>, proceed: Receiver<()>) {
        assert_eq!(self.phase, StartPhase::Retained);
        self.before_notification = Some((entered, proceed));
    }
}

fn run_reader<R: Read + Send + 'static>(
    shared: Arc<Shared<R>>, gate: Receiver<()>, notify: mpsc::Sender<()>,
    #[cfg(test)] before_notification: Option<NotificationBarrier>,
) -> WorkerExit {
    let gate_result = gate.recv();
    let allowed = gate_result.is_ok();
    shared.gate_wait.set(gate_result).unwrap_or_else(|_| panic!("single worker gate result"));
    if !allowed { return WorkerExit::GateDisconnected; }
    let (journal, copy_return) = {
        let mut cell = match shared.cell.lock() {
            Ok(cell) => cell,
            Err(_) => return WorkerExit::ReaderCellPoisoned,
        };
        shared.copy_call_armed.store(true, Ordering::Release);
        let ReaderCell { reader, journal } = &mut *cell;
        let result = copy_controller(reader, journal.as_mut().expect("retained one-shot journal"));
        // No callback/hook occurs between taking the returned journal and its
        // immutable publication. The reader itself is never taken from the cell.
        (journal.take().expect("returned journal retained"), result)
    };
    shared.completed.set(CompletedRead { journal, copy_return })
        .unwrap_or_else(|_| panic!("single immutable read publication"));
    if copy_return.is_err() { return WorkerExit::CopyRefused; }
    #[cfg(test)]
    if let Some((entered, proceed)) = before_notification {
        entered.send(()).expect("fixture publication checkpoint receiver");
        proceed.recv_timeout(std::time::Duration::from_secs(15))
            .expect("fixture publication checkpoint release deadline");
    }
    shared.notification_armed.store(true, Ordering::Release);
    let returned = notify.send(());
    let sent = returned.is_ok();
    shared.notification_return.set(returned)
        .unwrap_or_else(|_| panic!("single notification return"));
    if sent { WorkerExit::NotificationSent } else { WorkerExit::NotificationFailed }
}

#[cfg(test)]
#[path = "reader_reporter_tests.rs"]
mod tests;
