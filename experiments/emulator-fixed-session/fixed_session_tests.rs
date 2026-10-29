// SPDX-License-Identifier: GPL-3.0-only
// Inactive retained-route composition tests. Real isolated host files and actual
// frozen route/producer/worker/lifecycle types; synthetic backend only.
// The runner supplies a fresh private root. Fixtures are deliberately retained.
use super::*;
use crate::mixer_producer::{AckCheckpoint, AckSettings, BaselineReceipt, Envelope,
    Frame, FrameAck, Key};
use crate::mixer_route::{BackendOutcome, DispatchResult, MixerBackend, MixerRoute};
use crate::mixer_transport::{self, AttemptOutcome, WorkerExit};
use crate::owned_lifecycle::{FinishStatus, OwnedLifecycle};
use crate::settings_mapper::Selection;
use crate::settings_pair::TransactionResult;
use crate::settings_sink::{AttemptPhase, HookPoint as SinkHookPoint};
use std::fs::{self, File};
use std::os::unix::fs::{MetadataExt, PermissionsExt};
use std::path::PathBuf;
use std::sync::{Arc, Mutex, mpsc};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread;
use std::time::Duration;

static NEXT_CASE: AtomicUsize = AtomicUsize::new(1);
const WAIT: Duration = Duration::from_secs(10);
const SELECT: Selection = Selection { selector: true, fx_time: true };
const SAVED: &[u8] = b"{\"10\": 99, \"22\": 160, \"26\": 7, \"27\": 8}";

struct Case { path: PathBuf }
impl Case {
    fn new(label: &str) -> Self {
        let root = PathBuf::from(std::env::var_os("AZ_FIXED_SESSION_TEST_ROOT")
            .expect("runner must supply an explicit isolated fixed-session root"));
        assert_eq!(root.is_absolute(), true);
        let meta = fs::symlink_metadata(&root).unwrap();
        assert_eq!(meta.is_dir() && !meta.file_type().is_symlink(), true);
        let path = root.join(format!("{}-{}-{}", label, std::process::id(),
            NEXT_CASE.fetch_add(1, Ordering::Relaxed)));
        fs::create_dir(&path).unwrap();
        fs::set_permissions(&path, fs::Permissions::from_mode(0o700)).unwrap();
        Self { path }
    }
    fn original(&self) {
        self.write("knob.ctr", b"017\n");
        self.write("mixer.state", SAVED);
    }
    fn write(&self, name: &str, bytes: &[u8]) {
        fs::write(self.path.join(name), bytes).unwrap();
        fs::set_permissions(self.path.join(name), fs::Permissions::from_mode(0o600)).unwrap();
    }
    fn read(&self, name: &str) -> Vec<u8> { fs::read(self.path.join(name)).unwrap() }
    fn file(&self) -> File { File::open(&self.path).unwrap() }
    fn identity(&self) -> (u64, u64) {
        let m = fs::metadata(&self.path).unwrap(); (m.dev(), m.ino())
    }
    fn unchanged(&self) {
        assert_eq!(self.read("knob.ctr"), b"017\n");
        assert_eq!(self.read("mixer.state"), SAVED);
    }
}

