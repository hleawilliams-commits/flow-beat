# USB1 / USB2 buttons: SOURCE helper

This folder currently contains a bounded native SOURCE request helper and focused host tests. **There is no button UI, USB selection, native activation, complete mod package, or physical installation here.** The separate [observation probe](../experiments/usb-browser-switch/probe/README.md) investigates the native backend in the emulator; its findings must be assessed before connecting the two pieces.

`source_request.h` requests SOURCE through the SDK's mixer input filter. It can begin only from `AZ_VIEW_DECK`. It emits at most one 200 ms SOURCE press, then releases it; it never sends row selections, knob steps, LOAD or USB STOP. A request expires one second after acceptance. Accepted requests are debounced for 500 ms, and only one request can be live.

## API and thread ownership

Include the header in one translation unit. The owner calls init/request/tick/end on the core UI thread; the SDK calls the registered filter on the mixer bus thread. Shared lifecycle state is atomic. Pulse phase and knob history belong only to the mixer thread.

| Call | Meaning |
| --- | --- |
| `usb_source_init(core, id)` | Register the mixer filter once from mod init. Returns 1 if ready, 0 if required services are missing or registration fails. |
| `usb_source_clock_ms()` | Monotonic milliseconds, or -1 if unavailable. Use the same clock origin for request and tick. |
| `usb_source_request(now)` | Return 1 when accepted; otherwise 0. Requires idle state, nonnegative time, DECK view and a satisfied debounce. |
| `usb_source_tick(now)` | Report `IDLE`, `PENDING`, `SENT`, `CANCELLED` or `ARRIVED`. `SENT` means a pulse was emitted; it does not prove SOURCE opened. |
| `usb_source_cancelled()` | Read cancellation state. Recheck immediately before any future native activation. |
| `usb_source_cancel()` | Cancel a pending/sent request without overwriting retirement or idle state. |
| `usb_source_end()` | Request retirement after completion, cancellation or abandonment. The next mixer frame resets pulse state and reopens the gate. A new request remains refused until that acknowledgment. |

`usb_source_frame_at(...)` is the deterministic test entry point; production uses the filter registered by init. The helper does not register a native hook.

## Cancellation and arrival

`ARRIVED` is reported only when SOURCE is actually visible after this helper emitted its press. It is an observation, not completion: input monitoring and the one-second deadline remain active until the owner calls `end()`.

Any genuine navigation/LOAD bit in mixer bytes 32 or 33, browse-knob press, or knob movement cancels the request. That input passes through byte-for-byte, including after synthetic release or arrival. Changing to another screen, leaving SOURCE after arrival, a reversed clock or expiration also cancels. SOURCE appearing before the helper's first emitted press cannot count as its arrival.

The only synthetic changes are the SOURCE bit and native panel-press latch during the bounded press. Other buses, invalid/short frames and unrelated frame bytes remain unchanged. This does not yet prove the complete USB-button workflow on native firmware.

## Host verification

Run from this folder with the pinned SDK checkout beside `flow-beat`. Both exact commands below passed **592 assertions**:

```sh
clang -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -g -Wall -Wextra -Werror \
  -I../../az-mods-sdk/include tests/test_source_request.c \
  -o /tmp/usb-source-request-plain
/tmp/usb-source-request-plain

clang -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -g -Wall -Wextra -Werror \
  -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I../../az-mods-sdk/include tests/test_source_request.c \
  -o /tmp/usb-source-request-asan
/tmp/usb-source-request-asan
```

The tests cover registration/refusal, exact frame changes, one pulse and release, debounce, arrival versus emission, every navigation/LOAD bit before and after release, knob cancellation after arrival, timeout, wrong screens, leaving SOURCE and retirement before reuse. They use mock views and deterministic frames; they do not establish native timing, concurrent execution safety, actual USB activation or physical-device behavior. These commands make no TSan or emulator claim for this helper.
