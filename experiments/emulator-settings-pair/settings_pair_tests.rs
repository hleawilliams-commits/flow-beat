// SPDX-License-Identifier: GPL-3.0-only
// Actual Unix operations, confined to the explicit private runner root.
// No default live settings path, application main, native backend, or device.
use super::*;
use crate::mixer_producer::{BaselineReceipt, FrameAck, Key, Producer};
use std::fs::{self, File};
use std::os::unix::fs::{symlink, MetadataExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

static NEXT_CASE: AtomicUsize = AtomicUsize::new(1);

struct Case { path: PathBuf }
impl Case {
    fn new(label: &str) -> Self {
        let root = PathBuf::from(std::env::var_os("AZ_SETTINGS_PAIR_TEST_ROOT")
            .expect("test runner must provide the isolated filesystem root"));
        assert!(root.is_absolute(), "test root must be absolute");
        let meta = fs::symlink_metadata(&root).expect("explicit test root exists");
        assert!(meta.is_dir() && !meta.file_type().is_symlink(), "test root must be a directory");
        let serial = NEXT_CASE.fetch_add(1, Ordering::Relaxed);
        let path = root.join(format!("{}-{}-{}", label, std::process::id(), serial));
        fs::create_dir(&path).expect("fresh exclusive case directory");
        fs::set_permissions(&path, fs::Permissions::from_mode(0o700)).unwrap();
        Self { path }
    }
    fn directory(&self) -> File { File::open(&self.path).unwrap() }
    fn write(&self, name: &str, bytes: &[u8], mode: u32) {
        fs::write(self.path.join(name), bytes).unwrap();
        fs::set_permissions(self.path.join(name), fs::Permissions::from_mode(mode)).unwrap();
    }
    fn originals(&self) {
        self.write("knob.ctr", b"017\n", 0o640);
        self.write("mixer.state", b"{\"synthetic\":\"original\"}\n", 0o600);
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

fn ack(receipt: BaselineReceipt, request_id: usize, fill: u8) -> FrameAck {
    let producer = Producer::new(64).unwrap();
    producer.stage_baseline(receipt).unwrap();
    producer.acknowledge_baseline(receipt).unwrap();
    producer.confirm_released(receipt, true).unwrap();
    let mut request = None;
    for _ in 0..request_id {
        request = Some(producer.create_for_key(receipt.key, [fill; 152]).unwrap());
    }
    FrameAck { request: request.expect("nonzero synthetic request id"),
        kernel_sequence: receipt.kernel_sequence.checked_add(request_id as u64).unwrap() }
}

fn targets() -> PairTargets {
    PairTargets { knob: b"130".to_vec(), mixer: b"{\"synthetic\":\"target\"}\n".to_vec() }
}

fn attach(case: &Case) -> (SettingsPair, EvidenceHandle) {
    SettingsPair::attach(case.directory(), baseline(), [0x31; 16], 0o640).unwrap()
}

fn mode(path: &Path) -> u32 { fs::symlink_metadata(path).unwrap().mode() & 0o7777 }

#[test]
fn present_pair_preserves_original_bytes_modes_and_verifies_both_replacements() {
    let case = Case::new("present"); case.originals();
    let old_knob = fs::metadata(case.path.join("knob.ctr")).unwrap().ino();
    let old_mixer = fs::metadata(case.path.join("mixer.state")).unwrap().ino();
    let (mut pair, evidence) = attach(&case);
    let original = evidence.snapshot().original;
    let committed = pair.commit(ack(baseline(), 1, 1), targets());
    assert!(committed.is_ok());
    assert_eq!(case.read("knob.ctr"), targets().knob);
    assert_eq!(case.read("mixer.state"), targets().mixer);
    assert_eq!(mode(&case.path.join("knob.ctr")), 0o640);
    assert_eq!(mode(&case.path.join("mixer.state")), 0o600);
    assert_ne!(fs::metadata(case.path.join("knob.ctr")).unwrap().ino(), old_knob);
    assert_ne!(fs::metadata(case.path.join("mixer.state")).unwrap().ino(), old_mixer);
    let e = evidence.snapshot();
    assert_eq!(e.original, original);
    assert_ne!(e.current, original);
    assert_eq!(e.next_request_id, 2);
    assert_eq!(e.kernel_sequence, 24);
    assert!(e.terminal.is_none() && !e.poisoned);
    let tx = e.last.unwrap();
    assert_eq!(tx.result, TransactionResult::Committed);
    assert_eq!(tx.before, original);
    assert_eq!(tx.files[0].stage, FileStage::Verified);
    assert_eq!(tx.files[1].stage, FileStage::Verified);
    assert_eq!(tx.files[0].verified, Some(e.current.knob));
    assert_eq!(tx.files[1].verified, Some(e.current.mixer));
    assert_eq!(case.names(), vec!["knob.ctr", "mixer.state"]);
}

#[test]
fn absent_and_empty_are_distinct_and_creation_uses_explicit_mode() {
    let case = Case::new("absent-empty"); case.write("mixer.state", b"", 0o600);
    let (mut pair, evidence) = attach(&case);
    let before = evidence.snapshot();
    assert_eq!(before.original.knob, FileImage::Absent);
    assert!(matches!(&before.original.mixer, FileImage::Present {bytes, ..} if bytes.is_empty()));
    assert!(pair.commit(ack(baseline(), 1, 2), targets()).is_ok());
    assert_eq!(mode(&case.path.join("knob.ctr")), 0o640);
    assert_eq!(mode(&case.path.join("mixer.state")), 0o600);
    assert_eq!(evidence.snapshot().original, before.original);
}

#[test]
fn sequential_acks_and_exact_duplicate_do_not_repeat_mutations() {
    let case = Case::new("duplicates"); case.originals();
    let (mut pair, evidence) = attach(&case);
    let writes = Arc::new(AtomicUsize::new(0)); let observed = writes.clone();
    pair.set_fault_hook(Box::new(move |point| {
        if matches!(point, FaultPoint::BeforeCreate(_)) { observed.fetch_add(1, Ordering::SeqCst); }
        FaultAction::Continue
    }));
    let first_ack = ack(baseline(), 1, 3);
    let first = pair.commit(first_ack, targets()).unwrap();
    let after_first = evidence.snapshot();
    let duplicate = pair.commit(first_ack, targets());
    assert_eq!(duplicate, Ok(first));
    assert_eq!(writes.load(Ordering::SeqCst), 2);
    assert_eq!(evidence.snapshot().current, after_first.current);
    let second = PairTargets { knob: b"129\n".to_vec(), mixer: b"opaque\0second".to_vec() };
    assert!(pair.commit(ack(baseline(), 2, 4), second.clone()).is_ok());
    assert_eq!(writes.load(Ordering::SeqCst), 4);
    assert_eq!(case.read("knob.ctr"), second.knob);
    assert_eq!(case.read("mixer.state"), second.mixer);
    assert_eq!(evidence.snapshot().next_request_id, 3);
    assert_eq!(evidence.snapshot().kernel_sequence, 25);
}

#[test]
fn completed_duplicate_checks_current_files_before_cached_success() {
    let case = Case::new("duplicate-drift"); case.originals();
    let (mut pair, evidence) = attach(&case);
    let same = ack(baseline(), 1, 5); pair.commit(same, targets()).unwrap();
    case.write("mixer.state", b"external-change", 0o600);
    let result = pair.commit(same, targets());
    assert!(result.is_err());
    assert!(evidence.snapshot().terminal.is_some());
    assert_eq!(case.read("mixer.state"), b"external-change");
    assert_eq!(case.read("knob.ctr"), targets().knob);
}

#[test]
fn altered_duplicate_payload_or_target_closes_without_replacement() {
    for variant in 0..4 {
        let case = Case::new("duplicate-conflict"); case.originals();
        let (mut pair, evidence) = attach(&case);
        let original_ack = ack(baseline(), 1, 6); pair.commit(original_ack, targets()).unwrap();
        let old = evidence.snapshot();
        let mut next = original_ack; let mut images = targets();
        match variant {
            0 => next = ack(baseline(), 1, 7),
            1 => next.kernel_sequence += 1,
            2 => images.knob.push(0),
            _ => images.mixer.push(0),
        }
        assert!(pair.commit(next, images).is_err());
        let e = evidence.snapshot();
        assert!(e.terminal.is_some());
        assert_eq!(e.current, old.current);
        assert_eq!(e.last, old.last);
        assert_eq!(case.read("knob.ctr"), targets().knob);
        assert_eq!(case.read("mixer.state"), targets().mixer);
    }
}

#[test]
fn full_key_request_order_and_kernel_sequence_are_checked_before_writes() {
    for variant in 0..7 {
        let case = Case::new("ack-gates"); case.originals();
        let (mut pair, evidence) = attach(&case); let before = evidence.snapshot();
        let mut receipt = baseline();
        match variant {
            0 => receipt.key.instance[1] += 1,
            1 => receipt.key.run_id += 1,
            2 => receipt.key.epoch += 1,
            3 => receipt.key.producer_id += 1,
            _ => (),
        }
        let mut wrong = ack(receipt, if variant == 4 { 2 } else { 1 }, 8);
        if variant == 5 { wrong.kernel_sequence -= 1; }
        if variant == 6 { wrong.kernel_sequence += 1; }
        let rejected = pair.commit(wrong, targets());
        assert!(rejected.is_err());
        assert!(evidence.snapshot().terminal.is_some());
        assert_eq!(evidence.snapshot().current, before.current);
        assert_eq!(case.names(), vec!["knob.ctr", "mixer.state"]);
        assert_eq!(case.read("knob.ctr"), b"017\n");
        assert_eq!(case.read("mixer.state"), b"{\"synthetic\":\"original\"}\n");
    }
}

#[test]
fn old_ack_after_later_commit_is_not_a_cached_replay() {
    let case = Case::new("old-ack"); case.originals();
    let (mut pair, evidence) = attach(&case);
    pair.commit(ack(baseline(), 1, 9), targets()).unwrap();
    pair.commit(ack(baseline(), 2, 10), targets()).unwrap();
    let last = evidence.snapshot();
    assert!(pair.commit(ack(baseline(), 1, 9), targets()).is_err());
    assert!(evidence.snapshot().terminal.is_some());
    assert_eq!(evidence.snapshot().last, last.last);
    assert_eq!(evidence.snapshot().current, last.current);
    let after_closed = pair.commit(ack(baseline(), 3, 0x44), targets());
    assert!(after_closed.is_err());
    assert_eq!(evidence.snapshot().current, last.current);
}

#[test]
fn oversized_targets_are_rejected_without_partial_file_effects() {
    for index in 0..2 {
        let case = Case::new("target-bound"); case.originals();
        let (mut pair, evidence) = attach(&case); let before = evidence.snapshot();
        let mut images = targets();
        if index == 0 { images.knob = vec![0x41; MAX_FILE_BYTES + 1]; }
        else { images.mixer = vec![0x42; MAX_FILE_BYTES + 1]; }
        assert!(pair.commit(ack(baseline(), 1, 11), images).is_err());
        assert!(evidence.snapshot().terminal.is_some());
        assert_eq!(evidence.snapshot().current, before.current);
        assert_eq!(case.names(), vec!["knob.ctr", "mixer.state"]);
    }
}

#[test]
fn attach_refuses_symlinks_hardlinks_nonregular_special_modes_and_oversize() {
    for variant in 0..5 {
        let case = Case::new("invalid-file"); case.originals();
        fs::remove_file(case.path.join("knob.ctr")).unwrap();
        match variant {
            0 => { case.write("symlink-target", b"original-target", 0o600);
                   symlink("symlink-target", case.path.join("knob.ctr")).unwrap(); },
            1 => { case.write("hardlink-target", b"hardlink", 0o600);
                   fs::hard_link(case.path.join("hardlink-target"), case.path.join("knob.ctr")).unwrap(); },
            2 => fs::create_dir(case.path.join("knob.ctr")).unwrap(),
            3 => case.write("knob.ctr", &vec![0x61; MAX_FILE_BYTES + 1], 0o600),
            _ => case.write("knob.ctr", b"special", 0o1600),
        }
        // This launcher preserves sticky but masks setuid/setgid on created files.
        // Those two bits are tested separately through actual metadata validation.
        let special_mode = if variant == 4 { Some(0o1600) } else { None };
        if let Some(expected) = special_mode {
            assert_eq!(mode(&case.path.join("knob.ctr")), expected, "actual special-mode fixture precondition");
        }
        let names = case.names();
        let rejected = SettingsPair::attach(case.directory(), baseline(), [0x32; 16], 0o600).is_err();
        if let Some(expected) = special_mode {
            assert_eq!(mode(&case.path.join("knob.ctr")), expected, "special-mode fixture changed during attach");
        }
        assert!(rejected);
        assert_eq!(case.names(), names);
        assert_eq!(case.read("mixer.state"), b"{\"synthetic\":\"original\"}\n");
        if variant == 0 { assert_eq!(case.read("symlink-target"), b"original-target"); }
    }
}

#[test]
fn attach_rejects_invalid_baseline_namespace_mode_and_directory() {
    for variant in 0..7 {
        let case = Case::new("invalid-attach"); case.originals();
        let mut base = baseline(); let mut namespace = [0x33; 16]; let mut absent_mode = 0o600;
        match variant {
            0 => base.key.instance = [0, 0],
            1 => base.key.run_id = 0,
            2 => base.key.producer_id = 0,
            3 => namespace = [0; 16],
            4 => absent_mode = 0o4600,
            5 => base.key.epoch = 0,
            _ => (),
        }
        let file = if variant == 6 { File::open(case.path.join("knob.ctr")).unwrap() } else { case.directory() };
        assert!(SettingsPair::attach(file, base, namespace, absent_mode).is_err());
        assert_eq!(case.read("knob.ctr"), b"017\n");
    }
}

#[test]
fn fresh_commit_refuses_external_content_identity_mode_or_presence_drift() {
    for variant in 0..4 {
        let case = Case::new("external-drift"); case.originals();
        let (mut pair, evidence) = attach(&case); let before = evidence.snapshot();
        match variant {
            0 => case.write("knob.ctr", b"external", 0o640),
            1 => { case.write("external-replacement", b"017\n", 0o640);
                   fs::rename(case.path.join("external-replacement"), case.path.join("knob.ctr")).unwrap(); },
            2 => fs::set_permissions(case.path.join("knob.ctr"), fs::Permissions::from_mode(0o600)).unwrap(),
            _ => fs::remove_file(case.path.join("knob.ctr")).unwrap(),
        }
        assert!(pair.commit(ack(baseline(), 1, 12), targets()).is_err());
        assert!(evidence.snapshot().terminal.is_some());
        assert_eq!(evidence.snapshot().current, before.current);
        assert_eq!(case.read("mixer.state"), b"{\"synthetic\":\"original\"}\n");
        if variant == 0 { assert_eq!(case.read("knob.ctr"), b"external"); }
    }
}

#[test]
fn exclusive_temp_collision_is_retained_and_never_truncated_or_unlinked() {
    let case = Case::new("temp-collision"); case.originals();
    let (mut pair, evidence) = attach(&case);
    let root = case.path.clone(); let observer = evidence.clone();
    let collision = Arc::new(Mutex::new(None)); let recorded = collision.clone();
    pair.set_fault_hook(Box::new(move |point| {
        if point == FaultPoint::BeforeCreate(FileKind::Knob) {
            let name = observer.snapshot().last.unwrap().files[0].temp_name.clone().unwrap();
            fs::write(root.join(&name), b"external-collision").unwrap();
            *recorded.lock().unwrap() = Some(name);
        }
        FaultAction::Continue
    }));
    let result = pair.commit(ack(baseline(), 1, 13), targets());
    let name = collision.lock().unwrap().clone().unwrap();
    assert!(result.is_err());
    assert_eq!(case.read(&name), b"external-collision", "collision must survive refused creation");
    assert_eq!(case.read("knob.ctr"), b"017\n");
    let tx = evidence.snapshot().last.unwrap();
    assert_eq!(tx.files[0].stage, FileStage::CreateArmed);
    assert_eq!(tx.files[0].temp_identity, None);
    drop(pair); drop(evidence);
    assert_eq!(case.read(&name), b"external-collision");
}

#[test]
fn boundary_failures_retain_exact_armed_stage_and_never_claim_commit() {
    let boundaries = [
        (FaultPoint::BeforeCreate(FileKind::Knob), FileStage::CreateArmed),
        (FaultPoint::AfterCreate(FileKind::Knob), FileStage::CreateArmed),
        (FaultPoint::BeforeWrite(FileKind::Knob), FileStage::WriteArmed),
        (FaultPoint::AfterWrite(FileKind::Knob), FileStage::WriteArmed),
        (FaultPoint::BeforeMode(FileKind::Knob), FileStage::ModeArmed),
        (FaultPoint::AfterMode(FileKind::Knob), FileStage::ModeArmed),
        (FaultPoint::BeforeFileSync(FileKind::Knob), FileStage::FileSyncArmed),
        (FaultPoint::AfterFileSync(FileKind::Knob), FileStage::FileSyncArmed),
        (FaultPoint::BeforeRename(FileKind::Knob), FileStage::RenameArmed),
        (FaultPoint::AfterRename(FileKind::Knob), FileStage::RenameArmed),
        (FaultPoint::BeforeDirectorySync(FileKind::Knob), FileStage::DirectorySyncArmed),
        (FaultPoint::AfterDirectorySync(FileKind::Knob), FileStage::DirectorySyncArmed),
        (FaultPoint::BeforeReadback(FileKind::Knob), FileStage::ReadbackArmed),
        (FaultPoint::AfterReadback(FileKind::Knob), FileStage::ReadbackArmed),
    ];
    for (at, stage) in boundaries {
        let case = Case::new("boundary-failure"); case.originals();
        let (mut pair, evidence) = attach(&case);
        pair.set_fault_hook(Box::new(move |point| if point == at { FaultAction::Fail(libc::EIO) } else { FaultAction::Continue }));
        let same = ack(baseline(), 1, 14);
        let failure = pair.commit(same, targets());
        assert!(failure.is_err());
        let e = evidence.snapshot(); let tx = e.last.clone().unwrap();
        assert_eq!(tx.files[0].stage, stage);
        assert_eq!(tx.files[1].stage, FileStage::Untouched);
        assert!(matches!(tx.result, TransactionResult::Failed(_)));
        assert!(e.terminal.is_some());
        assert_eq!(e.next_request_id, 1);
        assert_eq!(e.kernel_sequence, 23);
        assert_eq!(case.read("mixer.state"), b"{\"synthetic\":\"original\"}\n");
        pair.set_fault_hook(Box::new(|_| panic!("failed duplicate must not retry filesystem work")));
        assert_eq!(pair.commit(same, targets()), failure);
        assert!(pair.commit(ack(baseline(), 2, 15), targets()).is_err());
        assert_eq!(evidence.snapshot().last, e.last);
    }
}

#[test]
fn second_file_failure_retains_first_verified_replacement_without_rollback() {
    let case = Case::new("partial-pair"); case.originals();
    let (mut pair, evidence) = attach(&case); let original = evidence.snapshot().original;
    pair.set_fault_hook(Box::new(|point| if point == FaultPoint::BeforeCreate(FileKind::Mixer) {
        FaultAction::Fail(libc::ENOSPC)
    } else { FaultAction::Continue }));
    let result = pair.commit(ack(baseline(), 1, 16), targets());
    assert!(result.is_err());
    let e = evidence.snapshot(); let tx = e.last.unwrap();
    assert_eq!(tx.files[0].stage, FileStage::Verified);
    assert_eq!(tx.files[1].stage, FileStage::CreateArmed);
    assert_ne!(e.current.knob, original.knob);
    assert_eq!(e.current.mixer, original.mixer);
    assert_eq!(case.read("knob.ctr"), targets().knob);
    assert_eq!(case.read("mixer.state"), b"{\"synthetic\":\"original\"}\n");
    assert!(matches!(tx.result, TransactionResult::Failed(_)));
    assert_eq!(e.original, original);
}

#[test]
fn after_rename_failure_records_visible_change_without_verified_success() {
    let case = Case::new("rename-uncertain"); case.originals();
    let (mut pair, evidence) = attach(&case); let original = evidence.snapshot().original;
    pair.set_fault_hook(Box::new(|point| if point == FaultPoint::AfterRename(FileKind::Knob) {
        FaultAction::Fail(libc::EIO)
    } else { FaultAction::Continue }));
    assert!(pair.commit(ack(baseline(), 1, 17), targets()).is_err());
    let e = evidence.snapshot(); let tx = e.last.unwrap();
    assert_eq!(tx.files[0].stage, FileStage::RenameArmed);
    assert_eq!(tx.files[0].verified, None);
    assert_eq!(e.current, original);
    assert_eq!(case.read("knob.ctr"), targets().knob);
    assert_eq!(case.read("mixer.state"), b"{\"synthetic\":\"original\"}\n");
    assert!(!case.path.join(tx.files[0].temp_name.as_ref().unwrap()).exists());
}

#[test]
fn readback_drift_refuses_success_and_preserves_observed_external_file() {
    let case = Case::new("readback-drift"); case.originals();
    let (mut pair, evidence) = attach(&case); let path = case.path.join("knob.ctr");
    pair.set_fault_hook(Box::new(move |point| {
        if point == FaultPoint::BeforeReadback(FileKind::Knob) { fs::write(&path, b"external-readback").unwrap(); }
        FaultAction::Continue
    }));
    let result = pair.commit(ack(baseline(), 1, 18), targets());
    assert!(result.is_err());
    let e = evidence.snapshot(); let tx = e.last.unwrap();
    assert_eq!(tx.files[0].stage, FileStage::ReadbackArmed);
    assert_eq!(tx.files[0].verified, None);
    assert!(matches!(tx.result, TransactionResult::Failed(_)));
    assert_eq!(case.read("knob.ctr"), b"external-readback");
    assert_eq!(case.read("mixer.state"), b"{\"synthetic\":\"original\"}\n");
}

#[test]
fn final_pair_verification_rejects_drift_after_both_individual_readbacks() {
    let case = Case::new("final-pair-drift"); case.originals();
    let (mut pair, evidence) = attach(&case); let path = case.path.join("knob.ctr");
    pair.set_fault_hook(Box::new(move |point| {
        if point == FaultPoint::BeforePairVerify { fs::write(&path, b"late-external").unwrap(); }
        FaultAction::Continue
    }));
    let result = pair.commit(ack(baseline(), 1, 19), targets());
    assert!(result.is_err());
    let e = evidence.snapshot(); let tx = e.last.unwrap();
    assert_eq!(tx.files[0].stage, FileStage::Verified);
    assert_eq!(tx.files[1].stage, FileStage::Verified);
    assert!(matches!(tx.result, TransactionResult::Failed(_)));
    assert_eq!(e.next_request_id, 1);
    assert_eq!(case.read("knob.ctr"), b"late-external");
}

#[test]
fn panic_after_rename_retains_pending_and_blocks_all_later_writes() {
    let case = Case::new("panic-rename"); case.originals();
    let (mut pair, evidence) = attach(&case); let same = ack(baseline(), 1, 20);
    pair.set_fault_hook(Box::new(|point| if point == FaultPoint::AfterRename(FileKind::Knob) {
        FaultAction::Panic
    } else { FaultAction::Continue }));
    let panic = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| pair.commit(same, targets())));
    assert!(panic.is_err());
    let pending = evidence.snapshot(); let tx = pending.last.clone().unwrap();
    assert_eq!(tx.result, TransactionResult::Pending);
    assert_eq!(tx.files[0].stage, FileStage::RenameArmed);
    assert!(!pending.poisoned);
    assert_eq!(case.read("knob.ctr"), targets().knob);
    pair.set_fault_hook(Box::new(|_| panic!("pending transaction must not retry")));
    assert!(pair.commit(same, targets()).is_err());
    assert!(pair.commit(ack(baseline(), 2, 21), targets()).is_err());
    assert!(evidence.snapshot().terminal.is_some());
    assert_eq!(evidence.snapshot().last, pending.last);
    assert_eq!(case.read("mixer.state"), b"{\"synthetic\":\"original\"}\n");
}

#[test]
fn pending_evidence_is_observable_without_lock_reentry_and_survives_writer_drop() {
    let case = Case::new("evidence-lifetime"); case.originals();
    let (mut pair, evidence) = attach(&case); let observer = evidence.clone();
    let saw_pending = Arc::new(AtomicUsize::new(0)); let seen = saw_pending.clone();
    pair.set_fault_hook(Box::new(move |point| {
        if point == FaultPoint::AfterIntent {
            let e = observer.snapshot();
            assert_eq!(e.last.unwrap().result, TransactionResult::Pending);
            seen.fetch_add(1, Ordering::SeqCst);
            return FaultAction::Fail(libc::EIO);
        }
        FaultAction::Continue
    }));
    assert!(pair.commit(ack(baseline(), 1, 22), targets()).is_err());
    assert_eq!(saw_pending.load(Ordering::SeqCst), 1);
    let before = evidence.snapshot(); drop(pair);
    assert_eq!(evidence.snapshot(), before);
    assert_eq!(case.read("knob.ctr"), b"017\n");
    assert_eq!(case.names(), vec!["knob.ctr", "mixer.state"]);
}

#[test]
fn poisoned_journal_is_flagged_diagnostic_only_and_cannot_write() {
    let case = Case::new("poison"); case.originals();
    let (mut pair, evidence) = attach(&case); let before = evidence.snapshot();
    let panic = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| pair.poison_journal_for_test()));
    assert!(panic.is_err());
    let e = evidence.snapshot();
    assert!(e.poisoned);
    assert_eq!(e.original, before.original);
    assert_eq!(e.current, before.current);
    assert!(pair.commit(ack(baseline(), 1, 23), targets()).is_err());
    assert!(evidence.snapshot().poisoned);
    assert_eq!(case.read("knob.ctr"), b"017\n");
}

