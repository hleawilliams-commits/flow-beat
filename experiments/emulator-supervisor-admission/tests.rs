// SPDX-License-Identifier: GPL-3.0-only
//! New-only offline composition checks: actual retained sentinel Child, frozen
//! startup/lifecycle/worker and private files; synthetic backend/poll failures.
//! Every semantic oracle follows explicit worker finish and actual child wait.
use super::*;
use crate::fixed_session::Config;
use crate::mixer_producer::{AckCheckpoint, AckSettings, BaselineReceipt, Envelope, Frame, FrameAck, Key};
use crate::mixer_route::{BackendOutcome, MixerBackend};
use crate::mixer_transport::{AttemptOutcome, MixerPort, StopReason, TransportError};
use crate::owned_lifecycle::{FinishStatus, LifecycleControl, LifecyclePhase};
use crate::owned_startup::{FinishResult, OwnedStartup, StartupHandle};
use crate::settings_mapper::Selection;
use crate::settings_sink::HookPoint as SinkPoint;
use crate::supervisor_observation::{ControllerEvent, Disposition, Journal};
use std::fs::{self, File};
use std::io::{self, Read, Write};
use std::os::fd::OwnedFd;
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex, mpsc};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread;
use std::time::{Duration, Instant};

const WAIT: Duration = Duration::from_secs(5);
const EXIT_CODE: i32 = 23;
const SAVED: &[u8] = b"{\"10\": 99, \"22\": 160, \"26\": 7, \"27\": 8}";
static SERIAL: AtomicUsize = AtomicUsize::new(1);
type TestResult<T = ()> = Result<T, String>;
type SinkHook = Box<dyn FnMut(SinkPoint) + Send>;

