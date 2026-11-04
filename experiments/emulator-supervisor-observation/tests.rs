// SPDX-License-Identifier: GPL-3.0-only
//! Focused offline observation checks. The real private child is a sentinel
//! shell, never the emulator. Only fixture cleanup sends its exit command.
//! Scripted Read/poll errors and panics are explicitly distinguished from OS
//! observations. Every semantic oracle runs after natural exit and actual wait.
use super::*;
use std::cell::Cell;
use std::fmt;
use std::io::{Read, Write};
use std::os::fd::OwnedFd;
use std::os::unix::net::UnixStream;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::process::{Child, ChildStdin, Command, ExitStatus, Stdio};
use std::thread;
use std::time::{Duration, Instant};

const LIMIT: Duration = Duration::from_secs(5);
const EXIT_CODE: i32 = 23;
type TestResult<T = ()> = Result<T, String>;

fn context<T>(result: io::Result<T>, label: &str) -> TestResult<T> {
    result.map_err(|error| format!("{label}: {error}"))
}

struct Fixture {
    child: Child,
    input: Option<ChildStdin>,
    waited: bool,
}
impl Fixture {
    fn spawn() -> TestResult<Self> {
        let (mut ready, output) = context(UnixStream::pair(), "fixture ready socketpair")?;
        context(ready.set_read_timeout(Some(LIMIT)), "fixture ready timeout")?;
        let child = context(Command::new("/bin/sh")
            .arg("-c")
            .arg("printf 'READY\\n'; IFS= read -r sentinel || exit 90; [ \"$sentinel\" = G ] || exit 91; exit 23")
            .stdin(Stdio::piped())
            .stdout(Stdio::from(OwnedFd::from(output)))
            .stderr(Stdio::null())
            .spawn(), "spawn private sentinel child")?;
        // The guard exists before any readiness read or fallible setup.
        let owner = Self { child, input: None, waited: false };
        // Retain stdin on the actual Child so observation cannot invisibly
        // take/drop it. Only the natural-exit case detaches it explicitly.
        if owner.child.stdin.is_none() {
            return Err("fixture child stdin unavailable".into());
        }
        let mut message = [0u8; 6];
        context(ready.read_exact(&mut message), "fixture READY")?;
        if &message != b"READY\n" {
            return Err("fixture malformed READY".into());
        }
        Ok(owner)
    }
    fn alive(&mut self) -> TestResult<bool> {
        Ok(context(self.child.try_wait(), "fixture liveness")?.is_none())
    }
    fn finish(&mut self) -> TestResult<ExitStatus> {
        if self.input.is_none() { self.input = self.child.stdin.take(); }
        send_exit(&mut self.input)?;
        let deadline = Instant::now() + LIMIT;
        loop {
            if context(self.child.try_wait(), "fixture cleanup status")?.is_some() {
                break;
            }
            if Instant::now() >= deadline {
                return Err("fixture natural-exit deadline exceeded".into());
            }
            thread::sleep(Duration::from_millis(5));
        }
        let status = context(self.child.wait(), "fixture explicit actual wait")?;
        self.waited = true;
        if status.code() != Some(EXIT_CODE) {
            return Err(format!("fixture unexpected exit: {status}"));
        }
        Ok(status)
    }
}
fn send_exit(input: &mut Option<ChildStdin>) -> TestResult {
    if let Some(mut input) = input.take() {
        context(input.write_all(b"G\n"), "fixture natural-exit command")?;
        // EOF only follows the explicit G sentinel; production never calls here.
    }
    Ok(())
}
impl Drop for Fixture {
    fn drop(&mut self) {
        if !self.waited {
            // Emergency private-fixture failure only. Never counted as a
            // semantic pass, production stop authority or ordinary cleanup.
            eprintln!("FIXTURE_ABORT sentinel kill/wait after fixture failure");
            let _ = self.child.kill();
            let _ = self.child.wait();
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
enum PollValue {
    Armed,
    Running,
    Exited(Option<i32>, bool),
    Error(io::ErrorKind, Option<i32>, String),
}
#[derive(Debug, PartialEq, Eq)]
struct PollInfo {
    attempt: u64,
    event: ControllerEvent,
    value: PollValue,
}
fn info(record: Option<&PollRecord>) -> Option<PollInfo> {
    record.map(|record| PollInfo {
        attempt: record.attempt(),
        event: record.event(),
        value: match record.returned() {
            None => PollValue::Armed,
            Some(Ok(None)) => PollValue::Running,
            Some(Ok(Some(status))) => PollValue::Exited(status.code(), status.success()),
            Some(Err(error)) => PollValue::Error(
                error.kind(), error.raw_os_error(), error.to_string()),
        },
    })
}
fn poll(attempt: u64, event: ControllerEvent, value: PollValue) -> Option<PollInfo> {
    Some(PollInfo { attempt, event, value })
}
fn loss(journal: &Journal) -> Option<(u64, ControllerEvent)> {
    journal.first_loss().map(|record| (record.attempt, record.event))
}
fn bind<'a>(fixture: &'a mut Fixture, journal: &'a mut Journal)
    -> TestResult<ChildObservation<'a>>
{
    ChildObservation::bind(&mut fixture.child, journal)
        .map_err(|error| format!("fixture initial bind: {error:?}"))
}

struct CountedReader<R> {
    inner: R,
    calls: usize,
}
impl<R: Read> Read for CountedReader<R> {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        self.calls += 1;
        self.inner.read(bytes)
    }
}
struct ScriptedReader {
    calls: usize,
    panic: bool,
}
impl Read for ScriptedReader {
    fn read(&mut self, _: &mut [u8]) -> io::Result<usize> {
        self.calls += 1;
        if self.panic {
            panic!("scripted reader unwind");
        }
        Err(io::Error::from_raw_os_error(5))
    }
}
#[derive(Debug)]
struct ExactPayload(&'static str);
impl fmt::Display for ExactPayload {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.0)
    }
}
impl std::error::Error for ExactPayload {}

#[test]
fn running_observation_preserves_borrowed_child() -> TestResult {
    let mut fixture = Fixture::spawn()?;
    let id = fixture.child.id();
    let mut journal = Journal::new();
    let result = {
        let mut observer = bind(&mut fixture, &mut journal)?;
        observer.observe(ControllerEvent::None)
    };
    let observed = (result, journal.bound(), journal.child_id(), journal.attempts(),
        journal.disposition(), journal.exited(), loss(&journal), info(journal.last_poll()),
        info(journal.first_poll_error()), fixture.child.stdin.is_some(), fixture.alive()?);
    fixture.finish()?;
    assert_eq!(observed,
        (Ok(Disposition::Observing), true, Some(id), 1, Disposition::Observing, false,
         None, poll(1, ControllerEvent::None, PollValue::Running), None, true, true),
        "oracle running retains exact borrowed child");
    Ok(())
}

#[test]
fn controller_eof_retains_exact_copy_and_holds_live_child() -> TestResult {
    let mut fixture = Fixture::spawn()?;
    let (reader, mut writer) = context(UnixStream::pair(), "controller socketpair")?;
    context(reader.set_read_timeout(Some(LIMIT)), "controller fixture timeout")?;
    context(writer.write_all(b"copied!"), "controller fixture bytes")?;
    drop(writer); // Real EOF on an unnamed socket, not a synthetic Read return.
    let mut reader = CountedReader { inner: reader, calls: 0 };
    let mut reader_journal = ReaderJournal::new();
    let event_result = copy_controller(&mut reader, &mut reader_journal);
    let before = reader.calls;
    let again = copy_controller(&mut reader, &mut reader_journal);
    let reader_observed = (reader_journal.attempted(),
        reader_journal.returned().map(|r| r.as_ref().ok().copied()),
        reader_journal.event(), (2..=8).contains(&before), before == reader.calls);
    let mut journal = Journal::new();
    let result = {
        let mut observer = bind(&mut fixture, &mut journal)?;
        // A missing/changed event stays a final oracle failure while preserving
        // the fixture's cleanup path; it cannot silently authorize a stop.
        observer.observe(reader_journal.event().unwrap_or(ControllerEvent::None))
    };
    let event = ControllerEvent::ReaderEof { copied: 7 };
    let observed = (event_result, again, reader_observed, result,
        journal.disposition(), loss(&journal), info(journal.last_poll()),
        fixture.child.stdin.is_some(), fixture.alive()?);
    fixture.finish()?;
    assert_eq!(observed,
        (Ok(event), Err(ReaderError::AlreadyAttempted),
         (true, Some(Some(7)), Some(event), true, true), Ok(Disposition::Hold),
         Disposition::Hold, Some((1, event)), poll(1, event, PollValue::Running), true, true),
        "oracle actual reader EOF holds live child");
    Ok(())
}

#[test]
fn scripted_reader_error_preserves_kind_and_raw_code() -> TestResult {
    let mut fixture = Fixture::spawn()?;
    let mut reader = ScriptedReader { calls: 0, panic: false };
    let mut returned = ReaderJournal::new();
    let error_kind = io::Error::from_raw_os_error(5).kind();
    let event = ControllerEvent::ReaderError { kind: error_kind, raw_os_error: Some(5) };
    let result = copy_controller(&mut reader, &mut returned);
    let repeated = copy_controller(&mut reader, &mut returned);
    let raw = returned.returned().and_then(|r| r.as_ref().err())
        .map(|e| (e.kind(), e.raw_os_error()));
    let mut panicking = ScriptedReader { calls: 0, panic: true };
    let mut interrupted = ReaderJournal::new();
    let caught = catch_unwind(AssertUnwindSafe(|| {
        let _ = copy_controller(&mut panicking, &mut interrupted);
    })).is_err();
    let panic_repeat = copy_controller(&mut panicking, &mut interrupted);
    let reader_observed = (result, repeated, reader.calls, raw, returned.event(),
        caught, interrupted.attempted(), interrupted.returned().is_none(),
        interrupted.event(), panic_repeat, panicking.calls);
    let mut journal = Journal::new();
    let observed_result = {
        let mut observer = bind(&mut fixture, &mut journal)?;
        observer.observe(returned.event().unwrap_or(ControllerEvent::None))
    };
    let observed = (reader_observed, observed_result, journal.disposition(),
        loss(&journal), info(journal.last_poll()), fixture.alive()?);
    fixture.finish()?;
    assert_eq!(observed,
        ((Ok(event), Err(ReaderError::AlreadyAttempted), 1, Some((error_kind, Some(5))),
          Some(event), true, true, true, None, Err(ReaderError::AlreadyAttempted), 1),
         Ok(Disposition::Hold), Disposition::Hold, Some((1, event)),
         poll(1, event, PollValue::Running), true),
        "oracle scripted reader return and unwind stay distinct");
    Ok(())
}

#[test]
fn reporter_disconnect_stays_distinct_and_hold_is_sticky() -> TestResult {
    let mut fixture = Fixture::spawn()?;
    let mut journal = Journal::new();
    let (sender, receiver) = std::sync::mpsc::channel::<()>();
    drop(sender);
    let disconnected = matches!(receiver.try_recv(),
        Err(std::sync::mpsc::TryRecvError::Disconnected));
    // Fixture-only mapping of the actual channel outcome. No production
    // reporter/admission adapter is installed by this observation slice.
    let event = if disconnected { ControllerEvent::ReporterDisconnected }
        else { ControllerEvent::None };
    let (first, first_info, second, third) = {
        let mut observer = bind(&mut fixture, &mut journal)?;
        let first = observer.observe(event);
        let first_info = info(observer.journal().last_poll());
        let second = observer.observe(ControllerEvent::None);
        let third = observer.observe(ControllerEvent::ReaderEof { copied: 0 });
        (first, first_info, second, third)
    };
    let observed = (disconnected, first, first_info, second, third, journal.attempts(),
        journal.disposition(), loss(&journal), info(journal.last_poll()), fixture.alive()?);
    fixture.finish()?;
    assert_eq!(observed,
        (true, Ok(Disposition::Hold), poll(1, ControllerEvent::ReporterDisconnected, PollValue::Running),
         Ok(Disposition::Hold), Ok(Disposition::Hold), 3, Disposition::Hold,
         Some((1, ControllerEvent::ReporterDisconnected)),
         poll(3, ControllerEvent::ReaderEof { copied: 0 }, PollValue::Running), true),
        "oracle reporter disconnect is distinct and hold cannot reopen");
    Ok(())
}

#[test]
fn natural_exit_is_recorded_then_polling_refuses() -> TestResult {
    let mut fixture = Fixture::spawn()?;
    // This one test needs an independently owned command handle while the
    // observer borrows Child; all other tests leave stdin in Child itself.
    fixture.input = fixture.child.stdin.take();
    let mut journal = Journal::new();
    // Distinct field borrows retain the same Child while the fixture writes its
    // private exit sentinel. No production observer gets termination authority.
    let (first, final_info, last_result, repeated, no_callback, attempts_unchanged) = {
        let mut observer = ChildObservation::bind(&mut fixture.child, &mut journal)
            .map_err(|e| format!("fixture initial bind: {e:?}"))?;
        let first = observer.observe(ControllerEvent::None);
        send_exit(&mut fixture.input)?;
        let deadline = Instant::now() + LIMIT;
        let mut last_result;
        loop {
            last_result = observer.observe(ControllerEvent::None);
            if observer.journal().exited() { break; }
            if Instant::now() >= deadline {
                return Err("fixture observation of natural exit exceeded deadline".into());
            }
            thread::sleep(Duration::from_millis(5));
        }
        let final_info = info(observer.journal().last_poll());
        let attempts = observer.journal().attempts();
        let called = Cell::new(false);
        let repeated = observer.observe_with_for_test(ControllerEvent::ReporterDisconnected, |_| {
            called.set(true);
            Ok(None)
        });
        (first, final_info, last_result, repeated, !called.get(),
         attempts == observer.journal().attempts())
    };
    let exited = final_info.as_ref().is_some_and(|r|
        r.event == ControllerEvent::None && r.attempt >= 2 &&
        r.value == PollValue::Exited(Some(EXIT_CODE), false));
    let observed = (first, exited, last_result, repeated, no_callback,
        attempts_unchanged, journal.exited(), journal.disposition(), loss(&journal));
    fixture.finish()?;
    assert_eq!(observed,
        (Ok(Disposition::Observing), true, Ok(Disposition::Hold), Err(ObserveError::Exited),
         true, true, true, Disposition::Hold, None),
        "oracle natural exit remains exact and polling refuses replay");
    Ok(())
}

#[test]
fn scripted_poll_error_is_not_an_exit_receipt() -> TestResult {
    let mut fixture = Fixture::spawn()?;
    let mut journal = Journal::new();
    let (first, current_error, second, later_running, first_error) = {
        let mut observer = bind(&mut fixture, &mut journal)?;
        let first = observer.observe_with_for_test(ControllerEvent::None, |_| {
            Err(io::Error::new(io::ErrorKind::PermissionDenied,
                ExactPayload("retained scripted polling payload")))
        });
        let current_error = info(observer.journal().last_poll());
        let second = observer.observe(ControllerEvent::None);
        let later_running = info(observer.journal().last_poll());
        let first_error = observer.journal().first_poll_error()
            .and_then(|r| r.returned()).and_then(|r| r.as_ref().err())
            .and_then(|e| e.get_ref()).and_then(|e| e.downcast_ref::<ExactPayload>())
            .map(|payload| payload.0);
        (first, current_error, second, later_running, first_error)
    };
    let observed = (first, current_error, second, later_running, first_error,
        info(journal.first_poll_error()), journal.exited(), journal.disposition(),
        loss(&journal), fixture.alive()?);
    fixture.finish()?;
    assert_eq!(observed,
        (Ok(Disposition::Hold),
         poll(1, ControllerEvent::None, PollValue::Error(io::ErrorKind::PermissionDenied,
             None, "retained scripted polling payload".into())),
         Ok(Disposition::Hold), poll(2, ControllerEvent::None, PollValue::Running),
         Some("retained scripted polling payload"),
         poll(1, ControllerEvent::None, PollValue::Error(io::ErrorKind::PermissionDenied,
             None, "retained scripted polling payload".into())),
         false, Disposition::Hold, None, true),
        "oracle scripted poll error is retained and never an exit receipt");
    Ok(())
}

#[test]
fn scripted_poll_panic_retains_armed_attempt_without_replay() -> TestResult {
    let mut fixture = Fixture::spawn()?;
    let mut journal = Journal::new();
    let calls = Cell::new(0u32);
    let (caught, immediate, armed, repeated, after) = {
        let mut observer = bind(&mut fixture, &mut journal)?;
        let caught = catch_unwind(AssertUnwindSafe(|| {
            let _ = observer.observe_with_for_test(ControllerEvent::None, |_| {
                calls.set(calls.get() + 1);
                panic!("scripted polling unwind");
            });
        })).is_err();
        let immediate = observer.journal().disposition();
        let armed = info(observer.journal().last_poll());
        let repeated = observer.observe_with_for_test(ControllerEvent::ReaderEof { copied: 0 }, |_| {
            calls.set(calls.get() + 1);
            Ok(None)
        });
        (caught, immediate, armed, repeated, info(observer.journal().last_poll()))
    };
    let observed = (caught, immediate, armed, repeated, after, calls.get(),
        journal.attempts(), journal.exited(), journal.disposition(), loss(&journal), fixture.alive()?);
    fixture.finish()?;
    assert_eq!(observed,
        (true, Disposition::Hold, poll(1, ControllerEvent::None, PollValue::Armed),
         Err(ObserveError::UnreturnedPoll), poll(1, ControllerEvent::None, PollValue::Armed),
         1, 1, false, Disposition::Hold, None, true),
        "oracle unreturned poll stays armed and cannot replay");
    Ok(())
}

#[test]
fn journal_cannot_rebind_to_another_actual_child() -> TestResult {
    let mut first = Fixture::spawn()?;
    let mut second = Fixture::spawn()?;
    let mut journal = Journal::new();
    let first_id = first.child.id();
    let result = {
        let mut observer = bind(&mut first, &mut journal)?;
        observer.observe(ControllerEvent::None)
    };
    let before = info(journal.last_poll());
    let same_refused = matches!(ChildObservation::bind(&mut first.child, &mut journal),
        Err(BindError::AlreadyBound));
    let other_refused = matches!(ChildObservation::bind(&mut second.child, &mut journal),
        Err(BindError::AlreadyBound));
    let observed = (result, same_refused, other_refused,
        journal.child_id(), journal.attempts(), before == info(journal.last_poll()),
        first.alive()?, second.alive()?);
    first.finish()?;
    second.finish()?;
    assert_eq!(observed,
        (Ok(Disposition::Observing), true, true, Some(first_id), 1, true, true, true),
        "oracle journal identity cannot be rebound to any child");
    Ok(())
}