fn baseline() -> BaselineReceipt {
    BaselineReceipt { key: Key { instance: [0x1234, 0x5678], run_id: 7,
        epoch: 11, producer_id: 13 }, frame: frame(29), kernel_sequence: 23 }
}
fn frame(salt: u8) -> Frame {
    std::array::from_fn(|i| (i as u8).wrapping_mul(73).wrapping_add(salt))
}
#[derive(Clone, Copy)] enum BaselineReply { Ok, Err, Panic }
#[derive(Clone, Copy)] enum Reply { Commit, Reject, Unknown, Panic }
struct Barrier { entered: mpsc::SyncSender<()>, proceed: mpsc::Receiver<()> }
impl Barrier {
    fn hit(&self) { self.entered.send(()).unwrap(); self.proceed.recv().unwrap(); }
}
fn barrier() -> (Barrier, mpsc::Receiver<()>, mpsc::SyncSender<()>) {
    let (entered, seen) = mpsc::sync_channel(1);
    let (release, proceed) = mpsc::sync_channel(1);
    (Barrier { entered, proceed }, seen, release)
}
struct Counts {
    baseline: Mutex<BaselineReceipt>,
    baseline_calls: AtomicUsize,
    submits: Mutex<Vec<Envelope>>,
    drops: AtomicUsize,
}
struct Backend {
    counts: Arc<Counts>, baseline_reply: BaselineReply, reply: Reply,
    sequence: u64, baseline_block: Option<Barrier>, submit_block: Option<Barrier>,
}
impl MixerBackend for Backend {
    fn baseline(&mut self) -> Result<BaselineReceipt, String> {
        self.counts.baseline_calls.fetch_add(1, Ordering::SeqCst);
        if let Some(block) = &self.baseline_block { block.hit(); }
        match self.baseline_reply {
            BaselineReply::Ok => Ok(*self.counts.baseline.lock().unwrap()),
            BaselineReply::Err => Err("synthetic baseline refusal".into()),
            BaselineReply::Panic => panic!("synthetic baseline unwind"),
        }
    }
    fn submit(&mut self, request: Envelope) -> BackendOutcome {
        self.counts.submits.lock().unwrap().push(request);
        if let Some(block) = &self.submit_block { block.hit(); }
        match self.reply {
            Reply::Commit => {
                self.sequence += 1;
                BackendOutcome::Committed(FrameAck { request, kernel_sequence: self.sequence })
            }
            Reply::Reject => BackendOutcome::Rejected { request, reason: 31 },
            Reply::Unknown => BackendOutcome::Unknown,
            Reply::Panic => panic!("synthetic native submit unwind"),
        }
    }
}
impl Drop for Backend {
    fn drop(&mut self) { self.counts.drops.fetch_add(1, Ordering::SeqCst); }
}
fn backend(b: BaselineReceipt, baseline_reply: BaselineReply, reply: Reply)
    -> (Backend, Arc<Counts>) {
    let counts = Arc::new(Counts { baseline: Mutex::new(b),
        baseline_calls: AtomicUsize::new(0), submits: Mutex::new(vec![]),
        drops: AtomicUsize::new(0) });
    (Backend { counts: counts.clone(), baseline_reply, reply,
        sequence: b.kernel_sequence, baseline_block: None, submit_block: None }, counts)
}
fn retain(case: &Case, b: BaselineReceipt, br: BaselineReply, reply: Reply)
    -> (FixedSession, EvidenceHandle, Arc<Counts>) {
    let (backend, counts) = backend(b, br, reply);
    let (session, evidence) = FixedSession::retain(Box::new(backend), case.file());
    (session, evidence, counts)
}
fn initialize(session: &mut FixedSession) -> Arc<MixerRoute> {
    session.initialize(4, [0x71; 16], 0o640, SELECT).unwrap()
}
fn arm(route: &MixerRoute) -> BaselineReceipt {
    let b = route.refresh_baseline().unwrap();
    route.confirm_adoption(b, true).unwrap(); b
}
fn calls(counts: &Counts) -> usize { counts.baseline_calls.load(Ordering::SeqCst) }
fn drops(counts: &Counts) -> usize { counts.drops.load(Ordering::SeqCst) }
fn submitted(counts: &Counts) -> Vec<Envelope> { counts.submits.lock().unwrap().clone() }
fn unwind(f: impl FnOnce()) -> bool {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).is_err()
}
fn changed_backend(counts: &Counts) {
    let mut b = counts.baseline.lock().unwrap();
    b.key.epoch += 1; b.frame = frame(199); b.kernel_sequence += 10;
}
fn snapshot_thread(evidence: EvidenceHandle)
    -> (mpsc::Receiver<Evidence>, thread::JoinHandle<()>) {
    let (tx, rx) = mpsc::sync_channel(1);
    let observer = thread::spawn(move || { tx.send(evidence.snapshot()).unwrap(); });
    (rx, observer)
}