fn io<T>(r: io::Result<T>) -> TestResult<T> { r.map_err(|e| e.to_string()) }
struct Case { path: PathBuf }
impl Case {
    fn new(label: &str) -> TestResult<Self> {
        let root = PathBuf::from(std::env::var_os("AZ_SUPERVISOR_ADMISSION_TEST_ROOT")
            .ok_or("runner must supply private fixture root")?);
        let metadata = io(fs::symlink_metadata(&root))?;
        if !root.is_absolute() || !metadata.is_dir() || metadata.file_type().is_symlink() {
            return Err("fixture root must be an explicit real absolute directory".into());
        }
        let path = root.join(format!("{label}-{}-{}", std::process::id(),
            SERIAL.fetch_add(1, Ordering::Relaxed)));
        io(fs::create_dir(&path))?;
        io(fs::set_permissions(&path, fs::Permissions::from_mode(0o700)))?;
        for (name, bytes) in [("knob.ctr", &b"017\n"[..]), ("mixer.state", SAVED)] {
            io(fs::write(path.join(name), bytes))?;
            io(fs::set_permissions(path.join(name), fs::Permissions::from_mode(0o600)))?;
        }
        Ok(Self { path })
    }
    fn directory(&self) -> TestResult<File> { io(File::open(&self.path)) }
    fn read(&self, name: &str) -> TestResult<Vec<u8>> { io(fs::read(self.path.join(name))) }
    fn unchanged(&self) -> TestResult<bool> {
        Ok(self.read("knob.ctr")? == b"017\n" && self.read("mixer.state")? == SAVED)
    }
}
fn config() -> Config {
    Config { capacity: 4, namespace: [0x59; 16], absent_mode: 0o640,
        selection: Selection { selector: true, fx_time: true } }
}
fn frame(salt: u8) -> Frame {
    std::array::from_fn(|i| (i as u8).wrapping_mul(71).wrapping_add(salt))
}
fn baseline() -> BaselineReceipt {
    BaselineReceipt { key: Key { instance: [0x2468, 0x1357], run_id: 13,
        epoch: 17, producer_id: 19 }, frame: frame(29), kernel_sequence: 23 }
}
struct Block { entered: mpsc::Sender<()>, proceed: mpsc::Receiver<()> }
impl Block {
    fn hit(&self) {
        let _ = self.entered.send(());
        self.proceed.recv_timeout(Duration::from_secs(15)).expect("fixture barrier release deadline");
    }
}
struct Gate { entered: mpsc::Receiver<()>, release: mpsc::Sender<()> }
fn barrier() -> (Block, Gate) {
    let (entered, seen) = mpsc::channel();
    let (release, proceed) = mpsc::channel();
    (Block { entered, proceed }, Gate { entered: seen, release })
}
#[derive(Default)]
struct Counts { baselines: AtomicUsize, submits: Mutex<Vec<Envelope>>, drops: AtomicUsize }
struct Backend { counts: Arc<Counts>, block: Option<Block>, sequence: u64 }
impl MixerBackend for Backend {
    fn baseline(&mut self) -> Result<BaselineReceipt, String> {
        self.counts.baselines.fetch_add(1, Ordering::SeqCst);
        Ok(baseline())
    }
    fn submit(&mut self, request: Envelope) -> BackendOutcome {
        self.counts.submits.lock().unwrap().push(request);
        if let Some(block) = self.block.take() { block.hit(); }
        self.sequence += 1;
        BackendOutcome::Committed(FrameAck { request, kernel_sequence: self.sequence })
    }
}
impl Drop for Backend {
    fn drop(&mut self) { self.counts.drops.fetch_add(1, Ordering::SeqCst); }
}
struct ChildBeforeTransfer(Option<Child>);
impl Drop for ChildBeforeTransfer {
    fn drop(&mut self) {
        if let Some(child) = &mut self.0 {
            eprintln!("FIXTURE_ABORT before child transfer");
            let _ = child.kill(); let _ = child.wait();
        }
    }
}
fn sentinel() -> TestResult<ChildBeforeTransfer> {
    let (mut ready, output) = io(UnixStream::pair())?;
    io(ready.set_read_timeout(Some(WAIT)))?;
    let child = io(Command::new("/bin/sh").arg("-c")
        .arg("printf 'READY\\n'; IFS= read -r sentinel || exit 90; [ \"$sentinel\" = G ] || exit 91; exit 23")
        .stdin(Stdio::piped()).stdout(Stdio::from(OwnedFd::from(output)))
        .stderr(Stdio::null()).spawn())?;
    let owner = ChildBeforeTransfer(Some(child));
    let mut bytes = [0u8; 6]; io(ready.read_exact(&mut bytes))?;
    if &bytes != b"READY\n" { return Err("private child readiness mismatch".into()); }
    Ok(owner)
}
struct Fixture {
    case: Case,
    startup: OwnedStartup<Child>,
    handle: StartupHandle<Child>,
    control: Option<Arc<dyn LifecycleControl>>,
    counts: Arc<Counts>,
    child_id: u32,
    releases: Vec<mpsc::Sender<()>>,
    cleaned: bool,
}
impl Fixture {
    fn new(label: &str, block: Option<Block>, hook: Option<SinkHook>,
        releases: Vec<mpsc::Sender<()>>) -> TestResult<Self>
    {
        let case = Case::new(label)?;
        let directory = case.directory()?;
        let mut child = sentinel()?;
        let child_id = child.0.as_ref().unwrap().id();
        let counts = Arc::new(Counts::default());
        let backend = Backend { counts: counts.clone(), block, sequence: baseline().kernel_sequence };
        let (startup, handle) = OwnedStartup::retain(Box::new(backend), directory, child.0.take());
        // Custody guard exists before start/attachment/worker creation.
        let mut fixture = Self { case, startup, handle, control: None, counts,
            child_id, releases, cleaned: false };
        if let Some(hook) = hook { fixture.startup.set_sink_hook_for_test(hook); }
        fixture.control = Some(fixture.startup.start(config()).map_err(|e| format!("start: {e:?}"))?);
        let port = fixture.port();
        let grant = port.refresh_baseline().map_err(|e| format!("baseline: {e:?}"))?;
        port.confirm_adoption(grant, true).map_err(|e| format!("adopt: {e:?}"))?;
        Ok(fixture)
    }
    fn port(&self) -> Arc<MixerPort> { self.control.as_ref().unwrap().port() }
    fn submitted(&self) -> Vec<Envelope> { self.counts.submits.lock().unwrap().clone() }
    fn child_state(&self) -> TestResult<(bool, bool, u32)> {
        self.handle.with_supervisor_for_test(|child|
            Ok((io(child.try_wait())?.is_none(), child.stdin.is_some(), child.id())))
    }
    fn send_child_exit(&self) -> TestResult {
        self.handle.with_supervisor_for_test(|child| {
            if let Some(mut input) = child.stdin.take() { io(input.write_all(b"G\n"))?; }
            Ok(())
        })
    }
    fn release_all(&self) {
        for release in &self.releases { let _ = release.send(()); }
    }
    fn finish(&mut self) -> TestResult<FinishResult> {
        self.release_all();
        let finish = self.handle.finish();
        // This is only the fixture's sentinel command, never production stop.
        self.send_child_exit()?;
        let status = self.handle.with_supervisor_for_test(|child| -> TestResult<_> {
            let deadline = Instant::now() + WAIT;
            loop {
                if io(child.try_wait())?.is_some() { break; }
                if Instant::now() >= deadline { return Err("fixture child exit deadline".into()); }
                thread::sleep(Duration::from_millis(5));
            }
            io(child.wait())
        })?;
        self.cleaned = true;
        if status.code() != Some(EXIT_CODE) { return Err(format!("private child exit: {status}")); }
        Ok(finish)
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        if self.cleaned { return; }
        eprintln!("FIXTURE_ABORT explicit cleanup after fixture failure");
        self.release_all();
        if let Some(control) = &self.control { let _ = control.finish(); }
        else { let _ = self.handle.finish(); }
        self.handle.with_supervisor_for_test(|child| {
            let _ = child.kill(); let _ = child.wait();
        });
    }
}
fn admission(j: &AdmissionJournal) -> (bool, Option<CloseCause>, Option<(bool, Option<StopReason>)>) {
    (j.bound(), j.first_cause(), j.latch().map(|l| (l.armed(), l.returned())))
}
fn expected_admission(cause: CloseCause)
    -> (bool, Option<CloseCause>, Option<(bool, Option<StopReason>)>)
{
    (true, Some(cause), Some((true, Some(StopReason::Requested))))
}
fn retained_before_finish(f: &Fixture) -> bool {
    let s = f.handle.snapshot();
    s.supervisor_retained && s.owner_retained && s.route_retained
        && !s.journal.finish_requested
        && s.lifecycle.as_ref().is_some_and(|v| v.as_ref().is_ok_and(|l|
            l.phase == LifecyclePhase::Running && l.joined.is_none()))
        && f.counts.drops.load(Ordering::SeqCst) == 0
}
fn finished() -> FinishResult { FinishResult::Lifecycle(FinishStatus::RecoveryRequired) }

