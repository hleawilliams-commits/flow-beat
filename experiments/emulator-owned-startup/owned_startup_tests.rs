// SPDX-License-Identifier: GPL-3.0-only
// Offline startup ownership composition: actual frozen worker/lifecycle threads,
// synthetic backend and supervisor, and real files under an explicit private root.
// Every case reaching a real worker explicitly finishes it before control assertions.
use super::*;
use crate::fixed_session::{Config, InitPhase};
use crate::mixer_producer::{AckCheckpoint, AckSettings, BaselineReceipt, Envelope, Frame, FrameAck, Key};
use crate::mixer_route::{BackendOutcome, MixerBackend};
use crate::mixer_transport::{AttemptOutcome, WorkerExit};
use crate::owned_lifecycle::{FinishStatus, LifecyclePhase, LifecycleSnapshot, StopStatus};
use crate::settings_mapper::Selection;
use crate::settings_pair::TransactionResult;
use crate::settings_sink::{AttemptPhase, HookPoint as SinkHookPoint};
use std::fs::{self, File};
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;
use std::sync::{Arc, Mutex, mpsc};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread;
use std::time::{Duration, Instant};

const WAIT: Duration = Duration::from_secs(10);
const SAVED: &[u8] = b"{\"10\": 99, \"22\": 160, \"26\": 7, \"27\": 8}";
static NEXT_CASE: AtomicUsize = AtomicUsize::new(1);

