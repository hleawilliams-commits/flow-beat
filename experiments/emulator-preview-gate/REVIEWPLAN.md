# Offline preview integration review plan

This plan reviews the new, inactive preview candidate against emulator commit
`06fc981707e69982c0554bb508e0c646c005a979` and the frozen broker/producer experiment
at flow-beat commit `616ba866d45eb97746ddfa35ebdd3e7d6df1d84c`. It is a source review
and regression plan, not a passing test receipt or runtime authorization. The
original emulator, older candidates, configuration files and devices remain
untouched.

The selected scope is an injected mixer endpoint, actual App/controls integration,
and a copied producer with a combined exact-ACK/settings callback. There is no
live backend or new file persistence implementation; `main` supplies no owned
endpoint and `config.rs` remains unchanged. The callback can establish ordering
of synthetic effects, not ownership, durability or atomicity of the two existing
host files.

## Exact source concerns

| Pinned source path and line | Existing behavior relevant to the new boundary |
| --- | --- |
| `az-preview/src/app.rs:446` | Browse input derives a counter from the shared file and stores it before enqueue; a send-only gate cannot fence this effect. |
| `az-preview/src/app.rs:203` | Mixer float models are initialized separately from the raw frame; FX SELECT and FX TIME have separate model values and drag accumulators. |
| `az-preview/src/app.rs:578` | One held/pending collection contains Mixer and ERP buttons. Clearing it wholesale loses other-bus input. |
| `az-preview/src/app.rs:719` | Selector and FX TIME retain fractional drag state; FX TIME is an independent little-endian two-byte value. |
| `az-preview/src/app.rs:757` | X-pad state is regenerated from a held pointer while the panel is visible. |
| `az-preview/src/app.rs:786` | Shared-file counter changes update both the raw and optimistic sent frame, assuming guest delivery. |
| `az-preview/src/app.rs:829` | The mixer sent cache advances despite an ignored enqueue result. It is not an acknowledged frame. |
| `az-preview/src/app.rs:911` | Screen-only view skips panel handlers but still composes/sends frames. Visibility does not close admission or prove pointer release. |
| `az-preview/src/controls.rs:868` | The generic latest-frame cache stores untagged values, including disconnected input, and replays them after reconnect. |
| `az-preview/src/controls.rs:941` | Legacy mixer settings are saved after socket-write success, including replay; this is not a kernel ACK. |
| `az-preview/src/controls.rs:958` | The same legacy stream carries fixed-size LED frames in the other direction. A new ACK protocol cannot be appended without an explicit framing design. |
| `az-preview/src/config.rs:67` | Counter persistence truncates/writes directly and ignores errors. |
| `az-preview/src/config.rs:76` | Mixer persistence merges prior bytes and uses a fixed temporary filename, ignoring rename errors. No generation/request ordering is present. |

Source SHA-256 pins:

- `app.rs`: `f6a7bb28c3f2dbce759582ac2c7b1147adf93f5137838a833ed5d35f1834864b`
- `controls.rs`: `b9145e4b41439761abbfdec461b854412748af2fe95183fd5843ad34d4298445`
- `config.rs`: `0a1986611ec2642379a42f1f517d8d1decf1764e0cee8ab2f37c3e6c04782992`
- `main.rs`: `a13cf052beca509185cb71a56e4196624e0cf6e34d5e62ea43e8fe5f03abecde`

## Discriminating regression oracles

1. **Creation and adoption.** Start owned mode closed, including screen-only mode.
   No mixer frame, counter store or settings callback may occur before the exact
   baseline is adopted, acknowledged and a subsequent input release is observed.
   Use a nondefault full frame with nonzero packed low bits, selector nibble and
   independent FX TIME bytes. No-input composition after adoption must preserve
   all 152 bytes. A subsequent individual control change must use the adopted
   model, not constructor defaults or old drag residues. Baseline equality with
   a prior sent frame must not bypass explicit adoption.

