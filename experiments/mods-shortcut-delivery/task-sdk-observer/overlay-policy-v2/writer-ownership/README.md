# Overlay writer ownership: existing paths and remaining gate

**The current sources provide no exclusive ownership of the controller overlay or its CRC parameter.** The coordinator's local lock, a stable process list, an empty socket observation and two matching frame samples cannot establish that ownership. There is no existing zero-change exclusion route that also preserves the running development setup and safely handles pending preview frames. This audit opens no runtime gate and implements no broker, process control or kernel change.

The pinned-source verifier passes **58 checks and 18 source/semantic rejection controls**. A new fixture executes the exact extracted Rust `LatestFrames`, `wire_frame` and `ctrl_thread`: **17 assertions and three strict executed controls** pass. It uses real `std::mpsc` queues but fake UnixStream, sleep, LED-reader, logging and persistence functions. It makes no socket, emulator, device or process-control call. All generated extracted source, binaries and logs are ignored under `private/`; the public result contains hashes and bounded findings only.

A separate parent execution reproduced the complete result byte-for-byte
(SHA-256 `3eeed07dce434e95b2579774b7e87285c01564dcfda3ec54c0f25a72a481b669`).

From the repository root:

```sh
python3 experiments/mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/writer-ownership/verify.py \
  --source-root ../xdj-az-emu \
  --result /tmp/az-writer-ownership.json
```

The verifier checks 13 exact local source files. Its inventory is limited to those sources and the reviewed local references; it is not a census of installed guest binaries, arbitrary scripts, existing descriptors or later root commands. The module's runtime identity must still be checked separately. Existing policy, private helpers, closed runtime receipts, source/binary candidates, settings and unrelated development remain unchanged.

## Concrete writer paths

| Path | What the pinned source does | Ownership consequence |
|---|---|---|
| Preview UI → control channel | `app.rs::send_frames` composes a whole mixer frame, including held/pending buttons and X-PAD, then sends it through a normal `std::mpsc::channel`. | No coordinator lock, lease token or drain acknowledgement participates. A producer ceasing to send does not remove already queued frames. |
| Preview control thread → `ctrl.sock` | `controls.rs::ctrl_thread` retains the last frame per bus, drains pending messages while disconnected, and sends retained frames when connection succeeds. It remembers a received frame before attempting its write. | Disconnect does not invalidate cached state. Reconnect can replay it; a write error does not prove no delivery. It persists mixer analog fields only after a successful send/replay, so unchanged saved settings are not an acknowledgement that no frame was sent or queued. |
| QEMU control endpoint → guest forwarder | The boot script exposes the host socket as `cdj3k.ctrl`. The guest forwarder reads complete 153-byte messages and writes them to an already open `/dev/subucom_ctrl` descriptor. | Sampling the host connection does not reveal pending UI/channel/socket/QEMU/virtio/forwarder bytes or revoke the guest writer. Actual QEMU buffering and drain semantics were not exercised here. |
| `az-drive.py` → socket or direct SSH | `send` uses the socket with retries, or a root SSH `cat > /dev/subucom_ctrl` when the preview is detected or socket paths fail. The preview-running result is cached. | The socket is not the only ingress. Closing or monopolizing it does not exclude direct writes. The driver's retry logic has no shared coordinator lease or exactly-once delivery acknowledgement. |
| Reviewed coordinator → direct guest write | The separate coordinator's retained action payloads use direct guest writes under its cooperative host lock. | This serializes participating coordinator calls only. It does not intercept preview or arbitrary az-drive/root commands. The coordinator implementation is not retested by this fixture. |
| Other process with controller write access | `ctrl_open` and `ctrl_release` return zero without owner state. `ctrl_write` ignores its file-object argument and accepts any opener meeting ordinary access checks. | The source has no exclusive-open, owner token or per-file epoch. An old writable descriptor still reaches the same global overlay. A `flock` taken by one client would not make these existing writers participate. |
| CRC sysfs/module parameter | `spi_crc_len` is a module parameter with mode 0644. `spi_read` copies the overlay under `miso_lock`, unlocks, then reads the current CRC prefix length. | The overlay spinlock serializes copying/replacement; it does not freeze CRC configuration across checks or an action. An authorized concurrent parameter writer can alter later restamping. |

The kernel accepts full sticky overlays, a one-byte command clearing one bus, and a zero-length handler call clearing all buses. A future owner check must cover **every mutating command**, including clear operations, rather than only full 153-byte writes. The C handler's zero-length branch does not prove that every userspace zero-byte write API reaches it. Closing the controller descriptor does not reset the sticky overlay.

The guest `subucom-forwarder.service` is configured with `Restart=always` and a two-second restart delay. Killing that process is not a durable exclusion. Stopping or runtime-masking the service could suppress that particular forwarding path while effective, but does not stop direct guest writers or CRC changes. Restart also needs a backlog/resynchronization design; no reviewed acknowledgement currently proves the old stream is empty.