#[test]
fn retained_directory_descriptor_does_not_redirect_to_replacement_pathname() {
    let case = Case::new("directory-anchor"); case.originals();
    let (mut pair, evidence) = attach(&case);
    let moved = case.path.with_file_name(format!("{}-moved", case.path.file_name().unwrap().to_str().unwrap()));
    fs::rename(&case.path, &moved).unwrap();
    fs::create_dir(&case.path).unwrap();
    case.write("knob.ctr", b"replacement-directory", 0o600);
    case.write("mixer.state", b"replacement-directory", 0o600);
    let result = pair.commit(ack(baseline(), 1, 24), targets());
    assert!(result.is_ok());
    assert_eq!(case.read("knob.ctr"), b"replacement-directory");
    assert_eq!(case.read("mixer.state"), b"replacement-directory");
    assert_eq!(fs::read(moved.join("knob.ctr")).unwrap(), targets().knob);
    assert_eq!(fs::read(moved.join("mixer.state")).unwrap(), targets().mixer);
    assert_eq!(evidence.snapshot().last.unwrap().result, TransactionResult::Committed);
}

#[test]
fn same_bytes_newly_authorized_are_accepted_without_proving_ui_provenance() {
    let case = Case::new("opaque-authority"); case.originals();
    let (mut pair, evidence) = attach(&case);
    let unchanged = PairTargets { knob: case.read("knob.ctr"), mixer: case.read("mixer.state") };
    assert!(pair.commit(ack(baseline(), 1, 0xff), unchanged.clone()).is_ok());
    assert_eq!(case.read("knob.ctr"), unchanged.knob);
    assert_eq!(case.read("mixer.state"), unchanged.mixer);
    assert_eq!(evidence.snapshot().last.unwrap().result, TransactionResult::Committed);
}