#[test]
fn retain_has_no_external_calls_and_preserves_actual_directory() {
    let case = Case::new("retain"); case.original();
    let (session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let state = evidence.snapshot();
    assert_eq!(calls(&counts), 0);
    assert_eq!(submitted(&counts), vec![]);
    assert_eq!(state.journal.phase, InitPhase::Retained);
    assert_eq!(state.journal.captured_baseline, None);
    assert_eq!(state.settings_retained, false);
    assert_eq!(state.route_retained, false);
    assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
    case.unchanged(); drop(session);
    assert_eq!(drops(&counts), 0);
    assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
    drop(evidence); assert_eq!(drops(&counts), 1);
}

#[test]
fn captured_full_baseline_binds_sink_and_requires_exact_explicit_adoption() {
    let case = Case::new("capture"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session); let before = evidence.snapshot();
    assert_eq!(before.journal.captured_baseline, Some(Ok(baseline())));
    assert_eq!(before.settings.as_ref().unwrap().journal.baseline, baseline());
    assert_eq!(before.settings.as_ref().unwrap().pair.baseline, baseline());
    assert_eq!(before.settings.as_ref().unwrap().journal.selection, SELECT);
    assert_eq!(before.journal.baseline_served, false);
    assert_eq!(route.current_key(), None);
    assert_eq!(route.staged_baseline(), None);
    assert_eq!(route.create_for_key(baseline().key, frame(41)).is_err(), true);
    let b = route.refresh_baseline().unwrap();
    assert_eq!(b, baseline());
    assert_eq!(route.staged_baseline(), Some(b));
    assert_eq!(route.confirm_adoption(b, false).is_err(), true);
    assert_eq!(route.current_key(), None);
    assert_eq!(submitted(&counts), vec![]);
    case.unchanged();
    route.confirm_adoption(b, true).unwrap();
    assert_eq!(route.current_key(), Some(b.key));
    let request = route.create_for_key(b.key, frame(41)).unwrap();
    assert_eq!(route.dispatch(request).unwrap(), DispatchResult::Committed { settings_written: true });
    assert_eq!(case.read("knob.ctr"), frame(41)[34].to_string().as_bytes());
    assert_eq!(calls(&counts), 1);
}

#[test]
fn changed_backend_receipt_cannot_replace_constructor_captured_grant() {
    let case = Case::new("drift"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session);
    changed_backend(&counts);
    let supplied = route.refresh_baseline().unwrap();
    assert_eq!(supplied, baseline());
    assert_ne!(supplied, *counts.baseline.lock().unwrap());
    assert_eq!(calls(&counts), 1);
    assert_eq!(evidence.snapshot().settings.unwrap().journal.baseline, supplied);
    assert_eq!(evidence.snapshot().journal.baseline_served, true);
    route.confirm_adoption(supplied, true).unwrap();
    let request = route.create_for_key(supplied.key, frame(47)).unwrap();
    assert_eq!(route.dispatch(request).unwrap(), DispatchResult::Committed { settings_written: true });
    assert_eq!(submitted(&counts), vec![request]);
}

#[test]
fn mapping_failure_cannot_rearm_newer_epoch_or_submit_a_repair_frame() {
    let case = Case::new("failed-rearm"); case.original(); case.write("mixer.state", b"invalid");
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session); let b = arm(&route);
    let request = route.create_for_key(b.key, frame(51)).unwrap();
    assert_eq!(matches!(route.dispatch(request).unwrap(), DispatchResult::CommittedSettingsFailed(_)), true);
    let failed = evidence.snapshot();
    changed_backend(&counts);
    assert_eq!(route.refresh_baseline().is_err(), true);
    let newer = *counts.baseline.lock().unwrap();
    assert_eq!(route.confirm_adoption(newer, true).is_err(), true);
    assert_eq!(route.create_for_key(newer.key, frame(53)).is_err(), true);
    assert_eq!(calls(&counts), 1);
    assert_eq!(submitted(&counts), vec![request]);
    let after = evidence.snapshot();
    assert_eq!(after.journal.first_refusal, Some(Refusal {
        operation: RefusalOperation::Baseline, error: InitError::BaselineAlreadyServed, request: None }));
    assert_eq!(after.settings, failed.settings);
    assert_eq!(after.journal.captured_baseline, failed.journal.captured_baseline);
    assert_eq!(case.read("mixer.state"), b"invalid");
    let known = FrameAck { request, kernel_sequence: b.kernel_sequence + 1 };
    let status = route.diagnostic_snapshot().producer.status;
    assert_eq!(status.last_ack, Some(AckCheckpoint { ack: known, settings: AckSettings::ReturnedErr }));
    assert_eq!(status.unresolved, None);
}

#[test]
fn one_grant_stays_consumed_after_rejection_close_cancel_or_wrong_adoption() {
    for mode in 0..4 {
        let case = Case::new(&format!("grant-{mode}")); case.original();
        let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Reject);
        let route = initialize(&mut session); let b = route.refresh_baseline().unwrap();
        if mode < 2 {
            route.confirm_adoption(b, true).unwrap();
            if mode == 0 {
                let request = route.create_for_key(b.key, frame(59)).unwrap();
                assert_eq!(route.dispatch(request).unwrap(), DispatchResult::Rejected { reason: 31 });
            } else { route.close().unwrap(); }
        } else if mode == 2 { route.close().unwrap(); }
        else {
            let mut wrong = b; wrong.frame[151] ^= 1;
            assert_eq!(route.confirm_adoption(wrong, true).is_err(), true);
        }
        changed_backend(&counts);
        assert_eq!(route.refresh_baseline().is_err(), true);
        assert_eq!(route.confirm_adoption(b, true).is_err(), true);
        assert_eq!(route.create_for_key(b.key, frame(61)).is_err(), true);
        assert_eq!(evidence.snapshot().journal.baseline_served, true);
        assert_eq!(calls(&counts), 1);
        assert_eq!(submitted(&counts).len(), usize::from(mode == 0));
        case.unchanged();
    }
}

#[test]
fn invalid_preflight_configuration_is_terminal_before_query_or_file_io() {
    for (n, capacity, namespace, mode) in [
        (0, 0, [0x71; 16], 0o640), (1, crate::mixer_producer::MAX_QUEUE + 1, [0x71; 16], 0o640),
        (2, 4, [0; 16], 0o640), (3, 4, [0x71; 16], 0o1640),
    ] {
        let case = Case::new(&format!("config-{n}")); case.original();
        let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
        assert_eq!(session.initialize(capacity, namespace, mode, SELECT).is_err(), true);
        let failed = evidence.snapshot();
        assert_eq!(failed.journal.terminal.is_some(), true);
        assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
        assert_eq!(calls(&counts), 0);
        assert_eq!(submitted(&counts), vec![]);
        assert_eq!(evidence.snapshot().journal.terminal, failed.journal.terminal);
        assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
        assert_eq!(drops(&counts), 0); case.unchanged();
    }
}

