# Queued producer review and required handback contract

This is an offline source/design review. No preview, bridge, driver or kernel source was changed; no existing test suite was rerun and no runtime endpoint was contacted. The [phase-1 fence](../emulator-overlay-ownership/README.md) and [writer counterexamples](../mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/writer-ownership/README.md) remain separate evidence. This note specifies missing producer work; it does not establish handback readiness.

The selected successor design is a **broker-held controller file with an atomic generation rotation**: the same owning file and run remain, the current exact-next-sequence authorizes ROTATE, the epoch advances, sequence becomes zero and the complete supplied frame replaces the overlay together. A module-instance identity must also bind mutations. These are design premises for the separate kernel successor, not evidence of installed behavior. Neither an offered-owner/ACCEPT protocol nor legacy unfencing is required for this narrower design.

## Pinned source facts

The following local files at emulator commit `06fc981707e69982c0554bb508e0c646c005a979` were read and hashed. Full source copies and user settings are not included here.

| Source | SHA-256 |
| --- | --- |
| `az-preview/src/controls.rs` | `b9145e4b41439761abbfdec461b854412748af2fe95183fd5843ad34d4298445` |
| `az-preview/src/app.rs` | `f6a7bb28c3f2dbce759582ac2c7b1147adf93f5137838a833ed5d35f1834864b` |
| `az-preview/src/config.rs` | `0a1986611ec2642379a42f1f517d8d1decf1764e0cee8ab2f37c3e6c04782992` |
| `az-preview/src/main.rs` | `a13cf052beca509185cb71a56e4196624e0cf6e34d5e62ea43e8fe5f03abecde` |
| `guest/subucom/forwarder.c` | `88ea53aa7a3e0dd022a9b0ca494737cf799eb82b6c56546e076a7493a1879c57` |
| `tools/az-drive.py` | `6f88798ec54785dc2ce9180e5c2839a1a8e8ec370c997bc7f48e8d1cc06e640f` |

- `main.rs:47–59` creates an ordinary `std::mpsc` channel and starts the control thread. `controls.rs:847–856` gives `CtrlMsg` only a bus and whole frame; its wire representation has no identity, generation or receipt.
- `controls.rs:868–885,908–953` caches each bus's whole frame, drains disconnected queues into that cache, and replays cached frames on reconnect. It records a message before attempting `write_all`. A transport error can therefore cause the same held frame to be attempted again. A successful write is not a kernel acknowledgment.
- `app.rs:451–475,578–659,781–837` maintains browse gestures, held/pending buttons and X-PAD, adopts the shared knob counter, and updates `sent_mixer` immediately after attempting channel send. The UI can consequently suppress a necessary baseline send even though its earlier frame never committed. Browse handling writes the shared counter before transmission.
- `config.rs:62–97` treats saved counter/analog files as shared state, with ignored write errors. Sender-side analog persistence follows socket success/replay, not kernel acceptance. Those files are neither a producer barrier nor acknowledgment.
- `forwarder.c:175–204,313–350` reads fixed 153-byte messages, forwards them without a guest result message, and closes/reopens its controller descriptor on write error. Its partial-read EOF/error path supplies no transaction identity or cross-reconnect synchronization. The LED path has separate fixed-size framing. An owning adapter must not inherit the generic reopen-on-rejection behavior: closing its file would orphan its lease.
- `az-drive.py:105–132` retries socket sends and then uses a direct SSH write fallback. That fallback has no generation or kernel receipt. Old untagged mixer traffic must remain fenced, including direct writes and existing descriptors.

## Smallest safe producer protocol

Keep a single broker's controller description for the owned interval. All mixer producers, including preview and bounded actions, use that broker; other buses and LED/level/MOSI output remain independent. This is a cooperating-components protocol, not protection against arbitrary privileged code.