#[test]
fn finite_sequence_and_request_capacity_never_wrap_or_reopen() {
    let rejected_case = Case::new("sequence-max-attach"); rejected_case.originals();
    let mut exhausted = baseline(); exhausted.kernel_sequence = u64::MAX;
    assert!(SettingsPair::attach(rejected_case.directory(), exhausted, [0x34; 16], 0o600).is_err());
    assert_eq!(rejected_case.read("knob.ctr"), b"017\n");

    let final_case = Case::new("sequence-last"); final_case.originals();
    let mut last_base = baseline(); last_base.kernel_sequence = u64::MAX - 1;
    let (mut pair, evidence) = SettingsPair::attach(final_case.directory(), last_base, [0x35; 16], 0o600).unwrap();
    let final_ack = ack(last_base, 1, 25);
    let completed = pair.commit(final_ack, targets()).unwrap();
    assert_eq!(evidence.snapshot().kernel_sequence, u64::MAX);
    assert_eq!(evidence.snapshot().terminal.as_ref().map(|e| e.kind), Some(ErrorKind::Overflow));
    assert_eq!(pair.commit(final_ack, targets()), Ok(completed));
    assert!(pair.commit(ack(baseline(), 2, 26), targets()).is_err());
    assert_eq!(evidence.snapshot().kernel_sequence, u64::MAX);

    let id_case = Case::new("request-id-exhaustion"); id_case.originals();
    let (mut capped, capped_evidence) = attach(&id_case);
    // Test-only private journal seed exercises an otherwise infeasibly long run.
    // No production setter/adoption/reset API is introduced.
    capped.shared.journal.lock().unwrap().next_request_id = u64::MAX;
    let error = capped.commit(ack(baseline(), 1, 27), targets()).unwrap_err();
    assert_eq!(error.kind, ErrorKind::Overflow);
    assert_eq!(capped_evidence.snapshot().next_request_id, u64::MAX);
    assert_eq!(capped_evidence.snapshot().last, None);
    assert_eq!(id_case.read("knob.ctr"), b"017\n");
}

#[test]
fn copied_metadata_rejects_each_special_bit_without_privileged_file_creation() {
    let case = Case::new("copied-special-metadata"); case.originals();
    let (_pair, evidence) = attach(&case);
    let ordinary = match evidence.snapshot().original.knob {
        FileImage::Present { identity, .. } => identity,
        FileImage::Absent => panic!("ordinary fixture must exist"),
    };
    assert!(validate_identity(&ordinary).is_ok());
    for bit in [0o4000, 0o2000, 0o1000] {
        let mut supplied = ordinary.clone();
        supplied.mode |= bit;
        let rejection = validate_identity(&supplied).unwrap_err();
        assert_eq!(rejection.kind, ErrorKind::InvalidFile);
        assert_eq!(mode(&case.path.join("knob.ctr")), 0o640);
    }
    // Only the copied value carried these bits. This does not claim a real
    // attach with setuid/setgid files, external writer exclusion, or recovery.
    assert_eq!(case.read("knob.ctr"), b"017\n");
}
