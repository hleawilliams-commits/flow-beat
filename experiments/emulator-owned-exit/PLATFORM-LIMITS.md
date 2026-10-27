# Platform lifecycle limits

This is a static audit of the selected cached eframe **0.29.1** / winit
**0.30.13** sources and emulator commit
`06fc981707e69982c0554bb508e0c646c005a979`. The preview selects the glow renderer
with eframe default features disabled. [Source pins](framework-source-pins.json)
identify the inspected files and frozen prepared dependency lockfile. No window,
framework event loop, platform shutdown, supervisor, guest or backend was run to
validate these findings. Tests of an injected runner cannot supply that evidence.

## Startup can fail without creating an App

In the pinned emulator `az-preview/src/main.rs:63–90`, `start_if_needed` precedes
`run_native`; the one-shot AppCreator closure captures `launched`. In eframe
`src/native/run.rs`, event-loop creation can fail before that closure is used.
The glow `init_run_state` in `src/native/glow_integration.rs:191–342` creates its
window/context/painter before taking and invoking AppCreator. Those earlier
operations can return an error. A creator error also occurs before insertion of
`GlowWinitRunning`.

`save_and_destroy` at `glow_integration.rs:370–381` calls App `on_exit` only when
`running.take()` returns an existing App. A never-invoked creator therefore has
no App exit callback. Dropping its captures is ordinary Rust destruction, not a
worker join or recovery action. Retention solely inside the creator or App
cannot cover this startup case. An outer owner must survive the runner call and
receive its returning success/error path independently of App creation.

## Ordinary close cancellation happens before on_exit

`src/epi.rs:159–167` documents `on_exit` as a shutdown callback, not a cancellation
callback. `EpiIntegration::update` in `src/native/epi_integration.rs:262–303`
checks the root close request and the App's emitted `ViewportCommand::CancelClose`
after update. Without cancellation it marks the integration closed. The glow
integration then returns an exit event; `on_exit` is called during save/destroy.

Thus a future owned App can defer an ordinary close request while its lifecycle
owner retains unresolved work. Returning from `on_exit` cannot undo a close
already in progress. The save/destroy path takes the running state before
calling `on_exit`; that avoids repeating this particular callback from that
state, but is not a guarantee covering panic, abort or every platform path.

## macOS Cmd-Q bypasses that ordinary cancellation stage

The cached winit `src/platform_impl/macos/menu.rs:66–73` assigns Cmd-Q to the
AppKit `terminate:` action. Its delegate implements `applicationWillTerminate:`
(`app_state.rs:69–72`), which notifies windows and calls `internal_exit`
(`app_state.rs:166–171`). `internal_exit` emits `LoopExiting` at line 224. This
inspected delegate has no `applicationShouldTerminate:` decision hook.

Eframe's `src/native/run.rs:196–202` handles `exiting` by saving/destroying the
App immediately. Its comment explicitly notes that on macOS Cmd-Q,
`run_app_on_demand` may not return despite its name. This already-terminating
path is not the earlier egui close-request / CancelClose exchange. A fallback
only after `run_native` returns is insufficient, and an App exit callback must
join while its retained resources are still alive. Merely leaving an outer Arc
on the main stack does not preserve those resources after process termination.
No native termination interception or changed platform policy is implemented
by this candidate.

## The supervisor resource differs by platform

On Unix, `boot::Launched` is `std::process::Child` (`boot.rs:14`), including the
piped stdin opened at startup. The supervisor reads that pipe; EOF or channel
disconnection leads to its existing stop path (`boot.rs:67–115`). Explicit
`boot::stop` drops stdin (`boot.rs:234–237`). Dropping the last owning Child also
drops its stdin field: it need not call `Child::kill` to make the supervisor
observe EOF. Process exit closes the descriptor independently of Rust cleanup.
Holding the Child through join can defer that local close, but cannot guarantee
retention after process death or prove native quiescence.

On Windows, `boot::Launched` is only a `u32` PID (`boot.rs:16`).
`stop_with_this_app` (`boot.rs:338–370`) attempts to configure and assign a
`KILL_ON_JOB_CLOSE` job, intentionally leaving its handle open for the process
lifetime. A successful assignment has process-exit consequences independent of
App cleanup; creation/configuration/assignment failures do not establish that
binding. The returned PID is **not** the job handle or an owned process handle.
Retaining that number in a coordinator must not be described as retaining a
Windows job resource, preventing job closure, or proving process identity.
Neither platform's existing supervisor/job policy is weakened here.

## Narrow contract of the inactive successor

The successor composes an idempotent local stop/join owner with App exit and the
returning runner path. Its owned branch retains the joined mixer session and
available supervisor value through that boundary. It has no owned Quit/stop
API and no recovery permission. Main's owned port/worker pair remains absent;
no live owned backend or enable switch is introduced. Legacy behavior remains
separate.

A completed join is local worker completion. It is not neutralization, restored
host files, a cancelled native event, or permission to release the final backend
or supervisor owner. In the current main wiring, a future nonempty owned pair
would be retained outside AppCreator through the returning helper, but the owner
would then be dropped when main's match branch returns. The rejected-pair error
branch likewise eventually drops the returned resources. Literal None leaves
both owned effects unselected. These are concrete remaining enablement limits:
postponing resource release until after join does not make it authorized.
Likewise, default App/creator Drop can bypass an explicit callback unless an
independent owner remains. The returning helper is not a panic/unwind guard.
Drop, unwind, nonreturning framework termination and process death must not be
reported as successful recovery. The current in-memory candidate does not solve
those final-custody, platform termination and recovery gates.

A bounded headless seam can inject a runner that never invokes AppCreator,
returns an error after creation, invokes `on_exit` before returning, drops an App
without that callback, or invokes both cleanup paths. It can check exactly one
real join, retained endpoint identity, and absence of owned Quit/stop/resource
release while the outcome remains held. These are local source/lifetime tests;
they do not execute or emulate AppKit's termination policy. A real live owner
still needs a separately reviewed recovery and platform lifecycle contract.