struct Case { path: PathBuf }
impl Case {
    fn new(label: &str) -> Self {
        let root = PathBuf::from(std::env::var_os("AZ_OWNED_STARTUP_TEST_ROOT")
            .expect("runner must supply an explicit isolated owned-startup root"));
        assert_eq!(root.is_absolute(), true);
        let m = fs::symlink_metadata(&root).unwrap();
        assert_eq!(m.is_dir() && !m.file_type().is_symlink(), true);
        let path = root.join(format!("{}-{}-{}", label, std::process::id(),
            NEXT_CASE.fetch_add(1, Ordering::Relaxed)));
        fs::create_dir(&path).unwrap();
        fs::set_permissions(&path, fs::Permissions::from_mode(0o700)).unwrap();
        let case = Self { path };
        case.write("knob.ctr", b"017\n"); case.write("mixer.state", SAVED);
        case
    }
    fn write(&self, name: &str, bytes: &[u8]) {
        fs::write(self.path.join(name), bytes).unwrap();
        fs::set_permissions(self.path.join(name), fs::Permissions::from_mode(0o600)).unwrap();
    }
    fn file(&self) -> File { File::open(&self.path).unwrap() }
    fn read(&self, name: &str) -> Vec<u8> { fs::read(self.path.join(name)).unwrap() }
    fn unchanged(&self) {
        assert_eq!(self.read("knob.ctr"), b"017\n");
        assert_eq!(self.read("mixer.state"), SAVED);
    }
}
fn config() -> Config {
    Config { capacity: 4, namespace: [0x73; 16], absent_mode: 0o640,
        selection: Selection { selector: true, fx_time: true } }
}
fn frame(salt: u8) -> Frame {
    std::array::from_fn(|i| (i as u8).wrapping_mul(71).wrapping_add(salt))
}
fn baseline() -> BaselineReceipt {
    BaselineReceipt { key: Key { instance: [0x2468, 0x1357], run_id: 13,
        epoch: 17, producer_id: 19 }, frame: frame(29), kernel_sequence: 23 }
}
struct Barrier { entered: mpsc::SyncSender<()>, proceed: mpsc::Receiver<()> }
impl Barrier { fn hit(&self) { self.entered.send(()).unwrap(); self.proceed.recv().unwrap(); } }
fn barrier() -> (Barrier, mpsc::Receiver<()>, mpsc::SyncSender<()>) {
    let (entered, seen) = mpsc::sync_channel(1);
    let (release, proceed) = mpsc::sync_channel(1);
    (Barrier { entered, proceed }, seen, release)
}
#[derive(Clone, Copy)] enum BaselineReply { Ok, Err, Panic }
#[derive(Clone, Copy)] enum Reply { Commit, Unknown }
struct Counts {
    baselines: AtomicUsize,
    submitted: Mutex<Vec<Envelope>>,
    backend_drops: AtomicUsize,
    supervisor_drops: AtomicUsize,
}
struct Backend {
    counts: Arc<Counts>, baseline_reply: BaselineReply, reply: Reply,
    baseline_block: Option<Barrier>, submit_block: Option<Barrier>, sequence: u64,
}
impl MixerBackend for Backend {
    fn baseline(&mut self) -> Result<BaselineReceipt, String> {
        self.counts.baselines.fetch_add(1, Ordering::SeqCst);
        if let Some(block) = &self.baseline_block { block.hit(); }
        match self.baseline_reply {
            BaselineReply::Ok => Ok(baseline()),
            BaselineReply::Err => Err("synthetic startup baseline refusal".into()),
            BaselineReply::Panic => panic!("synthetic startup baseline unwind"),
        }
    }
    fn submit(&mut self, request: Envelope) -> BackendOutcome {
        self.counts.submitted.lock().unwrap().push(request);
        if let Some(block) = &self.submit_block { block.hit(); }
        match self.reply {
            Reply::Commit => {
                self.sequence += 1;
                BackendOutcome::Committed(FrameAck { request, kernel_sequence: self.sequence })
            }
            Reply::Unknown => BackendOutcome::Unknown,
        }
    }
}
impl Drop for Backend {
    fn drop(&mut self) { self.counts.backend_drops.fetch_add(1, Ordering::SeqCst); }
}
struct Supervisor(Arc<Counts>);
impl Drop for Supervisor {
    fn drop(&mut self) { self.0.supervisor_drops.fetch_add(1, Ordering::SeqCst); }
}
fn backend(reply: Reply, baseline_reply: BaselineReply) -> (Backend, Arc<Counts>) {
    let counts = Arc::new(Counts { baselines: AtomicUsize::new(0),
        submitted: Mutex::new(vec![]), backend_drops: AtomicUsize::new(0),
        supervisor_drops: AtomicUsize::new(0) });
    (Backend { counts: counts.clone(), baseline_reply, reply, baseline_block: None,
        submit_block: None, sequence: baseline().kernel_sequence }, counts)
}
fn retain(case: &Case, reply: Reply, br: BaselineReply)
    -> (OwnedStartup<Supervisor>, StartupHandle<Supervisor>, Arc<Counts>) {
    let (backend, counts) = backend(reply, br);
    let (startup, handle) = OwnedStartup::retain(Box::new(backend), case.file(), Some(Supervisor(counts.clone())));
    (startup, handle, counts)
}
fn baselines(counts: &Counts) -> usize { counts.baselines.load(Ordering::SeqCst) }
fn submitted(counts: &Counts) -> Vec<Envelope> { counts.submitted.lock().unwrap().clone() }
fn drops(counts: &Counts) -> (usize, usize) {
    (counts.backend_drops.load(Ordering::SeqCst), counts.supervisor_drops.load(Ordering::SeqCst))
}
fn unwind(f: impl FnOnce()) -> bool {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).is_err()
}
fn lifecycle(handle: &StartupHandle<Supervisor>) -> LifecycleSnapshot {
    handle.snapshot().lifecycle.expect("retained owner").expect("lifecycle diagnostic")
}
fn finish_joined(handle: &StartupHandle<Supervisor>) -> FinishResult {
    handle.finish()
}
fn start_ready(startup: &mut OwnedStartup<Supervisor>, handle: &StartupHandle<Supervisor>)
    -> Arc<dyn crate::owned_lifecycle::LifecycleControl> {
    match startup.start(config()) {
        Ok(control) => control,
        Err(error) => {
            let cleanup = handle.finish();
            panic!("startup failed after explicit cleanup: {error:?}; {cleanup:?}");
        }
    }
}
fn snapshot_thread(handle: StartupHandle<Supervisor>)
    -> (mpsc::Receiver<StartupSnapshot>, thread::JoinHandle<()>) {
    let (tx, rx) = mpsc::sync_channel(1);
    let job = thread::spawn(move || { tx.send(handle.snapshot()).unwrap(); });
    (rx, job)
}

#[test]
fn retention_and_finish_before_start_preserve_inputs_without_queries_or_join_claim() {
    let case = Case::new("retained");
    let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
    let before = handle.snapshot();
    assert_eq!(before.journal.phase, StartPhase::Retained);
    assert_eq!(before.supervisor_retained, true);
    assert_eq!(before.pending_pair_retained, false);
    assert_eq!(before.owner_retained, false);
    assert_eq!(before.fixed.journal.phase, InitPhase::Retained);
    assert_eq!(baselines(&counts), 0);
    assert_eq!(submitted(&counts), vec![]);
    assert_eq!(handle.finish(), FinishResult::NoWorkerRetained);
    assert_eq!(startup.start(config()).err(), Some(StartError::FinishRequested));
    let after = handle.snapshot();
    assert_eq!(after.journal.finish_requested, true);
    assert_eq!(after.lifecycle, None);
    assert_eq!(after.journal.channel_return, None);
    assert_eq!(baselines(&counts), 0);
    drop(startup); assert_eq!(drops(&counts), (0, 0));
    case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
}

