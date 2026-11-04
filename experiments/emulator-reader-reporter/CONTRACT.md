# Retained one-shot reader reporter

This inactive source slice owns only a supplied `Read` resource and the one
reader thread it creates. It composes the frozen `ReaderJournal` and
`copy_controller` helper with private local notification. It owns no Child,
backend, mixer port or guest graph and adds no production launch selection.
This document states the source contract; execution evidence is separate.

## Retain before starting

The API separates `retain`, `start_once`, `try_report`, explicit `join` and
nonblocking `snapshot`. Construction stores the supplied reader and a fresh
frozen reader journal before thread creation or reading. Start is one-shot. A
returned spawn error retains the actual `io::Error`. Spawn success is represented
by the stored actual JoinHandle and a separate `Ok(())` record; it does not retain
the original success Result as one object. Neither outcome is reader execution.
A failed spawn does not retry, read, or discard the retained resource.

A successful thread waits at a private one-shot gate. The owner stores its actual
JoinHandle before releasing that gate, so the worker cannot read before its join
handle is retained. Gate release is a separate recorded operation. Missing or
failed release is not successful reading; no replacement worker is started.

The reader and its mutable journal stay in retained storage throughout the
frozen copy helper. A reader panic therefore leaves the actual attempted but
unreturned journal available as poisoned diagnostics. Poison never authorizes
another copy. The helper may make multiple reads or retry internally and may
block indefinitely; one copy call is neither a single-read nor a latency bound.

## Preserve results before reporting

The complete returned `io::Result<u64>` belongs to the frozen journal. A returned
error preserves its original error object; `io::copy` supplies no partial-byte
count on error. The completed journal is published immutably before the worker
attempts a private unit notification. Caller code cannot inject a different event
through a public sender or replace this journal.

The copy result, notification attempt/result, receiver observation and thread
join are separate facts. Sending a unit does not establish that it was received.
A dropped receiver does not erase the completed journal. Conversely, reporter
disconnection without a completed journal is not EOF or proof of a returned read
error. Pending work and unavailable or poisoned evidence must remain explicit.
The first terminal receiver observation, including a typed report error, remains
retained. Later `try_report` calls refuse without consuming another notification
or replacing a received unit with subsequent channel disconnection. A received
unit without a completed journal is `PublicationMissing`, not EOF.
Local notification provenance does not authenticate the supplied reader, its
peer, a remote session or a native controller.

Inspection must not wait behind the reader. It uses independent phase evidence,
immutable completion storage and nonblocking lock attempts. An armed wrapper
call alone does not prove that the frozen helper returned. Busy, poison and
attempted-unreturned states cannot become completed results through inspection.
These separately observed fields are not a coherent snapshot of every layer.

## Explicit join and final lifetime

Only an explicit join may consume the retained JoinHandle. Missing handles, a
previous join attempt and joining the current worker refuse without consuming a
handle or claiming completion. The join attempt and
its actual return are retained before classification, including the original
panic payload. Join can block indefinitely. A read result can already be
published while the worker remains unjoined; successful notification is also not
join completion. No path retries an uncertain attempt, restarts a reader, or
uses a panic as permission to dispose another resource.

There is no custom Drop that cancels, closes, signals, joins or respawns. Ordinary
final destruction still matters: dropping the last resource owner can close its
reader, and dropping an unfinished JoinHandle detaches the thread. A worker's
remaining references can postpone resource destruction; they do not provide an
external recovery owner after all caller handles are lost. Process death,
allocation abort and nonlocal exits can destroy custody and in-memory evidence.
Nothing here is a durable journal, cancellation facility or teardown authority.

## Process and callback boundaries

In the pinned Unix application, `boot::Launched` is the supervisor subprocess.
That separate process starts and owns the bash child which execs QEMU. Its stdin
reader and mpsc receiver live inside the supervisor, while mixer admission lives
in the application. This local reporter does not connect those process spaces
or change the active stdin-EOF-to-termination policy.

A later consumer may use one existing scoped child observer, but this slice
provides no such adapter. The reader worker never borrows or polls a Child.
No arbitrary EOF event, local read completion, notification, worker join or
observed child exit establishes native quiescence or a stop/restart permit.

No emulator endpoint, device, service installation, package or release is part
of this source slice. Existing runtime, native-lifetime, stems, USB, physical
and recovery gates remain closed.
