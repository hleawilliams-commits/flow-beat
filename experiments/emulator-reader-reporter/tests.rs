// SPDX-License-Identifier: GPL-3.0-only
//! Focused inactive reporter fixtures. No Child, mixer admission, or endpoint.
//! UnixStream pairs are anonymous local fixtures. Scripted Read/spawn failures
//! test retained evidence, not OS failure rates. All semantic assertions follow
//! explicit worker joins; barriers are fixture bounds, not product latency.
use super::*;
use std::fmt;
use std::io::{Cursor, Read, Write};
use std::net::Shutdown;
use std::os::unix::net::UnixStream;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{mpsc, Arc};
use std::thread;
use std::time::Duration;

const WAIT: Duration = Duration::from_secs(5);
const HELD: Duration = Duration::from_secs(15);
const EARLY: Duration = Duration::from_millis(500);

#[derive(Default)]
struct Counts { reads: AtomicUsize, drops: AtomicUsize }
struct Tracked<R> {
    inner: R,
    counts: Arc<Counts>,
    entered: Option<mpsc::Sender<()>>,
}
impl<R> Tracked<R> {
    fn new(inner: R, counts: &Arc<Counts>) -> Self {
        Self { inner, counts: Arc::clone(counts), entered: None }
    }
    fn signalling(inner: R, counts: &Arc<Counts>, entered: mpsc::Sender<()>) -> Self {
        Self { inner, counts: Arc::clone(counts), entered: Some(entered) }
    }
}
impl<R: Read> Read for Tracked<R> {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        self.counts.reads.fetch_add(1, Ordering::SeqCst);
        if let Some(tx) = self.entered.take() { let _ = tx.send(()); }
        self.inner.read(out)
    }
}
impl<R> Drop for Tracked<R> {
    fn drop(&mut self) { self.counts.drops.fetch_add(1, Ordering::SeqCst); }
}
#[derive(Debug)]
struct ReadFault { marker: &'static str }
impl fmt::Display for ReadFault {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result { f.write_str(self.marker) }
}
impl std::error::Error for ReadFault {}
struct FailingRead;
impl Read for FailingRead {
    fn read(&mut self, _: &mut [u8]) -> io::Result<usize> {
        Err(io::Error::new(io::ErrorKind::Other, ReadFault { marker: "exact-reader-error" }))
    }
}
#[derive(Debug)]
struct PanicFault { marker: &'static str }
struct PanickingRead;
impl Read for PanickingRead {
    fn read(&mut self, _: &mut [u8]) -> io::Result<usize> {
        std::panic::panic_any(PanicFault { marker: "exact-reader-panic" })
    }
}
fn eof<R: Read + Send + 'static>(owner: &TerminalReader<R>, copied: u64) -> bool {
    owner.completed_journal().is_some_and(|j| j.attempted()
        && matches!(j.returned(), Some(Ok(n)) if *n == copied)
        && j.event() == Some(ControllerEvent::ReaderEof { copied }))
}
fn read_fault<R: Read + Send + 'static>(owner: &TerminalReader<R>) -> bool {
    owner.completed_journal().and_then(|j| j.returned())
        .and_then(|r| r.as_ref().err())
        .is_some_and(|e| e.kind() == io::ErrorKind::Other
            && e.raw_os_error().is_none()
            && e.get_ref().and_then(|e| e.downcast_ref::<ReadFault>())
                .is_some_and(|e| e.marker == "exact-reader-error"))
}
fn joined<R: Read + Send + 'static>(owner: &TerminalReader<R>) -> bool {
    matches!(owner.join_return(), Some(Ok(_)))
}
fn first_notice<R: Read + Send + 'static>(owner: &mut TerminalReader<R>, n: u64) -> bool {
    matches!(owner.try_report(), Ok(ReportPoll::Completed {
        event: ControllerEvent::ReaderEof { copied }, notice: Notice::Received
    }) if copied == n)
}