#[test]
fn fixed_initialization_failures_and_unwind_keep_supervisor_without_a_worker_receipt() {
    for (n, br) in [BaselineReply::Ok, BaselineReply::Err, BaselineReply::Panic].into_iter().enumerate() {
        let case = Case::new(&format!("fixed-failure-{n}"));
        let (mut startup, handle, counts) = retain(&case, Reply::Commit, br);
        let mut options = config(); if n == 0 { options.capacity = 0; }
        let panicked = unwind(|| { let result = startup.start(options); assert_eq!(result.is_err(), true); });
        let state = handle.snapshot();
        let finish = handle.finish();
        let retry = startup.start(config());
        assert_eq!(panicked, n == 2);
        assert_eq!(state.pending_pair_retained, false);
        assert_eq!(state.owner_retained, false);
        assert_eq!(state.journal.channel_return, None);
        assert_eq!(state.lifecycle, None);
        assert_eq!(finish, FinishResult::NoWorkerRetained);
        assert_eq!(retry.is_err(), true);
        assert_eq!(baselines(&counts), usize::from(n != 0));
        assert_eq!(submitted(&counts), vec![]);
        if n == 1 {
            assert_eq!(state.fixed.journal.captured_baseline,
                Some(Err("synthetic startup baseline refusal".into())));
        }
        if n == 2 {
            assert_eq!(state.fixed.backend_poisoned, true);
            assert_eq!(state.fixed.journal.phase, InitPhase::BaselineArmed);
        }
        drop(startup); assert_eq!(drops(&counts), (0, 0));
        case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
    }
}

#[test]
fn actual_channel_spawn_error_arm_retains_inputs_and_never_fabricates_join() {
    let case = Case::new("spawn-error");
    let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
    startup.fail_spawn_for_test();
    let result = startup.start(config());
    let before = handle.snapshot();
    let finished = handle.finish();
    if let Ok(control) = &result { let _ = control.finish(); }
    assert_eq!(result.err(), Some(StartError::Channel(ChannelFailure::SpawnFailed)));
    assert_eq!(before.journal.channel_return, Some(ChannelReturn::Failed(ChannelFailure::SpawnFailed)));
    assert_eq!(before.route_retained, true);
    assert_eq!(before.pending_pair_retained, false);
    assert_eq!(before.owner_retained, false);
    assert_eq!(before.lifecycle, None);
    assert_eq!(finished, FinishResult::NoWorkerRetained);
    assert_eq!(handle.snapshot().lifecycle, None);
    assert_eq!(baselines(&counts), 1);
    assert_eq!(submitted(&counts), vec![]);
    assert_eq!(startup.start(config()).is_err(), true);
    drop(startup); assert_eq!(drops(&counts), (0, 0));
    case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
}

#[test]
fn armed_channel_unwind_is_unproven_without_a_captured_pair_or_return() {
    let case = Case::new("channel-armed");
    let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
    startup.set_hook(Box::new(|point| {
        if point == HookPoint::ChannelArmed { panic!("synthetic channel boundary gap"); }
    }));
    let panicked = unwind(|| { let _ = startup.start(config()); });
    let state = handle.snapshot();
    let finish = handle.finish();
    assert_eq!(panicked, true);
    assert_eq!(state.journal.phase, StartPhase::ChannelArmed);
    assert_eq!(state.journal.channel_return, None);
    assert_eq!(state.pending_pair_retained, false);
    assert_eq!(state.owner_retained, false);
    assert_eq!(finish, FinishResult::StartupUnproven);
    assert_eq!(handle.snapshot().lifecycle, None);
    assert_eq!(startup.start(config()).is_err(), true);
    assert_eq!(baselines(&counts), 1);
    assert_eq!(submitted(&counts), vec![]);
    drop(startup); assert_eq!(drops(&counts), (0, 0));
    case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
}

