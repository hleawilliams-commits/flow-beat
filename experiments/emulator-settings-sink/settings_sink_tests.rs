// SPDX-License-Identifier: GPL-3.0-only
// Real isolated host files with actual frozen route, producer and worker types.
// The backend is synthetic and authenticates no native device or broker receipt.
// The runner must supply a fresh explicit private filesystem root. No /tmp,
// ambient application settings, emulator, endpoint, main or native backend.
use super::*;
use crate::mixer_producer::{AckCheckpoint, AckSettings, Envelope, Frame, Key, Producer};
use crate::mixer_route::{BackendOutcome, DispatchResult, MixerBackend, MixerRoute};
use crate::mixer_transport::{self, AttemptOutcome, WorkerExit};
use crate::settings_pair::{ErrorKind, FaultAction, FaultPoint, FileKind, TransactionResult};
use std::fs::{self, File};
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{mpsc, Arc, Mutex};
use std::thread;
use std::time::Duration;

static NEXT_CASE: AtomicUsize = AtomicUsize::new(1);
const NONE: Selection = Selection { selector: false, fx_time: false };
const ALL: Selection = Selection { selector: true, fx_time: true };
const SAVED: &[u8] = b"{\"10\": 99, \"22\": 160, \"26\": 7, \"27\": 8}";
const ANALOG: [usize; 51] = [
    10, 11, 12, 13, 14, 15, 16, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68,
    69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 80,
];

struct Case { path: PathBuf }
impl Case {
    fn new(label: &str) -> Self {
        let root = PathBuf::from(std::env::var_os("AZ_SETTINGS_SINK_TEST_ROOT")
            .expect("runner must supply an explicit isolated settings-sink test root"));
        assert_eq!(root.is_absolute(), true);
        let meta = fs::symlink_metadata(&root).expect("explicit root exists");
        assert_eq!(meta.is_dir() && !meta.file_type().is_symlink(), true);
        let serial = NEXT_CASE.fetch_add(1, Ordering::Relaxed);
        let path = root.join(format!("{}-{}-{}", label, std::process::id(), serial));
        fs::create_dir(&path).expect("fresh exclusive case directory");
        fs::set_permissions(&path, fs::Permissions::from_mode(0o700)).unwrap();
        Self { path }
    }
    fn directory(&self) -> File { File::open(&self.path).unwrap() }
    fn write(&self, name: &str, bytes: &[u8]) {
        fs::write(self.path.join(name), bytes).unwrap();
        fs::set_permissions(self.path.join(name), fs::Permissions::from_mode(0o600)).unwrap();
    }
    fn originals(&self) {
        self.write("knob.ctr", b"017\n");
        self.write("mixer.state", SAVED);
    }
    fn read(&self, name: &str) -> Vec<u8> { fs::read(self.path.join(name)).unwrap() }
    fn names(&self) -> Vec<String> {
        let mut names = fs::read_dir(&self.path).unwrap()
            .map(|entry| entry.unwrap().file_name().into_string().unwrap()).collect::<Vec<_>>();
        names.sort(); names
    }
}

fn baseline() -> BaselineReceipt {
    BaselineReceipt { key: Key { instance: [0x1234, 0x5678], run_id: 7,
        epoch: 11, producer_id: 13 }, frame: [0x5a; 152], kernel_sequence: 23 }
}
fn frame(salt: u8) -> Frame {
    std::array::from_fn(|i| (i as u8).wrapping_mul(73).wrapping_add(salt))
}
fn ack(b: BaselineReceipt, count: usize, bytes: Frame) -> FrameAck {
    let producer = Producer::new(64).unwrap();
    producer.stage_baseline(b).unwrap();
    producer.acknowledge_baseline(b).unwrap();
    producer.confirm_released(b, true).unwrap();
    let mut request = None;
    for _ in 0..count { request = Some(producer.create_for_key(b.key, bytes).unwrap()); }
    FrameAck { request: request.expect("nonzero synthetic request"),
        kernel_sequence: b.kernel_sequence.checked_add(count as u64).unwrap() }
}
fn attach(case: &Case, b: BaselineReceipt, selection: Selection)
    -> (SettingsSinkBridge, EvidenceHandle) {
    SettingsSinkBridge::attach(case.directory(), b, [0x61; 16], 0o640, selection).unwrap()
}
fn image_bytes(image: &FileImage) -> Option<&[u8]> {
    match image { FileImage::Absent => None, FileImage::Present { bytes, .. } => Some(bytes) }
}
// Independent whole-output oracle, not the production mapper/parser.
fn targets(bytes: Frame, selection: Selection, saved_present: bool) -> PairTargets {
    let mut values: Vec<(usize, u8)> = ANALOG.iter().map(|&n| (n, bytes[n])).collect();
    if selection.selector { values.push((22, bytes[22] & 0xf0)); }
    else if saved_present { values.push((22, 160)); }
    if selection.fx_time { values.extend([(26, bytes[26]), (27, bytes[27])]); }
    else if saved_present { values.extend([(26, 7), (27, 8)]); }
    values.sort_unstable_by_key(|&(n, _)| n);
    let fields: Vec<_> = values.iter().map(|(n, value)| format!("\"{}\": {}", n, value)).collect();
    PairTargets { knob: bytes[34].to_string().into_bytes(),
        mixer: format!("{{{}}}", fields.join(", ")).into_bytes() }
}
fn assert_files(case: &Case, expected: &PairTargets) {
    assert_eq!(case.read("knob.ctr"), expected.knob);
    assert_eq!(case.read("mixer.state"), expected.mixer);
}

