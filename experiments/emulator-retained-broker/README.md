# Retained broker lifecycle

This inactive successor supplies broker custody before synchronization setup or
initial backend validation. It prepares the remaining lifecycle boundary needed
to compose the separate [CLAIM owner](../emulator-claim-owner/) and
[retained adapter](../emulator-retained-adapter/) with the actual broker and
dispatcher. That complete graph is still unimplemented.

The frozen broker constructor leaves its output null during initialization and
snapshot validation. On snapshot failure it ignores mutex-destruction failure
before freeing its allocation. Its void destructor intentionally keeps storage
when mutex destruction fails, but cannot report that retention to a graph owner.
These are source findings; no live failure is claimed.

This successor excludes those two lifecycle bodies. The exact frozen helper and
operation bodies before mb_create and from mb_adopt onward are included without
changes. Existing operation semantics and the wire dispatcher stay frozen.

## Staged acquisition

mbro_create validates and copies the expected state and backend pointers, then
returns an owner without a pthread or backend call. Backend context remains
borrowed: an outer owner must keep that context alive, including on failure.

mbro_initialize performs one attr initialization, ERRORCHECK configuration and
mutex initialization. Each call has retained intent and raw return. Successfully
initialized resources stay in the owner even when a later stage fails. The
attribute object remains held after successful mutex initialization.

mbro_validate makes one snapshot callback, preserving its raw result and
zero-initialized/returned semantic value before checking full correspondence.
Callback error, invalid shape or disagreement keeps the owner, synchronization
objects and borrowed backend without exporting a broker. These semantic bytes
are diagnostic evidence when the callback failed. Matching validation alone
does not establish native ownership or baseline authority.

Only Ready exports a borrowed embedded broker for existing mb_* operations and
the dispatcher. Setup repeats perform no work or journal replacement. The
lifecycle journal records acquisition; it is not a current operational snapshot.

## Observable disposal, external preconditions

Before disposal, the caller must independently establish quiescence, retire every
broker/dispatcher borrow, and resolve or account for operation uncertainty.
The first attempt permanently closes export and invalidates all earlier broker
borrows, including when disposal fails. The component cannot enforce exclusion
of raw pointer aliases.

Each explicit call attempts destruction of the remaining mutex once, then the
attribute once only after mutex success. A nonzero return keeps owner storage
and all remaining resources. If the mutex is destroyed but attribute destruction
fails, the broker can no longer be used: only lifecycle inspection and an
independently justified later disposal attempt are permitted. Such a later
attempt skips the already-destroyed mutex.

First and latest reports, attempted/returned flags, raw results and a bounded
attempt counter remain inspectable while storage exists. There is no automatic
retry. A successful call consumes the owner only after all initialized resources
have been destroyed and returns its final report. Null-owner disposal is a no-op.
The overflow branch is source-reviewed; the host fixture does not fabricate a
private counter value to claim runtime exhaustion coverage.

This establishes an observable local resource result under stated preconditions.
It does not establish those preconditions, close a backend descriptor, release
a native overlay, restore state or authorize recovery. Backend ownership remains
the outer caller's responsibility. Needed evidence must be copied before free.

## Evidence and remaining work

[Host evidence](HOST-TESTS.md) distinguishes actual production pthread-wrapper
execution from scripted synchronization refusals and synthetic backend values.
The private retained-file audit checks source generation and logs without
rerunning tests. Raw builds, logs and audit records stay ignored under private/.

Next compose CLAIM, adapter, retained broker and dispatcher while preserving
every acquired child through failed construction. Add owned transport and
supervisor binding before live wiring. External writer/alias exclusion, native
baseline and stop authority, deadlines, full overlay/both-file recovery and
durable/process-death custody remain open. No emulator/device endpoint is used,
no runtime input or installation occurs, and all existing stems, USB, observer,
recovery, audio-quality, physical-timing and release gates stay closed.