#[test]
fn published_pair_or_owner_survives_unwind_and_external_finish_performs_real_join() {
    for (n, point, phase, pending, owner) in [
        (0, HookPoint::PairRetained, StartPhase::PairRetained, true, false),
        (1, HookPoint::OwnerRetained, StartPhase::OwnerRetained, false, true),
    ] {
        let case = Case::new(&format!("published-panic-{n}"));
        let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
        startup.set_hook(Box::new(move |seen| if seen == point { panic!("synthetic published startup gap"); }));
        let panicked = unwind(|| { let _ = startup.start(config()); });
        let before = handle.snapshot();
        drop(startup);
        let finished = finish_joined(&handle);
        let after = lifecycle(&handle);
        let repeated = handle.finish();
        assert_eq!(panicked, true);
        assert_eq!(before.journal.phase, phase);
        assert_eq!(before.pending_pair_retained, pending);
        assert_eq!(before.owner_retained, owner);
        assert_eq!(before.journal.channel_return, Some(ChannelReturn::Succeeded));
        assert_eq!(finished, FinishResult::Lifecycle(FinishStatus::RecoveryRequired));
        assert_eq!(repeated, finished);
        assert_eq!(after.phase, LifecyclePhase::Retained);
        assert_eq!(after.joined_exit, Some(WorkerExit::Stopped));
        assert_eq!(after.joined.unwrap().mailbox.status.worker_exited, true);
        assert_eq!(baselines(&counts), 1);
        assert_eq!(submitted(&counts), vec![]);
        assert_eq!(drops(&counts), (0, 0));
        case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
    }
}

#[test]
fn publication_checkpoints_observe_resources_before_hooks_and_cleanup_always_joins() {
    for (n, point, phase, pending, owner) in [
        (0, HookPoint::PairRetained, StartPhase::PairRetained, true, false),
        (1, HookPoint::OwnerRetained, StartPhase::OwnerRetained, false, true),
    ] {
        let case = Case::new(&format!("publication-{n}"));
        let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
        let weak = Arc::downgrade(&handle.shared);
        let (tx, rx) = mpsc::sync_channel(1);
        startup.set_hook(Box::new(move |seen| if seen == point {
            let handle = StartupHandle { shared: weak.upgrade().unwrap() };
            tx.send(handle.snapshot()).unwrap();
        }));
        let result = startup.start(config());
        let observed = rx.recv_timeout(WAIT);
        let finished = finish_joined(&handle);
        if let Ok(control) = &result { let _ = control.finish(); }
        // These oracles run only after the actual unique worker has been joined.
        assert_eq!(result.is_ok(), true);
        assert_eq!(observed.is_ok(), true);
        assert_eq!(finished, FinishResult::Lifecycle(FinishStatus::RecoveryRequired));
        let observed = observed.unwrap();
        assert_eq!(observed.journal.phase, phase);
        assert_eq!(observed.pending_pair_retained, pending);
        assert_eq!(observed.owner_retained, owner);
        assert_eq!(observed.operation_active, true);
        assert_eq!(lifecycle(&handle).joined.unwrap().mailbox.status.worker_exited, true);
        drop(result); drop(startup);
        assert_eq!(drops(&counts), (0, 0));
        drop(handle); assert_eq!(drops(&counts), (1, 1));
    }
}

#[test]
fn ready_control_stays_unarmed_until_adoption_and_repeated_start_does_not_replace_it() {
    let case = Case::new("ready");
    let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
    let (tx, callback_seen) = mpsc::sync_channel(1);
    startup.set_sink_hook_for_test(Box::new(move |point| {
        if point == SinkHookPoint::AfterPairReturn { tx.send(()).unwrap(); }
    }));
    let control = start_ready(&mut startup, &handle);
    let port = control.port();
    let before = handle.snapshot();
    let initial_key = port.current_key();
    let retry = startup.start(config());
    let b = port.refresh_baseline().unwrap();
    let held = port.confirm_adoption(b, false);
    let held_key = port.current_key();
    port.confirm_adoption(b, true).unwrap();
    let request = port.enqueue_for_key(b.key, frame(47)).unwrap();
    callback_seen.recv_timeout(WAIT).unwrap();
    let finished = finish_joined(&handle);
    let _ = control.finish();
    let after = lifecycle(&handle);
    assert_eq!(before.journal.phase, StartPhase::Ready);
    assert_eq!(before.pending_pair_retained, false);
    assert_eq!(before.owner_retained, true);
    assert_eq!(initial_key, None);
    assert_eq!(retry.err(), Some(StartError::AlreadyAttempted));
    assert_eq!(b, baseline());
    assert_eq!(held.is_err(), true);
    assert_eq!(held_key, None);
    assert_eq!(finished, FinishResult::Lifecycle(FinishStatus::RecoveryRequired));
    let joined = after.joined.unwrap();
    assert_eq!(joined.route.producer.status.last_ack, Some(AckCheckpoint {
        ack: FrameAck { request, kernel_sequence: b.kernel_sequence + 1 },
        settings: AckSettings::ReturnedOk }));
    assert_eq!(joined.mailbox.status.last.unwrap().outcome,
        AttemptOutcome::Committed { settings_written: true });
    assert_eq!(baselines(&counts), 1);
    assert_eq!(submitted(&counts), vec![request]);
    assert_eq!(case.read("knob.ctr"), frame(47)[34].to_string().as_bytes());
    drop(port); drop(control); drop(startup);
    assert_eq!(drops(&counts), (0, 0));
    drop(handle); assert_eq!(drops(&counts), (1, 1));
}