#[derive(Clone, Copy)] enum Reply { Commit, Reject, Unknown, BadSequence, BadRequest }
struct BackendRecord { baseline: BaselineReceipt, calls: Vec<Envelope>, reply: Reply }
struct Backend(Arc<Mutex<BackendRecord>>);
impl MixerBackend for Backend {
    fn baseline(&mut self) -> Result<BaselineReceipt, String> {
        Ok(self.0.lock().unwrap().baseline)
    }
    fn submit(&mut self, request: Envelope) -> BackendOutcome {
        let mut record = self.0.lock().unwrap();
        record.calls.push(request);
        let next = record.baseline.kernel_sequence.checked_add(1).unwrap();
        match record.reply {
            Reply::Commit => { record.baseline.kernel_sequence = next;
                BackendOutcome::Committed(FrameAck { request, kernel_sequence: next }) }
            Reply::Reject => BackendOutcome::Rejected { request, reason: 5 },
            Reply::Unknown => BackendOutcome::Unknown,
            Reply::BadSequence => BackendOutcome::Committed(FrameAck { request, kernel_sequence: next + 1 }),
            Reply::BadRequest => {
                let mut different = *request.frame(); different[151] ^= 1;
                let changed = ack(record.baseline, request.request_id() as usize, different);
                BackendOutcome::Committed(changed)
            }
        }
    }
}
fn route(sink: SettingsSinkBridge, b: BaselineReceipt, reply: Reply)
    -> (Arc<MixerRoute>, Arc<Mutex<BackendRecord>>) {
    let record = Arc::new(Mutex::new(BackendRecord { baseline: b, calls: vec![], reply }));
    let route = MixerRoute::new(8, Box::new(Backend(record.clone())), Box::new(sink)).unwrap();
    (Arc::new(route), record)
}
fn arm(route: &MixerRoute) -> BaselineReceipt {
    let b = route.refresh_baseline().unwrap(); route.confirm_adoption(b, true).unwrap(); b
}
fn last_attempt(handle: &EvidenceHandle) -> AcceptedAttempt {
    handle.snapshot().journal.last_accepted_attempt.unwrap()
}

#[test]
fn queued_frames_persist_each_exact_ack_and_use_last_verified_mixer_image() {
    let case = Case::new("queued"); case.originals();
    let (sink, evidence) = attach(&case, baseline(), NONE);
    let original = evidence.snapshot();
    let (route, backend) = route(sink, baseline(), Reply::Commit);
    let b = arm(&route); let first_frame = frame(19); let second_frame = frame(101);
    let first = route.create_for_key(b.key, first_frame).unwrap();
    let second = route.create_for_key(b.key, second_frame).unwrap();
    assert_eq!(route.dispatch(first).unwrap(), DispatchResult::Committed { settings_written: true });
    let first_ack = FrameAck { request: first, kernel_sequence: b.kernel_sequence + 1 };
    let first_state = evidence.snapshot();
    let first_attempt = first_state.journal.last_accepted_attempt.clone().unwrap();
    assert_eq!(first_attempt.ack, first_ack);
    assert_eq!(first_attempt.source_mixer, original.pair.current.mixer);
    assert_eq!(first_attempt.targets, Some(targets(first_frame, NONE, true)));
    assert_files(&case, &targets(first_frame, NONE, true));
    assert_eq!(route.dispatch(second).unwrap(), DispatchResult::Committed { settings_written: true });
    let second_ack = FrameAck { request: second, kernel_sequence: b.kernel_sequence + 2 };
    let state = evidence.snapshot(); let attempt = state.journal.last_accepted_attempt.unwrap();
    assert_eq!(attempt.ack, second_ack);
    assert_eq!(attempt.source_mixer, first_state.pair.current.mixer);
    assert_eq!(attempt.targets, Some(targets(second_frame, NONE, true)));
    assert_eq!(attempt.phase, AttemptPhase::ReturnedOk(CommitReceipt {
        ack: second_ack, pair: state.pair.current.clone() }));
    assert_files(&case, &targets(second_frame, NONE, true));
    assert_eq!(state.journal.next_request_id, 3);
    assert_eq!(state.journal.kernel_sequence, b.kernel_sequence + 2);
    assert_eq!(state.pair.original, original.pair.original);
    assert_eq!(backend.lock().unwrap().calls, vec![first, second]);
    assert_eq!(route.diagnostic_snapshot().producer.status.last_ack,
        Some(AckCheckpoint { ack: second_ack, settings: AckSettings::ReturnedOk }));
}