#[test]
fn baseline_error_keeps_exact_response_real_inputs_and_never_requeries() {
    let case = Case::new("baseline-error"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Err, Reply::Commit);
    assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
    let failed = evidence.snapshot();
    assert_eq!(failed.journal.captured_baseline, Some(Err("synthetic baseline refusal".into())));
    assert_eq!(failed.settings_retained, false);
    assert_eq!(failed.route_retained, false);
    assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
    assert_eq!(calls(&counts), 1);
    assert_eq!(evidence.snapshot().journal.terminal, failed.journal.terminal);
    assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
    drop(session); assert_eq!(drops(&counts), 0);
    case.unchanged(); drop(evidence); assert_eq!(drops(&counts), 1);
}

#[test]
fn baseline_unwind_keeps_armed_phase_poison_and_actual_backend_custody() {
    let case = Case::new("baseline-panic"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Panic, Reply::Commit);
    assert_eq!(unwind(|| { let _ = session.initialize(4, [0x71; 16], 0o640, SELECT); }), true);
    let state = evidence.snapshot();
    assert_eq!(state.journal.phase, InitPhase::BaselineArmed);
    assert_eq!(state.journal.captured_baseline, None);
    assert_eq!(state.backend_poisoned, true);
    assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
    assert_eq!(calls(&counts), 1);
    assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
    drop(session); assert_eq!(drops(&counts), 0);
    case.unchanged(); drop(evidence); assert_eq!(drops(&counts), 1);
}

#[test]
fn malformed_baselines_and_attach_failure_preserve_captured_receipt_and_inputs() {
    for mode in 0..3 {
        let case = Case::new(&format!("attach-failure-{mode}")); case.original();
        let mut b = baseline();
        if mode == 0 { b.key.instance = [0, 0]; }
        if mode == 1 { b.kernel_sequence = u64::MAX; }
        if mode == 2 {
            fs::remove_file(case.path.join("mixer.state")).unwrap();
            fs::create_dir(case.path.join("mixer.state")).unwrap();
        }
        let (mut session, evidence, counts) = retain(&case, b, BaselineReply::Ok, Reply::Commit);
        assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
        let failed = evidence.snapshot();
        assert_eq!(failed.journal.captured_baseline, Some(Ok(b)));
        assert_eq!(failed.settings_retained, false);
        assert_eq!(failed.route_retained, false);
        assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
        assert_eq!(calls(&counts), 1);
        assert_eq!(submitted(&counts), vec![]);
        assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
        assert_eq!(case.read("knob.ctr"), b"017\n");
        assert_eq!(drops(&counts), 0);
        drop(session); drop(evidence); assert_eq!(drops(&counts), 1);
    }
}

#[test]
fn initialization_hook_unwinds_keep_each_published_resource_and_forbid_retry() {
    for (n, point, phase, settings, route_retained, queries) in [
        (0, HookPoint::AfterBaselineArmed, InitPhase::BaselineArmed, false, false, 0),
        (1, HookPoint::BaselineCaptured, InitPhase::BaselineCaptured, false, false, 1),
        (2, HookPoint::SettingsRetained, InitPhase::SettingsRetained, true, false, 1),
        (3, HookPoint::RouteRetained, InitPhase::RouteRetained, true, true, 1),
    ] {
        let case = Case::new(&format!("init-gap-{n}")); case.original();
        let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
        session.set_hook(Box::new(move |seen| if seen == point { panic!("synthetic construction gap"); }));
        assert_eq!(unwind(|| { let _ = session.initialize(4, [0x71; 16], 0o640, SELECT); }), true);
        let state = evidence.snapshot();
        assert_eq!(state.journal.phase, phase);
        assert_eq!(state.settings_retained, settings);
        assert_eq!(state.route_retained, route_retained);
        assert_eq!(state.journal.captured_baseline, if queries == 0 { None } else { Some(Ok(baseline())) });
        if settings {
            assert_eq!(state.settings.as_ref().unwrap().pair.baseline, baseline());
            assert_eq!(state.settings.as_ref().unwrap().pair.last, None);
        }
        assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
        assert_eq!(calls(&counts), queries);
        assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
        drop(session); assert_eq!(drops(&counts), 0);
        case.unchanged(); drop(evidence); assert_eq!(drops(&counts), 1);
    }
}