#[test]
fn busy_finish_during_blocked_baseline_is_not_cancellation_and_snapshot_does_not_wait() {
    let case = Case::new("blocked-baseline");
    let (block, entered, release) = barrier();
    let (mut backend, counts) = backend(Reply::Commit, BaselineReply::Ok);
    backend.baseline_block = Some(block);
    let (mut startup, handle) = OwnedStartup::retain(Box::new(backend), case.file(), Some(Supervisor(counts.clone())));
    let job = thread::spawn(move || { let result = startup.start(config()); (startup, result) });
    entered.recv_timeout(WAIT).unwrap();
    let busy = handle.finish();
    let (rx, observer) = snapshot_thread(handle.clone());
    let observed = rx.recv_timeout(WAIT);
    release.send(()).unwrap();
    let (startup, result) = job.join().unwrap(); observer.join().unwrap();
    let finished = finish_joined(&handle);
    if let Ok(control) = &result { let _ = control.finish(); }
    assert_eq!(busy, FinishResult::InProgress);
    assert_eq!(observed.is_ok(), true, "snapshot must finish before baseline release");
    let observed = observed.unwrap();
    assert_eq!(observed.operation_active, true);
    assert_eq!(observed.journal.finish_requested, false);
    assert_eq!(observed.fixed.journal.phase, InitPhase::BaselineArmed);
    assert_eq!(result.is_ok(), true);
    assert_eq!(finished, FinishResult::Lifecycle(FinishStatus::RecoveryRequired));
    assert_eq!(lifecycle(&handle).joined.unwrap().mailbox.status.worker_exited, true);
    assert_eq!(baselines(&counts), 1);
    assert_eq!(submitted(&counts), vec![]);
    drop(result); drop(startup); assert_eq!(drops(&counts), (0, 0));
    case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
}

#[test]
fn active_start_lease_retains_published_pair_while_busy_finish_leaves_start_running() {
    let case = Case::new("blocked-pair");
    let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
    let (block, entered, release) = barrier();
    startup.set_hook(Box::new(move |point| if point == HookPoint::PairRetained { block.hit(); }));
    let job = thread::spawn(move || { let result = startup.start(config()); (startup, result) });
    entered.recv_timeout(WAIT).unwrap();
    let busy = handle.finish();
    let (rx, observer) = snapshot_thread(handle.clone());
    let observed = rx.recv_timeout(WAIT);
    release.send(()).unwrap();
    let (startup, result) = job.join().unwrap(); observer.join().unwrap();
    let finished = finish_joined(&handle);
    if let Ok(control) = &result { let _ = control.finish(); }
    assert_eq!(busy, FinishResult::InProgress);
    assert_eq!(observed.is_ok(), true, "snapshot must finish before pair hook release");
    let observed = observed.unwrap();
    assert_eq!(observed.journal.phase, StartPhase::PairRetained);
    assert_eq!(observed.pending_pair_retained, true);
    assert_eq!(observed.owner_retained, false);
    assert_eq!(observed.journal.finish_requested, false);
    assert_eq!(result.is_ok(), true);
    assert_eq!(finished, FinishResult::Lifecycle(FinishStatus::RecoveryRequired));
    assert_eq!(lifecycle(&handle).joined.unwrap().mailbox.status.worker_exited, true);
    assert_eq!(baselines(&counts), 1);
    drop(result); drop(startup); assert_eq!(drops(&counts), (0, 0));
    case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
}