#[test]
fn fixed_optional_selection_maps_only_the_ack_and_retains_full_transient_bytes() {
    let case = Case::new("selection"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), ALL);
    let accepted = ack(baseline(), 1, frame(43));
    let receipt = sink.persist(accepted).unwrap();
    let attempt = last_attempt(&evidence);
    assert_eq!(attempt.ack, accepted);
    assert_eq!(attempt.targets, Some(targets(*accepted.request.frame(), ALL, true)));
    assert_eq!(receipt.ack, accepted);
    assert_eq!(evidence.snapshot().journal.selection, ALL);
    assert_eq!(evidence.snapshot().journal.next_request_id, 2);
    assert_eq!(evidence.snapshot().journal.kernel_sequence, accepted.kernel_sequence);
    assert_files(&case, &targets(*accepted.request.frame(), ALL, true));
}

#[test]
fn absent_settings_are_created_but_empty_saved_map_is_refused() {
    let absent = Case::new("absent");
    let (mut sink, evidence) = attach(&absent, baseline(), NONE);
    assert_eq!(evidence.snapshot().pair.original.mixer, FileImage::Absent);
    let accepted = ack(baseline(), 1, frame(13)); sink.persist(accepted).unwrap();
    assert_eq!(last_attempt(&evidence).source_mixer, FileImage::Absent);
    assert_files(&absent, &targets(frame(13), NONE, false));
    let empty = Case::new("empty"); empty.write("mixer.state", b"");
    let (mut sink, evidence) = attach(&empty, baseline(), NONE);
    let outcome = sink.persist(accepted);
    assert_eq!(matches!(outcome, Err(SinkError::Map(_))), true);
    assert_eq!(evidence.snapshot().pair.last, None);
    assert_eq!(empty.names(), vec!["mixer.state"]);
    assert_eq!(empty.read("mixer.state"), b"");
}

#[test]
fn unknown_rejected_and_malformed_positive_receipts_never_enter_the_sink() {
    for (n, reply) in [Reply::Unknown, Reply::Reject, Reply::BadSequence, Reply::BadRequest].into_iter().enumerate() {
        let case = Case::new(&format!("not-ack-{n}")); case.originals();
        let (sink, evidence) = attach(&case, baseline(), ALL); let before = evidence.snapshot();
        let (route, backend) = route(sink, baseline(), reply); let b = arm(&route);
        let request = route.create_for_key(b.key, frame(13)).unwrap();
        let result = route.dispatch(request);
        match reply {
            Reply::Unknown => assert_eq!(result.unwrap(), DispatchResult::Unknown),
            Reply::Reject => assert_eq!(result.unwrap(), DispatchResult::Rejected { reason: 5 }),
            _ => assert_eq!(result.is_err(), true),
        }
        assert_eq!(evidence.snapshot(), before);
        assert_eq!(case.read("knob.ctr"), b"017\n");
        assert_eq!(case.read("mixer.state"), SAVED);
        assert_eq!(backend.lock().unwrap().calls, vec![request]);
        assert_eq!(route.dispatch(request).is_err(), true);
        assert_eq!(backend.lock().unwrap().calls.len(), 1);
    }
}

#[test]
fn mapping_refusal_keeps_known_native_commit_without_replay_or_pair_io() {
    let case = Case::new("map-error"); case.originals(); case.write("mixer.state", b"{\"bad\":0}");
    let (sink, evidence) = attach(&case, baseline(), ALL); let original = evidence.snapshot();
    let (route, backend) = route(sink, baseline(), Reply::Commit); let b = arm(&route);
    let first = route.create_for_key(b.key, frame(11)).unwrap();
    let queued = route.create_for_key(b.key, frame(17)).unwrap();
    assert_eq!(matches!(route.dispatch(first).unwrap(), DispatchResult::CommittedSettingsFailed(_)), true);
    let known = FrameAck { request: first, kernel_sequence: b.kernel_sequence + 1 };
    let state = evidence.snapshot(); let attempt = state.journal.last_accepted_attempt.unwrap();
    assert_eq!(attempt.ack, known);
    assert_eq!(attempt.source_mixer, original.pair.current.mixer);
    assert_eq!(attempt.targets, None);
    assert_eq!(matches!(attempt.phase, AttemptPhase::Failed(SinkError::Map(_))), true);
    assert_eq!(matches!(state.journal.terminal, Some(SinkError::Map(_))), true);
    assert_eq!(state.pair, original.pair);
    assert_eq!(state.journal.next_request_id, 1);
    assert_eq!(state.journal.kernel_sequence, b.kernel_sequence);
    let producer = route.diagnostic_snapshot().producer;
    assert_eq!(producer.status.last_ack, Some(AckCheckpoint { ack: known, settings: AckSettings::ReturnedErr }));
    assert_eq!(producer.status.kernel_sequence, known.kernel_sequence);
    assert_eq!(producer.status.unresolved, None);
    assert_eq!(producer.status.admission_open, false);
    assert_eq!(route.dispatch(first).is_err(), true);
    assert_eq!(route.dispatch(queued).is_err(), true);
    assert_eq!(backend.lock().unwrap().calls, vec![first]);
    assert_eq!(case.read("mixer.state"), b"{\"bad\":0}");
}

