# Inactive retained startup composition

This separate Unix successor combines the fixed-generation route, actual mixer
transport and existing lifecycle owner. It retains an injected backend, explicit
directory and optional supplied supervisor before initialization, then retains
each route, returned port/worker pair and lifecycle owner before later startup
steps.

A startup error or unwind can therefore leave an externally inspectable owner
with its actual resources still retained. An explicit subsequent finish can join
a captured worker through the existing lifecycle. A stop request, an incomplete
startup phase or a missing captured handle is not a joined receipt.

Read [the contract](CONTRACT.md) for the operation lease, construction boundaries,
poison behavior, supervisor custody and the unobserved interval inside the frozen
transport constructor.

This component does not start a guest, acquire a native backend, wire main execution,
confirm baseline adoption, assert released controls or activate runtime input.
The supplied supervisor is retained as an opaque resource. The source creates no
emulator or physical-device connection and provides no recovery, restoration or
restart permission.

The private candidate adds an inactive module declaration and a test-only
transport helper that selects the existing spawn-error branch. The helper
injects a returned error; it does not establish that an actual operating-system
thread creation failed. Parent production behavior remains unchanged.

[Host evidence](HOST-TESTS.md) records production compilation, 16 focused tests
and 12 strict source controls, independently reproduced and audited from retained
files; 236 inherited tests remain compiled/listed and filtered. These use actual local worker/lifecycle types with synthetic backend
and supervisor values. Read [the boundary review](REVIEW.md) before any native
integration. Existing runtime, stems-staging, native-ownership, USB and release
gates remain CLOSED.