#[test]
fn snapshot_finishes_before_blocked_baseline_query_is_released() {
    let case = Case::new("blocked-baseline"); case.original();
    let (block, entered, release) = barrier();
    let (mut backend, counts) = backend(baseline(), BaselineReply::Ok, Reply::Commit);
    backend.baseline_block = Some(block);
    let (mut session, evidence) = FixedSession::retain(Box::new(backend), case.file());
    let job = thread::spawn(move || {
        let route = initialize(&mut session); (session, route)
    });
    entered.recv_timeout(WAIT).unwrap();
    let (rx, observer) = snapshot_thread(evidence.clone());
    let observed = rx.recv_timeout(WAIT);
    release.send(()).unwrap();
    let (_session, _route) = job.join().unwrap(); observer.join().unwrap();
    assert_eq!(observed.is_ok(), true, "snapshot must finish before baseline release");
    let state = observed.unwrap();
    assert_eq!(state.journal.phase, InitPhase::BaselineArmed);
    assert_eq!(state.journal.captured_baseline, None);
    assert_eq!(calls(&counts), 1);
    assert_eq!(submitted(&counts), vec![]);
    case.unchanged();
}

#[test]
fn snapshot_finishes_before_blocked_submit_is_released() {
    let case = Case::new("blocked-submit"); case.original();
    let (block, entered, release) = barrier();
    let (mut backend, counts) = backend(baseline(), BaselineReply::Ok, Reply::Unknown);
    backend.submit_block = Some(block);
    let (mut session, evidence) = FixedSession::retain(Box::new(backend), case.file());
    let route = initialize(&mut session); let b = arm(&route);
    let request = route.create_for_key(b.key, frame(71)).unwrap();
    let dispatched = route.clone();
    let job = thread::spawn(move || dispatched.dispatch(request));
    entered.recv_timeout(WAIT).unwrap();
    let (rx, observer) = snapshot_thread(evidence.clone());
    let observed = rx.recv_timeout(WAIT);
    release.send(()).unwrap();
    let result = job.join().unwrap().unwrap(); observer.join().unwrap();
    assert_eq!(observed.is_ok(), true, "snapshot must finish before submit release");
    let state = observed.unwrap();
    assert_eq!(state.journal.last_submit, Some(SubmitAttempt { request, phase: SubmitPhase::Armed }));
    assert_eq!(state.settings.unwrap().journal.last_accepted_attempt, None);
    assert_eq!(result, DispatchResult::Unknown);
    assert_eq!(submitted(&counts), vec![request]); case.unchanged();
}

#[test]
fn snapshot_finishes_before_blocked_settings_writer_is_released() {
    let case = Case::new("blocked-settings"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session);
    let (block, entered, release) = barrier();
    session.set_sink_hook(Box::new(move |point| {
        if point == SinkHookPoint::AfterPlan { block.hit(); }
    })).unwrap();
    let b = arm(&route); let request = route.create_for_key(b.key, frame(73)).unwrap();
    let dispatched = route.clone();
    let job = thread::spawn(move || dispatched.dispatch(request));
    entered.recv_timeout(WAIT).unwrap();
    let (rx, observer) = snapshot_thread(evidence.clone());
    let observed = rx.recv_timeout(WAIT);
    release.send(()).unwrap();
    let result = job.join().unwrap().unwrap(); observer.join().unwrap();
    assert_eq!(observed.is_ok(), true, "snapshot must finish before settings release");
    let state = observed.unwrap();
    let attempt = state.settings.unwrap().journal.last_accepted_attempt.unwrap();
    assert_eq!(attempt.phase, AttemptPhase::PairArmed);
    assert_eq!(attempt.ack.request, request);
    assert_eq!(attempt.targets.is_some(), true);
    assert_eq!(result, DispatchResult::Committed { settings_written: true });
    assert_eq!(submitted(&counts), vec![request]);
}

#[test]
fn exact_submit_outcomes_are_retained_without_inventing_ack_or_settings() {
    for (n, reply) in [Reply::Commit, Reply::Unknown, Reply::Reject].into_iter().enumerate() {
        let case = Case::new(&format!("outcome-{n}")); case.original();
        let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, reply);
        let route = initialize(&mut session); let b = arm(&route);
        let request = route.create_for_key(b.key, frame(79)).unwrap();
        let outcome = route.dispatch(request).unwrap();
        let expected = match reply {
            Reply::Commit => BackendOutcome::Committed(FrameAck { request, kernel_sequence: b.kernel_sequence + 1 }),
            Reply::Unknown => BackendOutcome::Unknown,
            Reply::Reject => BackendOutcome::Rejected { request, reason: 31 },
            _ => unreachable!(),
        };
        assert_eq!(evidence.snapshot().journal.last_submit,
            Some(SubmitAttempt { request, phase: SubmitPhase::Returned(expected) }));
        let status = route.diagnostic_snapshot().producer.status;
        if matches!(reply, Reply::Commit) {
            assert_eq!(outcome, DispatchResult::Committed { settings_written: true });
            assert_eq!(status.last_ack, Some(AckCheckpoint {
                ack: FrameAck { request, kernel_sequence: b.kernel_sequence + 1 },
                settings: AckSettings::ReturnedOk }));
        } else {
            assert_eq!(status.last_ack, None);
            assert_eq!(evidence.snapshot().settings.unwrap().journal.last_accepted_attempt, None);
            case.unchanged();
        }
        assert_eq!(submitted(&counts), vec![request]);
        assert_eq!(calls(&counts), 1);
    }
}