#[test]
fn pair_failure_after_first_rename_retains_partial_effects_and_full_plan() {
    for (point, verified) in [
        (FaultPoint::AfterRename(FileKind::Knob), false),
        (FaultPoint::BeforeCreate(FileKind::Mixer), true),
    ] {
        let case = Case::new("pair-error"); case.originals();
        let (mut sink, evidence) = attach(&case, baseline(), NONE); let original = evidence.snapshot();
        sink.set_pair_fault_hook(Box::new(move |seen| if seen == point { FaultAction::Fail(5) } else { FaultAction::Continue }));
        let (route, backend) = route(sink, baseline(), Reply::Commit); let b = arm(&route);
        let request = route.create_for_key(b.key, frame(21)).unwrap();
        assert_eq!(matches!(route.dispatch(request).unwrap(), DispatchResult::CommittedSettingsFailed(_)), true);
        let expected = targets(frame(21), NONE, true);
        let state = evidence.snapshot(); let attempt = state.journal.last_accepted_attempt.clone().unwrap();
        assert_eq!(attempt.targets, Some(expected.clone()));
        assert_eq!(matches!(attempt.phase, AttemptPhase::Failed(SinkError::Pair(_))), true);
        assert_eq!(state.pair.original, original.pair.original);
        assert_eq!(state.pair.current.mixer, original.pair.current.mixer);
        assert_eq!(image_bytes(&state.pair.current.knob), Some(if verified { expected.knob.as_slice() } else { b"017\n" }));
        assert_eq!(case.read("knob.ctr"), expected.knob);
        assert_eq!(case.read("mixer.state"), SAVED);
        assert_eq!(matches!(state.pair.last.unwrap().result, TransactionResult::Failed(_)), true);
        assert_eq!(route.diagnostic_snapshot().producer.status.last_ack.unwrap().settings, AckSettings::ReturnedErr);
        assert_eq!(route.dispatch(request).is_err(), true);
        assert_eq!(evidence.snapshot().journal.last_accepted_attempt, Some(attempt));
        assert_eq!(backend.lock().unwrap().calls, vec![request]);
    }
}

#[test]
fn pair_panic_retains_callback_armed_and_pending_pair_after_route_drop() {
    let case = Case::new("pair-panic"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), NONE);
    sink.set_pair_fault_hook(Box::new(|point| if point == FaultPoint::AfterRename(FileKind::Knob) {
        FaultAction::Panic } else { FaultAction::Continue }));
    let (route, backend) = route(sink, baseline(), Reply::Commit); let b = arm(&route);
    let request = route.create_for_key(b.key, frame(31)).unwrap();
    let unwind = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| route.dispatch(request)));
    assert_eq!(unwind.is_err(), true);
    let accounting = route.diagnostic_snapshot();
    assert_eq!(accounting.endpoint_poisoned && accounting.producer.poisoned, true);
    assert_eq!(accounting.producer.status.last_ack.unwrap().settings, AckSettings::CallbackArmed);
    assert_eq!(accounting.producer.status.unresolved, None);
    let state = evidence.snapshot();
    assert_eq!(state.journal.last_accepted_attempt.as_ref().unwrap().phase, AttemptPhase::PairArmed);
    assert_eq!(state.pair.last.as_ref().unwrap().result, TransactionResult::Pending);
    assert_eq!(case.read("knob.ctr"), targets(frame(31), NONE, true).knob);
    assert_eq!(case.read("mixer.state"), SAVED);
    drop(route);
    assert_eq!(evidence.snapshot(), state);
    assert_eq!(backend.lock().unwrap().calls, vec![request]);
}

#[test]
fn panic_after_pair_return_does_not_invent_bridge_or_callback_completion() {
    let case = Case::new("return-gap"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), ALL);
    sink.set_hook(Box::new(|point| if point == HookPoint::AfterPairReturn { panic!("deliberate bridge completion gap"); }));
    let (route, _) = route(sink, baseline(), Reply::Commit); let b = arm(&route);
    let request = route.create_for_key(b.key, frame(41)).unwrap();
    assert_eq!(std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| route.dispatch(request))).is_err(), true);
    let state = evidence.snapshot(); let attempt = state.journal.last_accepted_attempt.unwrap();
    assert_eq!(attempt.phase, AttemptPhase::PairArmed);
    assert_eq!(state.pair.last.unwrap().result, TransactionResult::Committed);
    assert_eq!(state.journal.kernel_sequence, b.kernel_sequence);
    assert_eq!(state.pair.kernel_sequence, b.kernel_sequence + 1);
    assert_eq!(route.diagnostic_snapshot().producer.status.last_ack.unwrap().settings, AckSettings::CallbackArmed);
    assert_files(&case, &targets(frame(41), ALL, true));
}