#[test]
fn blocked_backend_join_keeps_snapshot_available_and_retains_exact_unresolved_request() {
    let case = Case::new("blocked-join");
    let (block, entered, release) = barrier();
    let (mut backend, counts) = backend(Reply::Unknown, BaselineReply::Ok);
    backend.submit_block = Some(block);
    let (mut startup, handle) = OwnedStartup::retain(Box::new(backend), case.file(), Some(Supervisor(counts.clone())));
    let control = start_ready(&mut startup, &handle); let port = control.port();
    let b = port.refresh_baseline().unwrap(); port.confirm_adoption(b, true).unwrap();
    let request = port.enqueue_for_key(b.key, frame(61)).unwrap();
    entered.recv_timeout(WAIT).unwrap();
    let other = handle.clone();
    let job = thread::spawn(move || other.finish());
    let deadline = Instant::now() + WAIT;
    let mut joining = false;
    while Instant::now() < deadline {
        if handle.snapshot().operation_active { joining = true; break; }
        thread::yield_now();
    }
    let second = if joining { handle.finish() } else { FinishResult::StartupUnproven };
    let (rx, observer) = snapshot_thread(handle.clone());
    let observed = rx.recv_timeout(WAIT);
    release.send(()).unwrap();
    let finished = job.join().unwrap(); observer.join().unwrap();
    let _ = control.finish();
    let after = lifecycle(&handle);
    assert_eq!(joining, true);
    assert_eq!(second, FinishResult::InProgress);
    assert_eq!(observed.is_ok(), true, "snapshot must finish before blocked backend release");
    assert_eq!(observed.unwrap().operation_active, true);
    assert_eq!(finished, FinishResult::Lifecycle(FinishStatus::RecoveryRequired));
    assert_eq!(after.phase, LifecyclePhase::Retained);
    assert_eq!(after.joined_exit, Some(WorkerExit::Stopped));
    let pending = after.joined.unwrap().route.producer.status.unresolved.unwrap();
    assert_eq!(pending.request, request);
    assert_eq!(pending.uncertain, true);
    assert_eq!(submitted(&counts), vec![request]);
    drop(port); drop(control); drop(startup);
    assert_eq!(drops(&counts), (0, 0));
    case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
}

#[test]
fn callback_unwind_joins_actual_worker_and_preserves_known_ack_separately_from_pair_commit() {
    let case = Case::new("callback-unwind");
    let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
    let (tx, entered) = mpsc::sync_channel(1);
    startup.set_sink_hook_for_test(Box::new(move |point| {
        if point == SinkHookPoint::AfterPairReturn {
            tx.send(()).unwrap(); panic!("synthetic startup callback gap");
        }
    }));
    let control = start_ready(&mut startup, &handle); let port = control.port();
    let b = port.refresh_baseline().unwrap(); port.confirm_adoption(b, true).unwrap();
    let request = port.enqueue_for_key(b.key, frame(67)).unwrap();
    entered.recv_timeout(WAIT).unwrap();
    let finished = finish_joined(&handle);
    let _ = control.finish();
    let state = handle.snapshot();
    let after = state.lifecycle.unwrap().unwrap();
    assert_eq!(finished, FinishResult::Lifecycle(FinishStatus::RecoveryRequired));
    assert_eq!(after.joined_exit, Some(WorkerExit::Panicked));
    assert_eq!(after.joined.unwrap().route.producer.status.last_ack, Some(AckCheckpoint {
        ack: FrameAck { request, kernel_sequence: b.kernel_sequence + 1 },
        settings: AckSettings::CallbackArmed }));
    let settings = state.fixed.settings.unwrap();
    assert_eq!(settings.journal.last_accepted_attempt.unwrap().phase, AttemptPhase::PairArmed);
    assert_eq!(settings.pair.last.unwrap().result, TransactionResult::Committed);
    assert_eq!(port.refresh_baseline().is_err(), true);
    assert_eq!(submitted(&counts), vec![request]);
    assert_eq!(baselines(&counts), 1);
    drop(port); drop(control); drop(startup);
    assert_eq!(drops(&counts), (0, 0));
    drop(handle); assert_eq!(drops(&counts), (1, 1));
}

#[test]
fn external_handle_survives_control_drop_and_final_release_has_no_ownership_cycle() {
    let case = Case::new("handle-only");
    let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
    let control = start_ready(&mut startup, &handle);
    let another_control = control.clone();
    let directory = handle.original_directory_for_test();
    let weak_directory = Arc::downgrade(&directory); drop(directory);
    drop(another_control); drop(control); drop(startup);
    let before = handle.snapshot();
    let retained_drops = drops(&counts);
    let directory_retained = weak_directory.upgrade().is_some();
    let finished = finish_joined(&handle);
    let joined = lifecycle(&handle);
    assert_eq!(retained_drops, (0, 0));
    assert_eq!(directory_retained, true);
    assert_eq!(before.owner_retained, true);
    assert_eq!(before.lifecycle.unwrap().unwrap().phase, LifecyclePhase::Running);
    assert_eq!(finished, FinishResult::Lifecycle(FinishStatus::RecoveryRequired));
    assert_eq!(joined.stop, StopStatus::ReturnedOk { discarded: 0, in_flight: false });
    assert_eq!(joined.joined.unwrap().mailbox.status.worker_exited, true);
    assert_eq!(drops(&counts), (0, 0));
    case.unchanged(); drop(handle);
    assert_eq!(drops(&counts), (1, 1));
    assert_eq!(weak_directory.upgrade().is_none(), true);
}