#[test]
fn known_loss_closes_real_admission_before_poll() -> TestResult {
    let mut f = Fixture::new("loss-order", None, None, vec![])?;
    let port = f.port(); let clone = port.clone();
    let event = ControllerEvent::ReaderEof { copied: 7 };
    let mut poll = Journal::new(); let mut close = AdmissionJournal::new();
    let mut inside = None;
    let result = f.handle.with_supervisor_observation(&mut poll, &mut close, |observer| {
        observer.observe_with_for_test(event, |child| {
            inside = Some((clone.inspect().map(|s| s.stop_reason),
                clone.enqueue_for_key(baseline().key, frame(31)), child.stdin.is_some()));
            child.try_wait()
        }).ok()
    });
    let held = retained_before_finish(&f); let child = f.child_state()?;
    let observed = (result, inside, admission(&close), poll.child_id(),
        held, child, f.submitted(), f.case.unchanged()?);
    let finish = f.finish()?;
    assert_eq!((observed, finish),
        ((Ok(Some(Disposition::Hold)), Some((Ok(StopReason::Requested),
          Err(TransportError::Terminal), true)), expected_admission(CloseCause::ControllerLoss(event)),
          Some(f.child_id), true, (true, true, f.child_id), vec![], true), finished()),
        "oracle known loss closes real admission before actual poll");
    Ok(())
}

#[test]
fn returned_poll_error_closes_real_port_without_child_stop() -> TestResult {
    let mut f = Fixture::new("poll-error", None, None, vec![])?;
    let port = f.port(); let mut poll = Journal::new(); let mut close = AdmissionJournal::new();
    let mut inside = None;
    let result = f.handle.with_supervisor_observation(&mut poll, &mut close, |observer| {
        let returned = observer.observe_with_for_test(ControllerEvent::None, |_| {
            Err(io::Error::from_raw_os_error(5)) // Scripted; no OS poll-error claim.
        }).ok();
        let raw = observer.poll_journal().first_poll_error()
            .and_then(|p| p.returned()).and_then(|r| r.as_ref().err())
            .map(|e| e.raw_os_error());
        inside = Some((port.inspect().map(|s| s.stop_reason), raw,
            observer.poll_journal().exited()));
        returned
    });
    let observed = (result, inside, admission(&close), retained_before_finish(&f),
        f.child_state()?, f.submitted(), f.case.unchanged()?);
    let finish = f.finish()?;
    assert_eq!((observed, finish),
        ((Ok(Some(Disposition::Hold)), Some((Ok(StopReason::Requested), Some(Some(5)), false)),
          expected_admission(CloseCause::ObservedHold), true,
          (true, true, f.child_id), vec![], true), finished()),
        "oracle retained poll error closes real port without stopping child");
    Ok(())
}