#[test]
fn interrupted_mapping_and_plan_are_retained_and_refuse_later_attempts() {
    for point in [HookPoint::AfterMappingIntent, HookPoint::AfterPlan, HookPoint::AfterPairReturn] {
        let case = Case::new("interrupted"); case.originals();
        let (mut sink, evidence) = attach(&case, baseline(), NONE);
        sink.set_hook(Box::new(move |seen| if seen == point { panic!("deliberate retained intent"); }));
        let first = ack(baseline(), 1, frame(51));
        assert_eq!(std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| sink.persist(first))).is_err(), true);
        let before = evidence.snapshot(); let attempt = before.journal.last_accepted_attempt.clone().unwrap();
        let expected_phase = if point == HookPoint::AfterMappingIntent { AttemptPhase::MappingArmed } else { AttemptPhase::PairArmed };
        assert_eq!(attempt.phase, expected_phase);
        assert_eq!(attempt.targets.is_some(), point != HookPoint::AfterMappingIntent);
        assert_eq!(before.pair.last.as_ref().map(|tx| tx.result.clone()),
            if point == HookPoint::AfterPairReturn { Some(TransactionResult::Committed) } else { None });
        sink.set_hook(Box::new(|_| {}));
        assert_eq!(sink.persist(first), Err(SinkError::Interrupted));
        let refused = evidence.snapshot();
        assert_eq!(refused.journal.last_accepted_attempt, Some(attempt));
        assert_eq!(refused.pair, before.pair);
        assert_eq!(refused.journal.first_refusal, Some(Refusal { ack: first, error: SinkError::Interrupted }));
        if point == HookPoint::AfterPairReturn { assert_files(&case, &targets(frame(51), NONE, true)); }
        else {
            assert_eq!(case.read("knob.ctr"), b"017\n");
            assert_eq!(case.read("mixer.state"), SAVED);
        }
    }
}

#[test]
fn evidence_snapshot_completes_while_callback_is_blocked_without_journal_lock() {
    for point in [HookPoint::AfterMappingIntent, HookPoint::AfterPlan, HookPoint::AfterPairReturn] {
        let case = Case::new("inspection"); case.originals();
        let (mut sink, evidence) = attach(&case, baseline(), NONE);
        let (entered_tx, entered_rx) = mpsc::sync_channel(1);
        let (resume_tx, resume_rx) = mpsc::sync_channel(1);
        sink.set_hook(Box::new(move |seen| if seen == point {
            entered_tx.send(()).unwrap(); resume_rx.recv().unwrap();
        }));
        let (route, _) = route(sink, baseline(), Reply::Commit); let b = arm(&route);
        let request = route.create_for_key(b.key, frame(61)).unwrap();
        let dispatch_route = route.clone(); let work = thread::spawn(move || dispatch_route.dispatch(request));
        entered_rx.recv_timeout(Duration::from_secs(10)).unwrap();
        let (snapshot_tx, snapshot_rx) = mpsc::sync_channel(1); let reader = evidence.clone();
        let inspect = thread::spawn(move || { let _ = snapshot_tx.send(reader.snapshot()); });
        let snapshot = snapshot_rx.recv_timeout(Duration::from_secs(5));
        // Always release before assertions so a control retaining the lock can unwind.
        resume_tx.send(()).unwrap();
        let dispatched = work.join().unwrap(); inspect.join().unwrap();
        assert_eq!(snapshot.is_ok(), true, "snapshot must complete before the callback is released");
        let observed = snapshot.unwrap();
        let phase = observed.journal.last_accepted_attempt.unwrap().phase;
        assert_eq!(phase, if point == HookPoint::AfterMappingIntent { AttemptPhase::MappingArmed } else { AttemptPhase::PairArmed });
        assert_eq!(observed.pair.last.map(|tx| tx.result),
            if point == HookPoint::AfterPairReturn { Some(TransactionResult::Committed) } else { None });
        assert_eq!(dispatched.unwrap(), DispatchResult::Committed { settings_written: true });
    }
}