#[test]
fn poisoned_startup_custody_before_threads_refuses_work_and_preserves_inputs() {
    for products in [false, true] {
        let case = Case::new(if products { "products-poison" } else { "journal-poison" });
        let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
        if products { handle.poison_products_for_test(); } else { handle.poison_journal_for_test(); }
        let result = startup.start(config());
        let finished = handle.finish();
        let state = handle.snapshot();
        assert_eq!(result.is_err(), true);
        assert_eq!(matches!(finished, FinishResult::Poisoned(_)), true);
        assert_eq!(if products { state.products_poisoned } else { state.journal.poisoned }, true);
        assert_eq!(state.supervisor_retained, true);
        assert_eq!(state.pending_pair_retained, false);
        assert_eq!(state.owner_retained, false);
        assert_eq!(state.lifecycle, None);
        assert_eq!(baselines(&counts), 0);
        assert_eq!(submitted(&counts), vec![]);
        drop(startup); assert_eq!(drops(&counts), (0, 0));
        case.unchanged(); drop(handle); assert_eq!(drops(&counts), (1, 1));
    }
}

#[test]
fn returned_control_alone_keeps_backend_supervisor_and_duplicated_directory_through_join() {
    let case = Case::new("control-only");
    let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
    let (tx, callback_seen) = mpsc::sync_channel(1);
    startup.set_sink_hook_for_test(Box::new(move |point| {
        if point == SinkHookPoint::AfterPairReturn { tx.send(()).unwrap(); }
    }));
    let control = start_ready(&mut startup, &handle);
    let weak_owner = {
        let products = handle.shared.products.lock().unwrap();
        Arc::downgrade(products.owner.as_ref().unwrap())
    };
    let original = handle.original_directory_for_test();
    let weak_original = Arc::downgrade(&original); drop(original);
    drop(startup); drop(handle);
    let held_drops = drops(&counts);
    let original_released = weak_original.upgrade().is_none();
    // The original File Arc is gone. The actual sink retains its duplicated
    // directory descriptor, which must still work after this private rename.
    let moved = case.path.with_extension("retained-directory");
    let exercise = (|| -> Result<Envelope, String> {
        fs::rename(&case.path, &moved).map_err(|error| format!("{error:?}"))?;
        let port = control.port();
        let b = port.refresh_baseline().map_err(|error| format!("{error:?}"))?;
        port.confirm_adoption(b, true).map_err(|error| format!("{error:?}"))?;
        let request = port.enqueue_for_key(b.key, frame(83))
            .map_err(|error| format!("{error:?}"))?;
        callback_seen.recv_timeout(WAIT).map_err(|error| format!("{error:?}"))?;
        Ok(request)
    })();
    // Even an exercise failure takes this explicit real-worker cleanup path.
    let finished = control.finish();
    let joined = control.snapshot();
    let moved_knob = fs::read(moved.join("knob.ctr"));
    let old_path_absent = !case.path.exists();
    let after_join_drops = drops(&counts);
    drop(control);
    let final_drops = drops(&counts);
    let owner_released = weak_owner.upgrade().is_none();

    assert_eq!(held_drops, (0, 0));
    assert_eq!(original_released, true);
    assert_eq!(exercise.is_ok(), true);
    assert_eq!(finished, FinishStatus::RecoveryRequired);
    let joined = joined.unwrap();
    assert_eq!(joined.phase, LifecyclePhase::Retained);
    assert_eq!(joined.joined_exit, Some(WorkerExit::Stopped));
    let accounting = joined.joined.unwrap();
    assert_eq!(accounting.mailbox.status.worker_exited, true);
    let request = exercise.unwrap();
    assert_eq!(accounting.route.producer.status.last_ack, Some(AckCheckpoint {
        ack: FrameAck { request, kernel_sequence: baseline().kernel_sequence + 1 },
        settings: AckSettings::ReturnedOk }));
    assert_eq!(moved_knob.unwrap(), frame(83)[34].to_string().as_bytes());
    assert_eq!(old_path_absent, true);
    assert_eq!(submitted(&counts), vec![request]);
    assert_eq!(baselines(&counts), 1);
    assert_eq!(after_join_drops, (0, 0));
    assert_eq!(final_drops, (1, 1));
    assert_eq!(owner_released, true);
}