#[test]
fn poll_unwind_closes_real_port_before_escape() -> TestResult {
    let mut f = Fixture::new("poll-unwind", None, None, vec![])?;
    let port = f.port(); let mut poll = Journal::new(); let mut close = AdmissionJournal::new();
    let result = f.handle.with_supervisor_observation(&mut poll, &mut close, |observer| {
        let caught = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let _ = observer.observe_with_for_test(ControllerEvent::None, |_| {
                panic!("scripted admission poll unwind");
            });
        })).is_err();
        (caught, port.inspect().map(|s| s.stop_reason),
         observer.poll_journal().last_poll().is_some_and(|r| r.returned().is_none()),
         observer.poll_journal().attempts(), admission(observer.admission_journal()))
    });
    let observed = (result, admission(&close), retained_before_finish(&f), f.child_state()?);
    let finish = f.finish()?;
    assert_eq!((observed, finish),
        ((Ok((true, Ok(StopReason::Requested), true, 1,
             expected_admission(CloseCause::PollUnwind))),
          expected_admission(CloseCause::PollUnwind), true, (true, true, f.child_id)), finished()),
        "oracle poll unwind latches real admission before caught escape");
    Ok(())
}

#[test]
fn in_flight_and_queued_work_remain_accounted_after_loss() -> TestResult {
    let (block, gate) = barrier();
    let (settings_done, settings_seen) = mpsc::channel();
    let hook: SinkHook = Box::new(move |point| {
        if point == SinkPoint::AfterPairReturn { let _ = settings_done.send(()); }
    });
    let mut f = Fixture::new("inflight-queue", Some(block), Some(hook), vec![gate.release.clone()])?;
    let port = f.port();
    let first = port.enqueue_for_key(baseline().key, frame(41)).map_err(|e| format!("{e:?}"))?;
    let entered = gate.entered.recv_timeout(WAIT).is_ok();
    if !entered { f.finish()?; return Err("fixture submit did not reach barrier".into()); }
    let second = port.enqueue_for_key(baseline().key, frame(43)).map_err(|e| format!("{e:?}"))?;
    let handle = f.handle.clone(); let (tx, rx) = mpsc::channel();
    let job = thread::spawn(move || {
        let mut poll = Journal::new(); let mut close = AdmissionJournal::new();
        let result = handle.with_supervisor_observation(&mut poll, &mut close,
            |observer| observer.observe(ControllerEvent::ReporterDisconnected).ok());
        let value = (result, admission(&close)); let _ = tx.send(value.clone()); value
    });
    let early = rx.recv_timeout(WAIT);
    // Do not touch mailbox on a timeout: a deliberately blocking control may
    // own it while awaiting callbacks. Release every barrier before any join.
    let during = if early.is_ok() { port.inspect().ok() } else { None };
    let held = retained_before_finish(&f);
    let _ = gate.release.send(());
    let returned = job.join();
    // Wait for the already-dispatched operation to reach actual settings
    // completion before explicit finish closes the producer gate. This is the
    // deliberate late-ACK case, not a scheduling-dependent close/ACK race.
    let late_settings = settings_seen.recv_timeout(WAIT).is_ok();
    let finish = f.finish()?;
    let s = f.handle.snapshot(); let joined = s.lifecycle.unwrap().unwrap().joined.unwrap();
    let observed = (early.is_ok(), returned.is_ok_and(|v|
        v == (Ok(Some(Disposition::Hold)),
            expected_admission(CloseCause::ControllerLoss(ControllerEvent::ReporterDisconnected)))),
        during.map(|s| (s.queued, s.in_flight, s.stop_reason)), held, finish,
        f.submitted(), joined.mailbox.status.discarded,
        joined.mailbox.status.last.map(|r| (r.request, r.outcome)),
        joined.route.producer.status.last_ack, f.case.read("knob.ctr")?,
        port.enqueue_for_key(baseline().key, frame(47)).err(),
        (f.counts.baselines.load(Ordering::SeqCst), late_settings,
         second != first && second.request_id() == first.request_id() + 1));
    assert_eq!(observed,
        (true, true, Some((1, Some(first), StopReason::Requested)), true, finished(),
         vec![first], 1, Some((first, AttemptOutcome::Committed { settings_written: true })),
         Some(AckCheckpoint { ack: FrameAck { request: first, kernel_sequence: 24 },
            settings: AckSettings::ReturnedOk }), frame(41)[34].to_string().into_bytes(),
         Some(TransportError::Terminal), (1, true, true)),
        "oracle terminal admission preserves in-flight result and discards queued work");
    Ok(())
}