The forwarder serves LED frames, audio level output and the mixer MOSI page as well as host control input. Stopping it changes those other development functions. Its mutex serializes its outgoing vport frames, not incoming overlay ownership.

## Adjacent routes and lifecycle effects

- The ordinary SPI ioctl path publishes **MOSI/LED state** separately; this is not the full sticky MISO overlay writer. It also supports timing/configuration ioctls. A stopped overlay writer does not freeze all native input/timing state.
- `guest/subucom/live.c` opens its selected device **O_RDWR**, including the controller device in MOSI mode, but the reviewed file contains no explicit `write` or `ioctl` call. A writable-descriptor census must distinguish potential write access from observed behavior; it cannot label this descriptor read-only.
- The shim has a separate GPIO virtio ingress that changes GPIO levels and wakes native readers. QMP/touch and other UI inputs are likewise outside a mixer-overlay byte lease. They matter if the eventual experiment claims exclusive navigation/event provenance rather than only mixer-byte preservation.
- Closing the preview is not a safe writer-only operation. When it owns the launched emulator, `App::on_exit` asks QEMU to quit and calls the supervisor stop path. The Unix supervisor also stops its child when its lifetime pipe closes. Killing or closing the app can therefore stop QEMU. No such action was taken.
- A fresh boot clears saved host controller state and may run other startup helpers. It is not a minimal way to exclude this writer while preserving the current development state. This audit neither recommends nor performs a reset/reboot.

## Actual-source counterexamples

The sender fixture supplies synthetic full frames with a held BROWSE bit solely to show why stale frames must not be treated as neutral. No real or saved user frame is used. Its controlled schedules establish:

1. **Producer disconnected, queue still pending.** Two messages are enqueued and their sender handle is dropped before the extracted control thread starts. Both queued frames are still written in order; receiver disconnection is reached only after them.
2. **Connect failure followed by success.** The first fake connection fails. The real disconnected branch drains the two queued messages into `LatestFrames`. The second fake connection succeeds and replays the last whole frame, including its button bit, without another UI message.
3. **Uncertain write, reconnect and replay.** A fake write records its attempted bytes and returns an error. The unchanged sender reconnects and attempts the same retained frame before processing the next queued message. This proves repeated source attempts. The fake's record is not evidence that a real socket delivered twice.

Three controls alter only the generated private copy: dropping disconnected latest-state capture, dropping capture before the write attempt, and skipping replay. Each must produce exactly its expected two failing observations; unexpected build errors, timeouts, exit status or assertion sets fail the verifier. The candidate source definitions remain byte-for-byte extracted. This is a serial deterministic test of the selected sender behavior, not a concurrency, OS transport, kernel queue or native delivery proof.

Two preparatory audit mistakes were retained privately: an initial O_RDONLY assumption for the live viewer failed before fixture execution, and the first tuple-struct extractor stopped at an array type's semicolon. The final checks use the actual O_RDWR/no-write finding and the corrected bounded declaration extractor. Neither failure prompted a production-source change.

## Minimal route still needing engineering

With the current interfaces, an operator can promise cooperative quiescence and sample named processes, services, descriptors and parameters. That is a stated operational assumption, **not genuine enforced ownership**. It should not be renamed a lease or used to silently relax the current runtime gate. Merely pausing a process also preserves its pending frames; resuming it requires the same backlog treatment.

The smallest concrete arbitration design to review next is a **per-mixer-bus, run-scoped owner at the final shared mutation boundary**, with an explicit policy for CRC writes. It must provide all of the following before claiming an owned bounded run:

1. Acquire a unique owner/generation before taking the baseline. Reject every non-owner overlay/clear mutation for that bus while ownership is active, including calls through descriptors opened earlier. All writers must traverse this enforcement. A userspace broker alone is insufficient if privileged direct writes can bypass it; an access-confinement design or a separately reviewed kernel enforcement change would be needed. Neither is implemented here, and general root override remains outside any ordinary guest-local security claim.
2. Freeze the applicable CRC setting for the owned interval, or mediate changes under the same owner/generation. Re-reading a writable value sequentially is only drift detection, not this guarantee.
3. Establish an ingress barrier that accounts for already queued preview/channel/socket/virtio/forwarder frames. Stale frames must not become valid later merely because ownership ends. Every resumed producer needs an explicit fresh-state/generation handshake; do not blindly replay its cached frame or discard queued actions without a documented acknowledgement policy.
4. Preserve other buses and the forwarder's LED/level/MOSI outputs. Capture/release must not stop the preview's QEMU supervisor, replace cores, alter saved settings, or depend on an old emulator snapshot.
5. Define crash and partial-write behavior. A lost owner must not authorize replay or silently reset the sticky overlay. Keep diagnostics possible while intervention stays closed; relinquishing ownership is separate from proving a neutral controller/view or restoring earlier state.

This is a design requirement, not a proposal to install an unreviewed kernel patch or start a new live run. Existing full-frame equality and V2 payload preservation remain useful checks, but they cannot supply these missing writer-ownership guarantees.