#[test]
fn exact_and_changed_duplicate_are_terminal_without_any_pair_operation() {
    for changed in [false, true] {
        let case = Case::new("duplicate"); case.originals();
        let (mut sink, evidence) = attach(&case, baseline(), NONE);
        let first = ack(baseline(), 1, frame(71)); sink.persist(first).unwrap();
        let previous = evidence.snapshot();
        case.write("mixer.state", b"external drift deliberately not re-read on duplicate");
        let pair_calls = Arc::new(AtomicUsize::new(0)); let counted = pair_calls.clone();
        sink.set_pair_fault_hook(Box::new(move |_| { counted.fetch_add(1, Ordering::SeqCst); FaultAction::Continue }));
        let duplicate = if changed { ack(baseline(), 1, frame(72)) } else { first };
        assert_eq!(sink.persist(duplicate), Err(SinkError::DuplicateOrStale));
        let rejected = evidence.snapshot();
        assert_eq!(rejected.pair, previous.pair);
        assert_eq!(rejected.journal.last_accepted_attempt, previous.journal.last_accepted_attempt);
        assert_eq!(rejected.journal.terminal, Some(SinkError::DuplicateOrStale));
        assert_eq!(rejected.journal.first_refusal, Some(Refusal { ack: duplicate, error: SinkError::DuplicateOrStale }));
        assert_eq!(pair_calls.load(Ordering::SeqCst), 0);
        assert_eq!(case.read("mixer.state"), b"external drift deliberately not re-read on duplicate");
        let later = ack(baseline(), 2, frame(73));
        assert_eq!(sink.persist(later), Err(SinkError::Closed));
        assert_eq!(sink.persist(first), Err(SinkError::Closed));
        assert_eq!(evidence.snapshot().journal.first_refusal, rejected.journal.first_refusal);
        assert_eq!(evidence.snapshot().journal.last_accepted_attempt, previous.journal.last_accepted_attempt);
        assert_eq!(evidence.snapshot().pair, previous.pair);
    }
}

#[test]
fn every_key_component_and_sequence_order_are_checked_before_plan_or_io() {
    for variant in 0..8 {
        let case = Case::new("gates"); case.originals();
        let (mut sink, evidence) = attach(&case, baseline(), NONE); let before = evidence.snapshot();
        let mut b = baseline();
        match variant {
            0 => b.key.instance[0] += 1,
            1 => b.key.instance[1] += 1,
            2 => b.key.run_id += 1,
            3 => b.key.epoch += 1,
            4 => b.key.producer_id += 1,
            _ => (),
        }
        let mut wrong = ack(b, if variant == 5 { 2 } else { 1 }, frame(81));
        if variant == 6 { wrong.kernel_sequence -= 1; }
        if variant == 7 { wrong.kernel_sequence += 1; }
        let expected = if variant < 5 { SinkError::WrongKey } else { SinkError::OutOfOrder };
        assert_eq!(sink.persist(wrong), Err(expected.clone()));
        let state = evidence.snapshot();
        assert_eq!(state.journal.last_accepted_attempt, None);
        assert_eq!(state.journal.first_refusal, Some(Refusal { ack: wrong, error: expected.clone() }));
        assert_eq!(state.journal.terminal, Some(expected));
        assert_eq!(state.pair, before.pair);
        assert_eq!(case.read("knob.ctr"), b"017\n");
        assert_eq!(case.read("mixer.state"), SAVED);
    }
}

#[test]
fn refusal_after_success_preserves_last_plan_and_first_refusal_independently() {
    let case = Case::new("first-refusal"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), NONE);
    let accepted = ack(baseline(), 1, frame(91)); sink.persist(accepted).unwrap();
    let previous = evidence.snapshot();
    let skipped = ack(baseline(), 3, frame(92));
    assert_eq!(sink.persist(skipped), Err(SinkError::OutOfOrder));
    let mut newer = baseline(); newer.key.epoch += 1;
    let later = ack(newer, 1, frame(93));
    assert_eq!(sink.persist(later), Err(SinkError::Closed));
    assert_eq!(sink.persist(accepted), Err(SinkError::Closed));
    let state = evidence.snapshot();
    assert_eq!(state.journal.terminal, Some(SinkError::OutOfOrder));
    assert_eq!(state.journal.first_refusal, Some(Refusal { ack: skipped, error: SinkError::OutOfOrder }));
    assert_eq!(state.journal.last_accepted_attempt, previous.journal.last_accepted_attempt);
    assert_eq!(state.pair, previous.pair);
}

#[test]
fn mapping_uses_retained_image_then_pair_refuses_external_drift_before_replacement() {
    let case = Case::new("drift"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), NONE);
    let first = ack(baseline(), 1, frame(101)); sink.persist(first).unwrap();
    let before = evidence.snapshot(); let knob = case.read("knob.ctr");
    case.write("mixer.state", b"{\"22\": 32}");
    let second = ack(baseline(), 2, frame(102));
    let result = sink.persist(second);
    assert_eq!(matches!(result, Err(SinkError::Pair(PairError { kind: ErrorKind::Drift, .. }))), true);
    let state = evidence.snapshot(); let attempt = state.journal.last_accepted_attempt.unwrap();
    assert_eq!(attempt.source_mixer, before.pair.current.mixer);
    assert_eq!(attempt.targets, Some(targets(frame(102), NONE, true)));
    assert_eq!(state.pair.last, before.pair.last);
    assert_eq!(case.read("knob.ctr"), knob);
    assert_eq!(case.read("mixer.state"), b"{\"22\": 32}");
}