#[test]
fn reserved_request_is_not_forwarded_after_observed_exit() -> TestResult {
    let mut f = Fixture::new("reserved-exit", None, None, vec![])?;
    let port = f.port();
    let (entered, seen) = mpsc::channel(); let (release, proceed) = mpsc::channel();
    f.releases.push(release.clone());
    port.pause_before_dispatch_for_test(entered, proceed);
    let request = port.enqueue_for_key(baseline().key, frame(53)).map_err(|e| format!("{e:?}"))?;
    let reserved = seen.recv_timeout(WAIT).is_ok();
    if !reserved { f.finish()?; return Err("fixture reservation barrier not reached".into()); }
    let before = port.inspect().map_err(|e| format!("{e:?}"))?;
    f.send_child_exit()?;
    let mut poll = Journal::new(); let mut close = AdmissionJournal::new();
    let mut deadline_ok = false;
    let result = f.handle.with_supervisor_observation(&mut poll, &mut close, |observer| {
        let deadline = Instant::now() + WAIT;
        loop {
            let _ = observer.observe(ControllerEvent::None);
            if observer.poll_journal().exited() { deadline_ok = true; break; }
            if Instant::now() >= deadline { break; }
            thread::sleep(Duration::from_millis(5));
        }
    });
    let exit = poll.last_poll().and_then(|p| p.returned())
        .and_then(|r| r.as_ref().ok()).and_then(|s| s.as_ref()).and_then(|s| s.code());
    let held = retained_before_finish(&f);
    let _ = release.send(());
    let finish = f.finish()?;
    let joined = f.handle.snapshot().lifecycle.unwrap().unwrap().joined.unwrap();
    assert_eq!((result, deadline_ok, exit, admission(&close), before.in_flight,
        held, finish, f.submitted(), joined.mailbox.status.last.map(|r| (r.request, r.outcome)),
        joined.route.producer.status.last_ack, f.case.unchanged()?),
        (Ok(()), true, Some(EXIT_CODE), expected_admission(CloseCause::ObservedHold),
         Some(request), true, finished(), vec![], Some((request, AttemptOutcome::NotForwarded)),
         None, true),
        "oracle observed child exit prevents forwarding of reserved request");
    Ok(())
}

#[test]
fn settings_callback_does_not_block_terminal_admission_latch() -> TestResult {
    let (block, gate) = barrier();
    let hook: SinkHook = Box::new(move |point| {
        if point == SinkPoint::AfterMappingIntent { block.hit(); }
    });
    let mut f = Fixture::new("settings-latch", None, Some(hook), vec![gate.release.clone()])?;
    let port = f.port();
    let request = port.enqueue_for_key(baseline().key, frame(59)).map_err(|e| format!("{e:?}"))?;
    let entered = gate.entered.recv_timeout(WAIT).is_ok();
    if !entered { f.finish()?; return Err("fixture settings barrier not reached".into()); }
    let handle = f.handle.clone(); let (tx, rx) = mpsc::channel();
    let job = thread::spawn(move || {
        let mut poll = Journal::new(); let mut close = AdmissionJournal::new();
        let result = handle.with_supervisor_observation(&mut poll, &mut close,
            |observer| observer.observe(ControllerEvent::ReaderEof { copied: 0 }).ok());
        let value = (result, admission(&close)); let _ = tx.send(value.clone()); value
    });
    let early = rx.recv_timeout(WAIT);
    // This ordering also cleans the deliberate blocking-latch mutation: its
    // observation is allowed to finish only AFTER the fixture releases settings.
    let _ = gate.release.send(());
    let returned = job.join();
    let child = f.child_state()?; let held = retained_before_finish(&f);
    let finish = f.finish()?;
    let joined = f.handle.snapshot().lifecycle.unwrap().unwrap().joined.unwrap();
    assert_eq!((early.is_ok(), returned.is_ok_and(|v|
        v == (Ok(Some(Disposition::Hold)), expected_admission(CloseCause::ControllerLoss(
            ControllerEvent::ReaderEof { copied: 0 })))), child, held, finish, f.submitted(),
        joined.route.producer.status.last_ack, f.case.read("knob.ctr")?),
        (true, true, (true, true, f.child_id), true, finished(), vec![request],
         Some(AckCheckpoint { ack: FrameAck { request, kernel_sequence: 24 },
            settings: AckSettings::ReturnedOk }), frame(59)[34].to_string().into_bytes()),
        "oracle admission latch completes before blocked settings are released");
    Ok(())
}