2. **Held input and selective clearing.** Combine Mixer held/pending buttons,
   an ERP held/pending button, SHIFT, X-pad and partial Browse/FX drag state.
   Close/adopt must invalidate the Mixer contributions while preserving ERP,
   deck state and SHIFT. A pointer held throughout adoption cannot immediately
   recreate a press, pad value or old drag; test release and later fresh input.
   Exercise a panel-to-screen-only transition so the barrier does not depend on
   calling visible widgets. A cached neutral boolean from before adoption is
   insufficient evidence of release after that baseline.

3. **Actual transport selection and caches.** Deliver an old queued envelope
   after close/new adoption, including a numeric request-ID collision in the
   new epoch. It must retain its old identity and be rejected rather than
   stamped at dequeue. In owned mode no bus-2 value can enter legacy send,
   disconnected cache, replay or socket-success settings paths. Bus 0/1/3 wire
   bytes, their cache/replay behavior and the LED read path remain unchanged.
   With `Links.mixer=None`, legacy behavior (including bus 2 and its existing
   settings path) remains selected. With `Some(owned_route)` whose current key
   is absent, owned Mixer is closed. A failure in the owned endpoint must not
   fall back to legacy bus-2 transport. Test both distinct states.

4. **Exact positive ACK and ordered settings.** Use the exact retained request
   frame/full key/ID plus kernel sequence. Enqueue success, transport write,
   low STATE sequence and an unknown result produce no settings effect. Delay
   the first settings callback and attempt later admission/close/adoption;
   the selected integration lock must prevent an older callback from executing
   after a newer committed settings effect or after generation adoption. An
   exact duplicate ACK has no second effect. Alter each ACK component and
   require no callback. Check the exhausting successful ACK: its admitted
   callback runs once before final closure. A positive ACK received after the
   gate was already closed records the known native result but performs no
   settings callback and does not reopen admission.

5. **Settings failure and uncertainty.** A callback error after a simulated
   partial side effect leaves the native request known committed and the gate
   closed; it must not become an unknown operation or be resent. Callback
   panic must fail closed. A genuinely unknown native result retains its
   original unresolved envelope and blocks adoption/rotation until resolved;
   a later positive result does not rearm. The test callback uses owned memory
   only. No test result may imply rollback of host files or native input.

6. **Model versus acknowledged state.** A queue failure or disconnected receiver
   cannot advance the acknowledged baseline or persist desired settings. A
   shared counter-file change must not be treated as a guest ACK in owned mode.
   Preserve distinct desired/queued/acknowledged meanings. Keep an explicit
   press/release sequence through backpressure; dropping a release cannot be
   described as restoring a neutral overlay. Existing UI minimum-hold time is
   not proof of native duration when delivery is delayed.

The existing `mixer_levels` projection has 51 analog offsets. A saved 54-offset
map can additionally contain selector byte 22 and FX TIME bytes 26/27. These
three values and Browse byte 34 are independent. The present injected callback
should carry the complete ACKed frame; it does not change or normalize the
existing file schema. A future persistence implementation must define exactly
which bytes it owns and preserve the original bytes/modes needed by recovery.

## Source-level negative controls and execution scope

Prefer narrow compiling mutations that re-enable one bypass at a time: legacy
bus-2 cache admission in owned mode; current-key stamping of an old queued value;
removal of post-adoption release gating; retaining stale FX model/drag state;
restoring an owned-mode shared-counter read/write; or callback-before-exact-ACK
validation. Require the specific expected failed assertion, not any compile
failure or panic. Existing frozen standalone tests need no unchanged rerun.

New tests should call the actual changed App, producer and route methods with
in-memory channels, backend receipts and callback state. Before running a broad
crate test command, inspect the selected tests for configuration, socket, boot,
process or file access. Do not start `ctrl_thread`, invoke application shutdown,
use the real settings sink, or connect to the emulator merely to exercise a
unit test. Retain exact candidate inputs, test inventory and results for the
independent run. Headless egui event tests can establish the exercised UI event
sequence; they do not prove every window-system or physical-input lifecycle.

The baseline receipt remains externally trusted. No current source-only result
can authenticate a broker grant, drain a real socket or UI backlog, prove that
native code consumed a frame, cancel earlier native events, establish process
restart recovery, or give another producer safe handback. Those are separate
integration boundaries, not features supplied by this inactive patch.