#[test]
fn final_sequence_capacity_commit_succeeds_before_future_callbacks_are_closed() {
    let case = Case::new("capacity"); case.originals();
    let mut b = baseline(); b.kernel_sequence = u64::MAX - 1;
    let (mut sink, evidence) = attach(&case, b, NONE);
    let last = ack(b, 1, frame(111)); let receipt = sink.persist(last).unwrap();
    let state = evidence.snapshot();
    assert_eq!(receipt.ack, last);
    assert_eq!(state.journal.last_accepted_attempt.unwrap().phase, AttemptPhase::ReturnedOk(receipt));
    assert_eq!(state.journal.kernel_sequence, u64::MAX);
    assert_eq!(state.journal.terminal, Some(SinkError::Overflow));
    assert_eq!(state.pair.last.unwrap().result, TransactionResult::Committed);
    assert_eq!(state.pair.terminal.unwrap().kind, ErrorKind::Overflow);
    assert_files(&case, &targets(frame(111), NONE, true));
    assert_eq!(sink.persist(last), Err(SinkError::Closed));
    assert_eq!(evidence.snapshot().journal.terminal, Some(SinkError::Overflow));
}

#[test]
fn poisoned_bridge_journal_is_diagnostic_only_and_never_touches_pair() {
    let case = Case::new("journal-poison"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), NONE);
    let before = evidence.snapshot();
    assert_eq!(std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| sink.poison_journal_for_test())).is_err(), true);
    assert_eq!(sink.persist(ack(baseline(), 1, frame(121))), Err(SinkError::Poisoned));
    let state = evidence.snapshot();
    assert_eq!(state.journal.poisoned, true);
    assert_eq!(state.journal.last_accepted_attempt, None);
    assert_eq!(state.journal.first_refusal, None);
    assert_eq!(state.pair, before.pair);
    assert_eq!(case.read("knob.ctr"), b"017\n");
    assert_eq!(case.read("mixer.state"), SAVED);
}

#[test]
fn poisoned_pair_journal_is_refused_before_mapper_uses_diagnostic_image() {
    let case = Case::new("pair-poison"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), NONE);
    assert_eq!(std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| sink.poison_pair_journal_for_test())).is_err(), true);
    let result = sink.persist(ack(baseline(), 1, frame(122)));
    assert_eq!(matches!(result, Err(SinkError::Pair(PairError { kind: ErrorKind::Poisoned, .. }))), true);
    let state = evidence.snapshot();
    assert_eq!(state.pair.poisoned, true);
    assert_eq!(state.journal.last_accepted_attempt, None);
    assert_eq!(state.pair.last, None);
    assert_eq!(case.read("knob.ctr"), b"017\n");
    assert_eq!(case.read("mixer.state"), SAVED);
}

#[test]
fn cloned_evidence_retains_original_and_committed_images_after_sink_drop() {
    let case = Case::new("retained"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), ALL); let retained = evidence.clone();
    sink.persist(ack(baseline(), 1, frame(131))).unwrap();
    let state = evidence.snapshot(); drop(sink); drop(evidence);
    assert_eq!(retained.snapshot(), state);
    assert_eq!(image_bytes(&retained.snapshot().pair.original.mixer), Some(SAVED));
    assert_files(&case, &targets(frame(131), ALL, true));
    drop(retained);
    assert_files(&case, &targets(frame(131), ALL, true));
}

#[test]
fn newer_route_epoch_can_native_commit_before_closed_bridge_refuses_callback() {
    let case = Case::new("rearm-limit"); case.originals(); case.write("mixer.state", b"invalid");
    let (sink, evidence) = attach(&case, baseline(), NONE);
    let (route, backend) = route(sink, baseline(), Reply::Commit); let b = arm(&route);
    let first = route.create_for_key(b.key, frame(141)).unwrap();
    assert_eq!(matches!(route.dispatch(first).unwrap(), DispatchResult::CommittedSettingsFailed(_)), true);
    let failure = evidence.snapshot();
    let mut newer = backend.lock().unwrap().baseline; newer.key.epoch += 1;
    backend.lock().unwrap().baseline = newer;
    assert_eq!(arm(&route), newer);
    let second = route.create_for_key(newer.key, frame(142)).unwrap();
    assert_eq!(matches!(route.dispatch(second).unwrap(), DispatchResult::CommittedSettingsFailed(_)), true);
    let second_ack = FrameAck { request: second, kernel_sequence: newer.kernel_sequence + 1 };
    let state = evidence.snapshot();
    assert_eq!(backend.lock().unwrap().calls, vec![first, second]);
    assert_eq!(route.diagnostic_snapshot().producer.status.last_ack,
        Some(AckCheckpoint { ack: second_ack, settings: AckSettings::ReturnedErr }));
    assert_eq!(state.journal.first_refusal, Some(Refusal { ack: second_ack, error: SinkError::Closed }));
    assert_eq!(state.journal.last_accepted_attempt, failure.journal.last_accepted_attempt);
    assert_eq!(state.journal.terminal, failure.journal.terminal);
    assert_eq!(state.pair, failure.pair);
    assert_eq!(case.read("mixer.state"), b"invalid");
}