1. **Tag at creation.** Under a common producer admission gate, create each mixer message with its granted module instance, run, epoch and a unique producer request identity. Carry these unchanged through the UI channel, reconnect cache and transport. The broker may allocate the kernel sequence only after admitting that unchanged generation. It must never attach the current epoch to an old untagged or stale queued frame. A new socket is not a new grant.
2. **One unresolved mutation.** The smallest accounting contract serializes broker ioctl mutations and permits one unresolved ordinary request at a time. Rotation is not a license to guess the next sequence while a prior ioctl remains unclassified. The broker retains request identity, exact payload, assigned sequence, whether the ioctl was attempted and its result. A duplicate identity with different bytes is rejected. An acknowledgment identifies kernel commitment or definite rejection; it never claims native consumption. Cached acknowledgments must not re-execute a committed request.
3. **Close admission before transfer.** Stop new mixer events and all corresponding saved-counter/analog writes before a producer acknowledges quiescence. Serialize this gate with message creation, including pending-button and X-PAD paths. Require UI gestures and momentary controls released; do not silently convert an active press into a neutral frame. Account for queued requests as completed, canceled before attempt, or unresolved. Keep the destination producer closed during this transition.
4. **Revoke at the final boundary.** A successful ROTATE advances the kernel epoch atomically with its supplied frame. Old-generation requests that remain in an upstream queue or were copied before rotation cannot subsequently commit under that old epoch. A local queue drain or a STATE snapshot alone cannot supply this revocation. The broker must not reopen the old generation after a failed connection or lost acknowledgment.
5. **Adopt a verified baseline before granting input.** Validate the full rotated frame and its neutral-control policy separately. Retain analog/packed fields, independent FX TIME values and the agreed browse counter; do not use an idle template as a substitute. Confirm the expected instance/run, new epoch, sequence and bytes, then make the destination explicitly adopt that baseline in its frame, counter and derived UI state. Invalidate the old mixer reconnect cache and distinguish acknowledged state from `sent_mixer`'s current optimistic enqueue state. Force a baseline acknowledgment even when a byte comparison would otherwise suppress transmission. Re-arm only after release of any pre-transfer UI gesture; accept subsequent fresh gestures under the new grant.

The kernel primitive does not validate the supplied frame's neutrality or the producer's readiness. Stopped recovery and exact host-file restoration remain separate prerequisites where required by the experiment. The broker must not write settings after its source generation has been revoked, or restore a counter and then let an old UI gesture immediately overwrite it.

## Uncertain writes and reconnects

| Observation | Permitted conclusion |
| --- | --- |
| Local enqueue or successful socket write | Bytes were accepted locally/by the transport; kernel result unknown. |
| Broker records a completed ioctl returning zero for the bound request | That overlay mutation committed. Native sampling/dispatch remains unknown. |
| Broker records a definite pre-commit rejection for the bound request | That attempt did not commit; this does not classify another copy or retry. |
| Same-instance, same-owner, one-unresolved-request STATE shows its exact sequence and exact payload | Its commit can be reconciled under those accounting assumptions; no native delivery acknowledgment. |
| STATE still shows sequence `n-1` after a timeout | Not cancellation: the request may remain upstream and arrive later. |
| Lost reply, parser ambiguity, missing broker accounting or module-instance change | Unknown; hold admission closed and use the separately reviewed recovery path. |

A definite “will never commit” result requires either a known cancellation before any possible submission, or successful generation revocation that makes every remaining copy permanently stale. Merely not finding a request in a broker's current receive buffer is insufficient. With strict one-unresolved-request accounting, reconcile a lost response through retained broker state and the same kernel ownership instance. Do not blindly resend a press, RELEASE or ROTATE. After a broker crash, its new file is a different owner; reconnect must not silently reclaim and replay the old queue.

The existing 153-byte input and fixed-size LED reader cannot simply accept new tagged request/ack bytes. A separately framed, bounded, versioned protocol or dedicated owned-mixer transport must define partial messages, EOF, reconnect and session binding without relabeling buffered bytes. Preserve existing other-bus/output paths rather than stopping the forwarder or closing the preview, which may own QEMU's lifetime.

## Required source changes and proof boundary

The preview needs generation-bearing `CtrlMsg`, explicit admission/quiescence acknowledgments, generation-aware cache invalidation, acknowledged baseline adoption, and gating of mixer gestures plus saved-state writes. The sender needs typed guest results and explicit unknown outcomes instead of unconditional reconnect replay. The owning guest adapter needs serialized request accounting and tagged ioctls; expected stale/duplicate refusals must not trigger the legacy close/reopen loop. Driver mixer commands must use the owned protocol or fail clearly while fenced; no untagged fallback may be treated as success.

New tests should force a held old frame through each queue/reconnect stage across rotation, a producer racing quiescence, an uncertain write followed by a misleading `n-1` snapshot, lost ROTATE acknowledgment, partial transport input, broker close/restart, and baseline equality suppressing the required send. Source-level and synthetic tests still would not prove actual QEMU/virtio buffering, firmware scheduling or recovery.

“Stale input excluded after transfer” must be scoped precisely: a new generation can exclude later old-generation **overlay commits**. It cannot retract native events already sampled or queued before rotation. Full native-event quiescence requires separate evidence. No queue/broker adapter, handback completion, neutral recovery or installation is established by this note.
