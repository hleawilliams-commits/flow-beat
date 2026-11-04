# Inactive retained reader reporter

This separate successor owns a supplied `Read` value and the single thread it
creates. It retains the reader before spawn, stores the actual JoinHandle before
releasing a private start gate, and publishes the full frozen ReaderJournal
before sending a private unit notification. Spawn, copy, notification, receive
and join outcomes remain distinct. Explicit join retains the actual panic
payload; diagnostics do not wait behind a blocked reader.

The candidate composes the frozen
[supervisor-admission](../emulator-supervisor-admission/) tree. Only an inactive
module declaration changes an inherited file; two new source files make the
candidate inventory 34. No existing owner, Child, mixer port, endpoint or main
selection is replaced. The original emulator checkout remains unchanged.

Read [the contract](CONTRACT.md), [source and fixture review](REVIEW.md), and
[focused execution evidence](HOST-TESTS.md). The [sanitized receipt](result.json)
binds the reviewed source, test inventory, controls and independent audit.

## Verification boundary

The focused runner compiles the actual inactive crate and selects eight new
tests plus four compiling mutation controls. It does not execute the 267
inherited selectors. Raw logs, executables, temporary source trees and any
failed attempts stay under ignored `private/`.

The offline [auditor](audit-results.py) is a required second verification step.
The runner checks an expected final assertion but, alone, does not reject an
additional earlier fixture panic. The auditor requires exactly the expected
panic records and locations: one final oracle for each strict control, one
intentional worker panic for its positive fixture, and none elsewhere. It also
verifies retained source, compiler artifacts, executable hashes, complete log
inventories and semantic agreement between two fresh matrices. Four separate
in-memory parser controls reject extra, unparsed, missing and wrong-location
panic records without rerunning a test.

For an intentional source change that warrants new checks:

```sh
python3 run-tests.py --emu-source /path/to/pinned/xdj-az-emu
python3 run-tests.py --emu-source /path/to/pinned/xdj-az-emu --host-only
python3 audit-results.py /path/to/first/private/matrix /path/to/second/private/matrix
```

Do not repeat an unchanged passing matrix. Preparation verifies 48 frozen input
files before composing this slice. It does not launch the emulator.

## Remaining integration

The pinned application owns a supervisor subprocess; the supervisor owns the
distinct child that execs QEMU. Its detached stdin reader currently discards
the copy result and routes either EOF or channel disconnection into termination.
This inactive local reporter changes none of those executable branches. The
one-way stdin pipe supplies no return channel to the application's admission
owner.

A next owned launch design must resolve that two-process topology, the actual
continuous child-observation scope and retained controller/status transport.
Reader completion is not native quiescence, child cancellation or a stop/restart
permit. No final Drop joins or cancels, but ordinary last-owner destruction can
close the reader or detach an unfinished thread; process death is outside the
in-memory custody guarantee. No physical timing, emulator runtime, observer,
stems activation, USB completion/cancellation or recovery gate is opened.