#[test]
fn submit_unwind_keeps_exact_armed_request_and_poisoned_actual_backend() {
    let case = Case::new("submit-panic"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Panic);
    let route = initialize(&mut session); let b = arm(&route);
    let request = route.create_for_key(b.key, frame(83)).unwrap();
    assert_eq!(unwind(|| { let _ = route.dispatch(request); }), true);
    let state = evidence.snapshot();
    assert_eq!(state.journal.last_submit, Some(SubmitAttempt { request, phase: SubmitPhase::Armed }));
    assert_eq!(state.backend_poisoned, true);
    assert_eq!(state.settings.unwrap().journal.last_accepted_attempt, None);
    let accounting = route.diagnostic_snapshot();
    assert_eq!(accounting.endpoint_poisoned, true);
    assert_eq!(accounting.producer.status.last_ack, None);
    assert_eq!(accounting.producer.status.unresolved.unwrap().request, request);
    assert_eq!(route.dispatch(request).is_err(), true);
    assert_eq!(submitted(&counts), vec![request]);
    drop(route); drop(session); assert_eq!(drops(&counts), 0);
    case.unchanged(); drop(evidence); assert_eq!(drops(&counts), 1);
}

#[test]
fn journal_poison_after_native_return_salvages_outcome_but_refuses_ack_acceptance() {
    let case = Case::new("return-poison"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let retained_weak = Arc::downgrade(&evidence.retention);
    session.set_hook(Box::new(move |point| {
        if point == HookPoint::AfterUnderlyingSubmitReturn {
            let poisoned = EvidenceHandle { retention: retained_weak.upgrade().unwrap() };
            poisoned.poison_journal_for_test();
        }
    }));
    let route = initialize(&mut session); let b = arm(&route);
    let request = route.create_for_key(b.key, frame(89)).unwrap();
    assert_eq!(route.dispatch(request).unwrap(), DispatchResult::Unknown);
    let state = evidence.snapshot();
    assert_eq!(state.journal.poisoned, true);
    assert_eq!(state.journal.last_submit, Some(SubmitAttempt { request,
        phase: SubmitPhase::Returned(BackendOutcome::Committed(FrameAck {
            request, kernel_sequence: b.kernel_sequence + 1 })) }));
    assert_eq!(state.settings.unwrap().journal.last_accepted_attempt, None);
    let status = route.diagnostic_snapshot().producer.status;
    assert_eq!(status.last_ack, None);
    assert_eq!(status.unresolved.unwrap().request, request);
    assert_eq!(status.unresolved.unwrap().uncertain, true);
    assert_eq!(submitted(&counts), vec![request]);
    assert_eq!(route.dispatch(request).is_err(), true);
    case.unchanged();
}

#[test]
fn poisoned_construction_journal_or_slots_cannot_query_or_build_a_route() {
    for poison_slots in [false, true] {
        let case = Case::new(if poison_slots { "slots-poison" } else { "journal-poison" });
        case.original();
        let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
        if poison_slots { evidence.poison_slots_for_test(); }
        else { evidence.poison_journal_for_test(); }
        assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
        let state = evidence.snapshot();
        assert_eq!(if poison_slots { state.slots_poisoned } else { state.journal.poisoned }, true);
        assert_eq!(state.settings_retained, false);
        assert_eq!(state.route_retained, false);
        assert_eq!(calls(&counts), 0);
        assert_eq!(submitted(&counts), vec![]);
        assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
        drop(session); assert_eq!(drops(&counts), 0);
        case.unchanged(); drop(evidence); assert_eq!(drops(&counts), 1);
    }
}

#[test]
fn evidence_only_or_route_only_custody_retains_backend_and_releases_without_cycle() {
    let case = Case::new("evidence-only"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session);
    let directory = evidence.original_directory_for_test();
    let weak_directory = Arc::downgrade(&directory); drop(directory);
    drop(route); drop(session);
    assert_eq!(drops(&counts), 0);
    assert_eq!(weak_directory.upgrade().is_some(), true);
    assert_eq!(evidence.snapshot().route_retained, true);
    assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
    drop(evidence);
    assert_eq!(drops(&counts), 1);
    assert_eq!(weak_directory.upgrade().is_none(), true);

    let case = Case::new("route-only"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session); let b = arm(&route);
    drop(evidence); drop(session);
    assert_eq!(drops(&counts), 0);
    let request = route.create_for_key(b.key, frame(97)).unwrap();
    assert_eq!(route.dispatch(request).unwrap(), DispatchResult::Committed { settings_written: true });
    assert_eq!(case.read("knob.ctr"), frame(97)[34].to_string().as_bytes());
    drop(route); assert_eq!(drops(&counts), 1);
}

#[test]
fn late_positive_ack_after_close_skips_files_and_cannot_rearm() {
    let case = Case::new("late-ack"); case.original();
    let (block, entered, release) = barrier();
    let (mut backend, counts) = backend(baseline(), BaselineReply::Ok, Reply::Commit);
    backend.submit_block = Some(block);
    let (mut session, evidence) = FixedSession::retain(Box::new(backend), case.file());
    let route = initialize(&mut session); let b = arm(&route);
    let before = evidence.snapshot().settings;
    let request = route.create_for_key(b.key, frame(101)).unwrap();
    let dispatched = route.clone();
    let job = thread::spawn(move || dispatched.dispatch(request));
    entered.recv_timeout(WAIT).unwrap();
    route.close().unwrap(); release.send(()).unwrap();
    assert_eq!(job.join().unwrap().unwrap(), DispatchResult::Committed { settings_written: false });
    let known = FrameAck { request, kernel_sequence: b.kernel_sequence + 1 };
    assert_eq!(route.diagnostic_snapshot().producer.status.last_ack,
        Some(AckCheckpoint { ack: known, settings: AckSettings::SkippedClosed }));
    assert_eq!(evidence.snapshot().settings, before);
    changed_backend(&counts);
    assert_eq!(route.refresh_baseline().is_err(), true);
    assert_eq!(calls(&counts), 1);
    assert_eq!(submitted(&counts), vec![request]); case.unchanged();
}

#[test]
fn real_worker_and_lifecycle_keep_exact_fixed_session_after_successful_join() {
    let case = Case::new("owned-join"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session);
    let (tx, entered) = mpsc::sync_channel(1);
    session.set_sink_hook(Box::new(move |point| {
        if point == SinkHookPoint::AfterPairReturn { tx.send(()).unwrap(); }
    })).unwrap();
    let (port, worker) = mixer_transport::channel(route.clone(), 4).unwrap();
    let port = Arc::new(port);
    let owner = match OwnedLifecycle::<()>::try_new(port.clone(), worker, None) {
        Ok(owner) => owner, Err(_) => panic!("actual matching pair refused"),
    };
    let b = port.refresh_baseline().unwrap(); port.confirm_adoption(b, true).unwrap();
    let request = port.enqueue_for_key(b.key, frame(107)).unwrap();
    entered.recv_timeout(WAIT).unwrap();
    assert_eq!(owner.finish(), FinishStatus::RecoveryRequired);
    let state = owner.snapshot().unwrap();
    let accounting = state.joined.unwrap();
    assert_eq!(state.joined_exit, Some(WorkerExit::Stopped));
    assert_eq!(accounting.route.producer.status.last_ack, Some(AckCheckpoint {
        ack: FrameAck { request, kernel_sequence: b.kernel_sequence + 1 },
        settings: AckSettings::ReturnedOk }));
    assert_eq!(accounting.mailbox.status.last.unwrap().outcome,
        AttemptOutcome::Committed { settings_written: true });
    assert_eq!(evidence.snapshot().settings.unwrap().journal.last_accepted_attempt.unwrap().ack.request, request);
    assert_eq!(port.refresh_baseline().is_err(), true);
    assert_eq!(calls(&counts), 1);
    drop(owner); drop(port); drop(route); drop(session);
    assert_eq!(drops(&counts), 0);
    drop(evidence); assert_eq!(drops(&counts), 1);
}

#[test]
fn callback_panic_join_keeps_pair_commit_without_callback_completion_or_replay() {
    let case = Case::new("callback-join"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session);
    let (tx, entered) = mpsc::sync_channel(1);
    session.set_sink_hook(Box::new(move |point| {
        if point == SinkHookPoint::AfterPairReturn {
            tx.send(()).unwrap(); panic!("synthetic callback completion gap");
        }
    })).unwrap();
    let (port, worker) = mixer_transport::channel(route.clone(), 4).unwrap();
    let b = port.refresh_baseline().unwrap(); port.confirm_adoption(b, true).unwrap();
    let request = port.enqueue_for_key(b.key, frame(109)).unwrap();
    entered.recv_timeout(WAIT).unwrap();
    let joined = worker.join().unwrap();
    assert_eq!(joined.exit(), WorkerExit::Panicked);
    assert_eq!(joined.last_ack(), Some(AckCheckpoint {
        ack: FrameAck { request, kernel_sequence: b.kernel_sequence + 1 },
        settings: AckSettings::CallbackArmed }));
    let state = evidence.snapshot();
    assert_eq!(state.settings_poisoned, true);
    let settings = state.settings.unwrap();
    assert_eq!(settings.journal.last_accepted_attempt.unwrap().phase, AttemptPhase::PairArmed);
    assert_eq!(settings.pair.last.unwrap().result, TransactionResult::Committed);
    assert_eq!(submitted(&counts), vec![request]);
    assert_eq!(port.refresh_baseline().is_err(), true);
    drop(joined); drop(port); drop(route); drop(session);
    assert_eq!(drops(&counts), 0);
    assert_eq!(case.read("knob.ctr"), frame(109)[34].to_string().as_bytes());
    drop(evidence); assert_eq!(drops(&counts), 1);
}

#[test]
fn baseline_delivery_unwind_never_restores_or_requeries_consumed_grant() {
    let case = Case::new("grant-panic"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    session.set_hook(Box::new(move |point| {
        if point == HookPoint::BaselineServed { panic!("synthetic grant delivery gap"); }
    }));
    let route = initialize(&mut session);
    assert_eq!(unwind(|| { let _ = route.refresh_baseline(); }), true);
    assert_eq!(evidence.snapshot().journal.baseline_served, true);
    assert_eq!(route.diagnostic_snapshot().endpoint_poisoned, true);
    assert_eq!(route.current_key(), None);
    assert_eq!(route.refresh_baseline().is_err(), true);
    assert_eq!(submitted(&counts), vec![]);
    assert_eq!(calls(&counts), 1);
    case.unchanged();
}

#[test]
fn repeated_initialize_after_ready_preserves_original_grant_and_configuration() {
    let case = Case::new("ready-retry"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let route = initialize(&mut session); let ready = evidence.snapshot();
    let retry = session.initialize(3, [0x35; 16], 0o600,
        Selection { selector: false, fx_time: false });
    assert_eq!(retry.err(), Some(InitError::AlreadyAttempted));
    let after = evidence.snapshot();
    assert_eq!(after.journal.phase, InitPhase::Ready);
    assert_eq!(after.journal.terminal, None);
    assert_eq!(after.journal.config, ready.journal.config);
    assert_eq!(after.journal.captured_baseline, ready.journal.captured_baseline);
    assert_eq!(after.settings, ready.settings);
    assert_eq!(after.journal.first_refusal, Some(Refusal {
        operation: RefusalOperation::Initialize, error: InitError::AlreadyAttempted, request: None }));
    let b = arm(&route);
    assert_eq!(b, baseline());
    let request = route.create_for_key(b.key, frame(113)).unwrap();
    assert_eq!(route.dispatch(request).unwrap(), DispatchResult::Committed { settings_written: true });
    assert_eq!(calls(&counts), 1);
    assert_eq!(submitted(&counts), vec![request]);
}

#[test]
fn poisoned_retained_settings_writer_cannot_publish_a_ready_route() {
    let case = Case::new("writer-poison"); case.original();
    let (mut session, evidence, counts) = retain(&case, baseline(), BaselineReply::Ok, Reply::Commit);
    let retained_weak = Arc::downgrade(&evidence.retention);
    session.set_hook(Box::new(move |point| {
        if point == HookPoint::SettingsRetained {
            let retained = EvidenceHandle { retention: retained_weak.upgrade().unwrap() };
            retained.poison_settings_for_test();
        }
    }));
    let result = session.initialize(4, [0x71; 16], 0o640, SELECT);
    assert_eq!(result.err(), Some(InitError::Poisoned(PoisonTarget::Settings)));
    let state = evidence.snapshot();
    assert_eq!(state.journal.phase, InitPhase::SettingsRetained);
    assert_eq!(state.settings_retained, true);
    assert_eq!(state.settings_poisoned, true);
    assert_eq!(state.route_retained, false);
    assert_eq!(state.settings.unwrap().pair.baseline, baseline());
    assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
    assert_eq!(calls(&counts), 1);
    assert_eq!(submitted(&counts), vec![]);
    assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
    drop(session); assert_eq!(drops(&counts), 0);
    case.unchanged(); drop(evidence); assert_eq!(drops(&counts), 1);
}

#[test]
fn poisoned_journal_retains_full_returned_baseline_without_constructing_a_route() {
    let case = Case::new("baseline-return-poison"); case.original();
    let (block, entered, release) = barrier();
    let (mut backend, counts) = backend(baseline(), BaselineReply::Ok, Reply::Commit);
    backend.baseline_block = Some(block);
    let (mut session, evidence) = FixedSession::retain(Box::new(backend), case.file());
    let job = thread::spawn(move || {
        let result = session.initialize(4, [0x71; 16], 0o640, SELECT);
        (session, result)
    });
    entered.recv_timeout(WAIT).unwrap();
    evidence.poison_journal_for_test();
    release.send(()).unwrap();
    let (mut session, result) = job.join().unwrap();
    assert_eq!(result.err(), Some(InitError::Poisoned(PoisonTarget::Journal)));
    let state = evidence.snapshot();
    assert_eq!(state.journal.poisoned, true);
    assert_eq!(state.journal.captured_baseline, Some(Ok(baseline())));
    assert_eq!(state.journal.phase, InitPhase::BaselineCaptured);
    assert_eq!(state.backend_poisoned, false);
    assert_eq!(state.settings_retained, false);
    assert_eq!(state.route_retained, false);
    assert_eq!(session.initialize(4, [0x71; 16], 0o640, SELECT).is_err(), true);
    assert_eq!(calls(&counts), 1);
    assert_eq!(submitted(&counts), vec![]);
    assert_eq!(evidence.directory_identity_for_test().unwrap(), case.identity());
    drop(session); assert_eq!(drops(&counts), 0);
    case.unchanged(); drop(evidence); assert_eq!(drops(&counts), 1);
}