#[test]
fn retained_and_spawn_refusal_preserve_reader() {
    let counts = Arc::new(Counts::default());
    let mut owner = TerminalReader::retain(Tracked::new(Cursor::new(b"untouched".to_vec()), &counts));
    let before = (counts.reads.load(Ordering::SeqCst), counts.drops.load(Ordering::SeqCst));
    let refused = owner.start_scripted_failure_for_test(io::Error::new(
        io::ErrorKind::Other, ReadFault { marker: "exact-spawn-error" }));
    let exact = owner.spawn_return().and_then(|r| r.as_ref().err())
        .and_then(|e| e.get_ref()).and_then(|e| e.downcast_ref::<ReadFault>())
        .is_some_and(|e| e.marker == "exact-spawn-error");
    let retry = owner.start_once();
    let no_worker = owner.join().is_err() && owner.join_return().is_none();
    let held = counts.drops.load(Ordering::SeqCst);
    let no_copy = owner.completed_journal().is_none() && owner.copy_return().is_none();
    drop(owner);
    let observed = (before, refused.is_err(), exact, retry.is_err(), no_worker,
        no_copy, held, counts.reads.load(Ordering::SeqCst), counts.drops.load(Ordering::SeqCst));
    assert_eq!(observed, ((0, 0), true, true, true, true, true, 0, 0, 1),
        "reader-spawn-refusal");
}

#[test]
fn reader_error_preserves_exact_payload() {
    let counts = Arc::new(Counts::default());
    let mut owner = TerminalReader::retain(Tracked::new(FailingRead, &counts));
    let start = owner.start_once();
    let finish = owner.join();
    let full = read_fault(&owner);
    let report = matches!(owner.try_report(), Ok(ReportPoll::Completed {
        event: ControllerEvent::ReaderError { kind: io::ErrorKind::Other, raw_os_error: None },
        notice: Notice::Received
    }));
    let actual_send = matches!(owner.notification_return(), Some(Ok(())));
    let first_report = owner.snapshot();
    let second_report = owner.try_report();
    let after_report = owner.snapshot();
    let terminal_preserved = second_report == Err(ReportError::AlreadyReported)
        && first_report.terminal_report == after_report.terminal_report
        && first_report.report_attempts == after_report.report_attempts;
    let retry = owner.start_once().is_err();
    let after = read_fault(&owner);
    let held = counts.drops.load(Ordering::SeqCst);
    let join_retained = joined(&owner);
    drop(owner);
    let observed = (start.is_ok(), finish.is_ok(), full, report, actual_send,
        terminal_preserved, retry, after, join_retained, held,
        (counts.reads.load(Ordering::SeqCst), counts.drops.load(Ordering::SeqCst)));
    assert_eq!(observed, (true, true, true, true, true, true, true, true, true, 0, (1, 1)),
        "reader-exact-error");
}

#[test]
fn reader_panic_is_joined_without_replay() {
    let counts = Arc::new(Counts::default());
    let mut owner = TerminalReader::retain(Tracked::new(PanickingRead, &counts));
    let start = owner.start_once();
    let finish = owner.join();
    let exact = owner.join_return().and_then(|r| r.as_ref().err())
        .and_then(|p| p.downcast_ref::<PanicFault>())
        .is_some_and(|p| p.marker == "exact-reader-panic");
    let snapshot = owner.snapshot();
    let panic_cell = snapshot.copy_call_armed && !snapshot.completed
        && snapshot.cell == CellSnapshot::Available { poisoned: true,
            journal_attempted: Some(true), journal_returned: Some(false) };
    let disconnected = matches!(owner.try_report(), Ok(ReportPoll::ReporterDisconnected));
    let incomplete = owner.completed_journal().is_none() && owner.copy_return().is_none()
        && owner.notification_return().is_none();
    let retry = owner.start_once().is_err();
    let repeat_join = owner.join().is_err();
    let before_drop = counts.drops.load(Ordering::SeqCst);
    let retained_panic = owner.join_return().and_then(|r| r.as_ref().err())
        .and_then(|p| p.downcast_ref::<PanicFault>()).is_some();
    drop(owner);
    let observed = (start.is_ok(), finish == Ok(JoinOutcome::Panicked), exact, disconnected, incomplete && panic_cell,
        retry, repeat_join, retained_panic, before_drop,
        (counts.reads.load(Ordering::SeqCst), counts.drops.load(Ordering::SeqCst)));
    assert_eq!(observed, (true, true, true, true, true, true, true, true, 0, (1, 1)),
        "reader-panic-no-replay");
}