#[test]
fn scope_return_and_unwind_keep_startup_child_and_exit_owner() -> TestResult {
    let mut all = Vec::new();
    for unwind in [false, true] {
        let mut f = Fixture::new(if unwind { "scope-unwind" } else { "scope-return" },
            None, None, vec![])?;
        let port = f.port(); let mut poll = Journal::new(); let mut close = AdmissionJournal::new();
        let mut inside = None;
        let caught = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            f.handle.with_supervisor_observation(&mut poll, &mut close, |observer| {
                let result = observer.observe(ControllerEvent::None).ok();
                inside = Some((result, port.current_key(),
                    port.inspect().map(|s| s.stop_reason), admission(observer.admission_journal())));
                if unwind { panic!("scripted scope callback unwind"); }
            })
        }));
        let caught_ok = if unwind { caught.is_err() } else { matches!(caught, Ok(Ok(()))) };
        let held = retained_before_finish(&f); let child = f.child_state()?;
        let reason = port.inspect().map(|s| s.stop_reason);
        let snapshot = f.handle.snapshot();
        let final_admission = admission(&close);
        let finish = f.finish()?;
        all.push((caught_ok, inside, final_admission, reason, held,
            child == (true, true, f.child_id) && poll.child_id() == Some(f.child_id),
            snapshot.supervisor_poisoned, finish, f.case.unchanged()?));
    }
    let expected = [false, true].map(|unwind|
        (true, Some((Some(Disposition::Observing), Some(baseline().key),
          Ok(StopReason::None), (true, None, None))),
         expected_admission(if unwind { CloseCause::ScopeUnwind } else { CloseCause::ScopeEnd }),
         Ok(StopReason::Requested), true, unwind, finished(), true));
    // Actual Child identity was compared with both its pre-transfer ID and
    // retained journal above; no equality of synthetic PID values is assumed.
    let expected: Vec<_> = expected.into_iter().map(|v|
        (v.0, v.1, v.2, v.3, v.4, true, v.5, v.6, v.7)).collect();
    // A retained owner exists, but the Child mutex is poisoned BEFORE entry.
    // Refusal must still close that owner's real port without polling/recovery.
    let mut f = Fixture::new("scope-prepoisoned", None, None, vec![])?;
    let port = f.port();
    let poisoned = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        f.handle.with_supervisor_for_test(|_| panic!("scripted pre-entry child mutex poison"));
    })).is_err();
    let mut poll = Journal::new(); let mut close = AdmissionJournal::new();
    let mut called = false;
    let refused = f.handle.with_supervisor_observation(&mut poll, &mut close, |_| { called = true; });
    let refusal_observed = (poisoned, refused, called, poll.bound(), poll.attempts(),
        admission(&close), port.enqueue_for_key(baseline().key, frame(67)).err(),
        retained_before_finish(&f), f.child_state()?);
    let finish = f.finish()?;
    assert_eq!((all, refusal_observed, finish),
        (expected, (true, Err(ScopeError::SupervisorPoisoned), false, false, 0,
         expected_admission(CloseCause::ScopeRefused(ScopeError::SupervisorPoisoned)),
         Some(TransportError::Terminal), true, (true, true, f.child_id)), finished()),
        "oracle scope return and unwind retain child and existing lifecycle owners");
    Ok(())
}