#[test]
fn poisoned_ready_startup_refuses_finish_but_retained_control_can_join_and_keep_diagnostics() {
    for products in [false, true] {
        let case = Case::new(if products { "ready-products-poison" } else { "ready-journal-poison" });
        let (mut startup, handle, counts) = retain(&case, Reply::Commit, BaselineReply::Ok);
        let control = start_ready(&mut startup, &handle);
        let weak_owner = {
            let retained = handle.shared.products.lock().unwrap();
            Arc::downgrade(retained.owner.as_ref().unwrap())
        };
        if products { handle.poison_products_for_test(); } else { handle.poison_journal_for_test(); }
        let refused = handle.finish();
        // This independent already-returned capability remains governed by the
        // frozen lifecycle; startup poison is not recovered or cleared.
        let joined_result = control.finish();
        let after = handle.snapshot();
        let repeated = handle.finish();
        drop(control); drop(startup);
        let handle_only_drops = drops(&counts);
        drop(handle);
        let final_drops = drops(&counts);
        let owner_released = weak_owner.upgrade().is_none();

        let expected = FinishResult::Poisoned(if products {
            PoisonTarget::Products
        } else { PoisonTarget::Journal });
        assert_eq!(refused, expected);
        assert_eq!(repeated, refused);
        assert_eq!(joined_result, FinishStatus::RecoveryRequired);
        assert_eq!(after.journal.phase, StartPhase::Ready);
        assert_eq!(if products { after.products_poisoned } else { after.journal.poisoned }, true);
        assert_eq!(after.owner_retained, true);
        assert_eq!(after.pending_pair_retained, false);
        assert_eq!(after.operation_active, false);
        let joined = after.lifecycle.unwrap().unwrap();
        assert_eq!(joined.phase, LifecyclePhase::Retained);
        assert_eq!(joined.joined_exit, Some(WorkerExit::Stopped));
        assert_eq!(joined.joined.unwrap().mailbox.status.worker_exited, true);
        assert_eq!(baselines(&counts), 1);
        assert_eq!(submitted(&counts), vec![]);
        assert_eq!(handle_only_drops, (0, 0));
        assert_eq!(final_drops, (1, 1));
        assert_eq!(owner_released, true);
        case.unchanged();
    }
}

#[test]
fn journal_poison_during_real_join_retains_observed_finish_without_claiming_success() {
    let case = Case::new("join-completion-poison");
    let (block, entered, release) = barrier();
    let (mut backend, counts) = backend(Reply::Unknown, BaselineReply::Ok);
    backend.submit_block = Some(block);
    let (mut startup, handle) = OwnedStartup::retain(
        Box::new(backend), case.file(), Some(Supervisor(counts.clone())));
    let control = start_ready(&mut startup, &handle);
    let port = control.port();
    let b = port.refresh_baseline().unwrap(); port.confirm_adoption(b, true).unwrap();
    let request = port.enqueue_for_key(b.key, frame(89)).unwrap();
    entered.recv_timeout(WAIT).unwrap();
    let other = handle.clone();
    let job = thread::spawn(move || other.finish());
    let deadline = Instant::now() + WAIT;
    let mut saw_joining = false;
    while Instant::now() < deadline {
        if handle.snapshot().lifecycle.as_ref().is_some_and(|state| {
            state.as_ref().is_ok_and(|state| state.phase == LifecyclePhase::Joining)
        }) {
            saw_joining = true;
            break;
        }
        thread::yield_now();
    }
    handle.poison_journal_for_test();
    release.send(()).unwrap();
    let finished = job.join().unwrap();
    // Retain an independent explicit completion path even if the observed
    // startup result is a refusal; it never clears the startup poison.
    let independently_finished = control.finish();
    let after = handle.snapshot();
    let repeated = handle.finish();
    drop(port); drop(control); drop(startup);
    let handle_only_drops = drops(&counts);
    drop(handle);
    let final_drops = drops(&counts);

    assert_eq!(saw_joining, true);
    assert_eq!(finished, FinishResult::Poisoned(PoisonTarget::Journal));
    assert_eq!(independently_finished, FinishStatus::RecoveryRequired);
    assert_eq!(after.journal.poisoned, true);
    assert_eq!(after.journal.last_finish,
        Some(FinishResult::Lifecycle(FinishStatus::RecoveryRequired)));
    assert_eq!(after.operation_active, false);
    assert_eq!(repeated, FinishResult::Poisoned(PoisonTarget::Journal));
    let joined = after.lifecycle.unwrap().unwrap();
    assert_eq!(joined.phase, LifecyclePhase::Retained);
    assert_eq!(joined.joined_exit, Some(WorkerExit::Stopped));
    let accounting = joined.joined.unwrap();
    assert_eq!(accounting.mailbox.status.worker_exited, true);
    assert_eq!(accounting.route.producer.status.last_ack, None);
    let unresolved = accounting.route.producer.status.unresolved.unwrap();
    assert_eq!(unresolved.request, request);
    assert_eq!(unresolved.uncertain, true);
    assert_eq!(submitted(&counts), vec![request]);
    assert_eq!(baselines(&counts), 1);
    assert_eq!(handle_only_drops, (0, 0));
    assert_eq!(final_drops, (1, 1));
    case.unchanged();
}