#[test]
fn joined_owner_retains_reader_until_final_drop() {
    let counts = Arc::new(Counts::default());
    let mut owner = TerminalReader::retain(Tracked::new(Cursor::new(b"retained".to_vec()), &counts));
    let start = owner.start_once();
    let finish = owner.join();
    let complete = eof(&owner, 8);
    let notice = first_notice(&mut owner, 8);
    let held = counts.drops.load(Ordering::SeqCst);
    let read_calls = counts.reads.load(Ordering::SeqCst);
    let retry = owner.start_once().is_err();
    let repeat_join = owner.join().is_err();
    let same_result = eof(&owner, 8) && joined(&owner);
    drop(owner);
    let observed = (start.is_ok(), finish.is_ok(), complete, notice, held,
        read_calls >= 2 && read_calls <= 9, retry, repeat_join, same_result,
        counts.drops.load(Ordering::SeqCst));
    assert_eq!(observed, (true, true, true, true, 0, true, true, true, true, 1),
        "reader-final-custody");
}

#[test]
fn gate_publication_precedes_first_read() {
    let counts = Arc::new(Counts::default());
    let (read_tx, read_rx) = mpsc::channel();
    let (published_tx, published_rx) = mpsc::channel();
    let (release_tx, release_rx) = mpsc::channel();
    let mut owner = TerminalReader::retain(Tracked::signalling(
        Cursor::new(b"gate".to_vec()), &counts, read_tx));
    owner.set_after_spawn_barrier_for_test(published_tx, release_rx);
    // The outer fixture thread keeps the actual owner even if the cfg checkpoint
    // panics. It closes only the private start gate and explicitly joins.
    let start_thread = thread::spawn(move || {
        let start = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| owner.start_once()));
        if start.is_err() { owner.drop_gate_for_test(); }
        let finish = owner.join();
        (owner, start, finish)
    });
    let published = published_rx.recv_timeout(WAIT);
    let no_early_read = matches!(read_rx.recv_timeout(EARLY), Err(mpsc::RecvTimeoutError::Timeout));
    let release = release_tx.send(());
    let completed = start_thread.join();
    let observed = match completed {
        Ok((mut owner, start, finish)) => {
            let publication = published.is_ok_and(|s| s.start_phase == StartPhase::HandleRetained
                && s.spawn_returned && s.handle_retained && !s.gate_release_armed
                && s.gate_return.is_none() && !s.copy_call_armed && !s.completed
                && s.cell == CellSnapshot::Available { poisoned: false,
                    journal_attempted: Some(false), journal_returned: Some(false) });
            let complete = eof(&owner, 4) && first_notice(&mut owner, 4);
            let actual_join = joined(&owner);
            let gate = matches!(owner.gate_return(), Some(Ok(())))
                && matches!(owner.gate_wait_return(), Some(Ok(())));
            drop(owner);
            (publication, no_early_read, release.is_ok(), matches!(start, Ok(Ok(()))),
                finish == Ok(JoinOutcome::Returned(WorkerExit::NotificationSent)), complete,
                actual_join, gate, counts.drops.load(Ordering::SeqCst))
        }
        Err(_) => (false, no_early_read, release.is_ok(), false, false, false, false, false,
            counts.drops.load(Ordering::SeqCst)),
    };
    assert_eq!(observed, (true, true, true, true, true, true, true, true, 1),
        "reader-gate-before-read");
}

#[test]
fn completed_journal_precedes_unit_notification() {
    let counts = Arc::new(Counts::default());
    let (reader, mut writer) = UnixStream::pair().expect("fixture anonymous pair");
    reader.set_read_timeout(Some(HELD)).expect("fixture read deadline");
    writer.write_all(b"journal").expect("fixture initial bytes");
    writer.shutdown(Shutdown::Write).expect("fixture EOF");
    let (published_tx, published_rx) = mpsc::channel();
    let (release_tx, release_rx) = mpsc::channel();
    let mut owner = TerminalReader::retain(Tracked::new(reader, &counts));
    owner.set_before_notification_barrier_for_test(published_tx, release_rx);
    let start = owner.start_once();
    let checkpoint = published_rx.recv_timeout(WAIT).is_ok();
    let complete_before_notice = eof(&owner, 7);
    let snapshot = owner.snapshot();
    let publication_state = snapshot.completed && snapshot.copy_call_armed
        && snapshot.handle_retained && snapshot.join_phase == JoinPhase::NotAttempted
        && !snapshot.notification_armed && snapshot.notification_return.is_none()
        && snapshot.cell == CellSnapshot::Available { poisoned: false,
            journal_attempted: None, journal_returned: None };
    let pending = owner.try_report() == Ok(ReportPoll::Pending);
    let release = release_tx.send(()).is_ok();
    let finish = owner.join();
    let notice = first_notice(&mut owner, 7);
    let journal_retained = eof(&owner, 7) && joined(&owner);
    drop(owner);
    drop(writer);
    let observed = (start.is_ok(), checkpoint, complete_before_notice, publication_state,
        pending, release, finish == Ok(JoinOutcome::Returned(WorkerExit::NotificationSent)),
        notice, journal_retained, counts.drops.load(Ordering::SeqCst));
    assert_eq!(observed, (true, true, true, true, true, true, true, true, true, 1),
        "reader-publish-before-notify");
}