#[test]
fn constructor_baseline_mismatch_is_detected_only_after_native_ack() {
    let case = Case::new("baseline-mismatch"); case.originals();
    let (sink, evidence) = attach(&case, baseline(), NONE); let before = evidence.snapshot();
    let mut different = baseline(); different.key.epoch += 1;
    let (route, backend) = route(sink, different, Reply::Commit); let b = arm(&route);
    let request = route.create_for_key(b.key, frame(151)).unwrap();
    assert_eq!(matches!(route.dispatch(request).unwrap(), DispatchResult::CommittedSettingsFailed(_)), true);
    let known = FrameAck { request, kernel_sequence: b.kernel_sequence + 1 };
    let state = evidence.snapshot();
    assert_eq!(backend.lock().unwrap().calls, vec![request]);
    assert_eq!(route.diagnostic_snapshot().producer.status.last_ack,
        Some(AckCheckpoint { ack: known, settings: AckSettings::ReturnedErr }));
    assert_eq!(state.journal.first_refusal, Some(Refusal { ack: known, error: SinkError::WrongKey }));
    assert_eq!(state.journal.last_accepted_attempt, None);
    assert_eq!(state.pair, before.pair);
}

#[test]
fn worker_join_retains_successful_callback_and_same_settings_evidence() {
    let case = Case::new("worker-success"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), NONE);
    let (entered_tx, entered_rx) = mpsc::sync_channel(1);
    sink.set_hook(Box::new(move |point| if point == HookPoint::AfterPairReturn { entered_tx.send(()).unwrap(); }));
    let (route, backend) = route(sink, baseline(), Reply::Commit);
    let (port, worker) = mixer_transport::channel(route, 4).unwrap();
    let b = port.refresh_baseline().unwrap(); port.confirm_adoption(b, true).unwrap();
    let request = port.enqueue_for_key(b.key, frame(161)).unwrap();
    entered_rx.recv_timeout(Duration::from_secs(10)).unwrap();
    let joined = worker.join().unwrap(); let known = FrameAck { request, kernel_sequence: b.kernel_sequence + 1 };
    assert_eq!(joined.exit(), WorkerExit::Stopped);
    assert_eq!(joined.last_ack(), Some(AckCheckpoint { ack: known, settings: AckSettings::ReturnedOk }));
    assert_eq!(joined.accounting().mailbox.status.last.unwrap().outcome, AttemptOutcome::Committed { settings_written: true });
    assert_eq!(last_attempt(&evidence).ack, known);
    assert_eq!(backend.lock().unwrap().calls, vec![request]);
    let state = evidence.snapshot(); drop(port); drop(joined);
    assert_eq!(evidence.snapshot(), state);
    assert_files(&case, &targets(frame(161), NONE, true));
}

#[test]
fn worker_panic_join_preserves_pair_commit_without_completed_callback() {
    let case = Case::new("worker-panic"); case.originals();
    let (mut sink, evidence) = attach(&case, baseline(), NONE);
    let (entered_tx, entered_rx) = mpsc::sync_channel(1);
    sink.set_hook(Box::new(move |point| if point == HookPoint::AfterPairReturn {
        entered_tx.send(()).unwrap(); panic!("deliberate worker callback gap");
    }));
    let (route, backend) = route(sink, baseline(), Reply::Commit);
    let (port, worker) = mixer_transport::channel(route, 4).unwrap();
    let b = port.refresh_baseline().unwrap(); port.confirm_adoption(b, true).unwrap();
    let request = port.enqueue_for_key(b.key, frame(171)).unwrap();
    entered_rx.recv_timeout(Duration::from_secs(10)).unwrap();
    let joined = worker.join().unwrap(); let known = FrameAck { request, kernel_sequence: b.kernel_sequence + 1 };
    assert_eq!(joined.exit(), WorkerExit::Panicked);
    assert_eq!(joined.last_ack(), Some(AckCheckpoint { ack: known, settings: AckSettings::CallbackArmed }));
    assert_eq!(joined.accounting().route.endpoint_poisoned, true);
    assert_eq!(joined.accounting().route.producer.poisoned, true);
    assert_eq!(joined.accounting().mailbox.status.last.unwrap().outcome, AttemptOutcome::Panicked);
    let state = evidence.snapshot();
    assert_eq!(state.journal.last_accepted_attempt.as_ref().unwrap().phase, AttemptPhase::PairArmed);
    assert_eq!(state.pair.last.as_ref().unwrap().result, TransactionResult::Committed);
    assert_eq!(backend.lock().unwrap().calls, vec![request]);
    drop(port); drop(joined);
    assert_eq!(evidence.snapshot(), state);
    assert_files(&case, &targets(frame(171), NONE, true));
}