#[test]
fn receiver_loss_preserves_completed_read() {
    let counts = Arc::new(Counts::default());
    let (published_tx, published_rx) = mpsc::channel();
    let (release_tx, release_rx) = mpsc::channel();
    let mut owner = TerminalReader::retain(Tracked::new(Cursor::new(b"notice".to_vec()), &counts));
    owner.set_before_notification_barrier_for_test(published_tx, release_rx);
    let start = owner.start_once();
    let checkpoint = published_rx.recv_timeout(WAIT).is_ok();
    let complete_before = eof(&owner, 6);
    // This cfg seam drops the real private receiver. No synthetic SendError is
    // injected: the worker's actual Sender::send returns the retained failure.
    owner.drop_receiver_for_test();
    let release = release_tx.send(()).is_ok();
    let finish = owner.join();
    let actual_send_failure = matches!(owner.notification_return(), Some(Err(_)));
    let snapshot = owner.snapshot();
    let full = eof(&owner, 6) && owner.copy_return()
        == Some(Ok(ControllerEvent::ReaderEof { copied: 6 }));
    let no_receiver = owner.try_report() == Err(ReportError::ReceiverUnavailable);
    let retry = owner.start_once() == Err(StartError::AlreadyAttempted);
    let join_retained = matches!(owner.join_return(), Some(Ok(WorkerExit::NotificationFailed)));
    drop(owner);
    let observed = (start.is_ok(), checkpoint, complete_before, release,
        finish == Ok(JoinOutcome::Returned(WorkerExit::NotificationFailed)),
        actual_send_failure, snapshot.notification_armed
            && snapshot.notification_return == Some(NotificationOutcome::ReceiverDisconnected),
        full, no_receiver, retry, join_retained, counts.drops.load(Ordering::SeqCst));
    assert_eq!(observed, (true, true, true, true, true, true, true, true, true, true, true, 1),
        "reader-notice-loss-distinct");
}

#[test]
fn blocked_socket_allows_nonblocking_snapshot() {
    let counts = Arc::new(Counts::default());
    let (reader, writer) = UnixStream::pair().expect("fixture anonymous pair");
    reader.set_read_timeout(Some(HELD)).expect("fixture read deadline");
    let (read_tx, read_rx) = mpsc::channel();
    let mut owner = TerminalReader::retain(Tracked::signalling(reader, &counts, read_tx));
    let start = owner.start_once();
    let entered = read_rx.recv_timeout(WAIT).is_ok();
    let (inspected_tx, inspected_rx) = mpsc::channel();
    // Even a blocking-lock control is released before joining either thread.
    // The deadline is a fixture discriminator, not a product latency proof.
    let (quick, released, inspection) = thread::scope(|scope| {
        let borrowed = &mut owner;
        let inspector = scope.spawn(move || {
            let snapshot = borrowed.snapshot();
            let _ = inspected_tx.send(());
            snapshot
        });
        let quick = inspected_rx.recv_timeout(EARLY).is_ok();
        let released = writer.shutdown(Shutdown::Write).is_ok();
        let inspection = inspector.join();
        (quick, released, inspection)
    });
    let finish = owner.join();
    let snapshot_while_busy = inspection.is_ok_and(|s| s.cell == CellSnapshot::Busy
        && s.copy_call_armed && !s.completed && s.handle_retained
        && s.join_phase == JoinPhase::NotAttempted);
    let complete = eof(&owner, 0) && first_notice(&mut owner, 0) && joined(&owner);
    drop(owner);
    drop(writer);
    let observed = (start.is_ok(), entered, quick, released, snapshot_while_busy,
        finish == Ok(JoinOutcome::Returned(WorkerExit::NotificationSent)),
        complete, counts.drops.load(Ordering::SeqCst));
    assert_eq!(observed, (true, true, true, true, true, true, true, 1),
        "reader-snapshot-does-not-wait");
}
