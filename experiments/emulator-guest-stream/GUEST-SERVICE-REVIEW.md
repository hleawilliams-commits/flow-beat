# Guest service and supervisor boundary review

This is source review, not an executed test or a deployment plan. It identifies
what the retained stream still needs from a real guest service and supervisor.
No program was built or run, no endpoint was opened, and no running emulator or
physical deck was contacted for this review. Existing host/process evidence and
ARM64 build evidence remain separate.

## Source identity

The five upstream files below were read from commit
`06fc981707e69982c0554bb508e0c646c005a979`. Their selected working-tree contents
match that commit. Line references in this review refer to these exact bytes;
`boot.rs` also matches the existing preview/owned-exit source pin.

| Upstream path | SHA-256 |
|---|---|
| `az-preview/src/boot.rs` | `f907da478aa758d4cb2a9f740f529fbe256376230d77635a061a746ce75ec1eb` |
| `az-preview/src/app.rs` | `f6a7bb28c3f2dbce759582ac2c7b1147adf93f5137838a833ed5d35f1834864b` |
| `guest/subucom/forwarder.c` | `88ea53aa7a3e0dd022a9b0ca494737cf799eb82b6c56546e076a7493a1879c57` |
| `initramfs-patch/patch-rootfs.d/11-virtio-stream-services.sh` | `8c123e8d0ad2f353c33e45ef4a7ca44b5a390ea98ca042b96246267cf1f848f4` |
| `tools/az-boot-m1.sh` | `65b4c3b298c51819ccb6dbed526883bdc9a47ce74e29e096c4eccf1d9b2bc285` |

The relevant frozen local sources are:

| Local source | SHA-256 |
|---|---|
| [owned_startup.rs](../emulator-owned-startup/owned_startup.rs) | `c82c949ec8af5d9d5b3579b2542af3fafab246d33c2c880add7570a0a9ef504d` |
| [stream.h](stream.h) | `1d888942b772540ae9e7a9d698a238519ff04af6278489181664f2f37c91c4a6` |
| [stream.c](stream.c) | `0e0606ddfd92b537744a71b2e0bcf29bec1a627c57b81a83214c795d27631cfb` |
| [process peer](../emulator-stream-process/peer.c) | `c014fd431b4dd973a40dcb53cc2efbc4a994807120c7604613be51c2ba89fb88` |
| [process checker](../emulator-stream-process/check.rs) | `a95521e88c19fae451ca21e919eafd053d843cdc8277f2f6368bd5b9be0011d5` |

## Existing transport and service conflict

The boot script already binds `ctrl.sock` to `cdj3k.ctrl` at lines 149–150.
The forwarder reads host control records of exactly 153 bytes and writes LED
and level records of 157 bytes (`forwarder.c:34–43, 211–231, 276–283, 313–320`).
The new broker's framed request/receipt protocol cannot be placed on this stream
unchanged: existing readers would consume its header as a legacy record, while
unsolicited LED/level records would corrupt broker responses.

A separately allocated duplex virtio stream is required for the proposed broker
service unless a new multiplexing protocol is independently designed at both
ends. Sharing the existing stream or stopping its forwarder would also affect
native input and telemetry. This review selects neither a new endpoint name nor
an installation change.

The old forwarder is not a suitable retained-service loop:

- Lines 142–172 retry controller/vport opens without a retained attempt journal.
- Lines 304–311 detach the LED and level threads; the main loop has no join owner.
- Lines 320–338 retry vport EOF and close/reopen it after an error. A partial
  `read_exact` prefix is local stack state and is not preserved across that path.
- Lines 341–345 close/reopen the controller after a failed write. That does not
  distinguish a rejected operation from an operation whose outcome is unknown.
- Lines 217–221 exit the process on a controller read failure, independently of
  the main loop and its descriptor custody.

The installed-unit generator compounds this mismatch: lines 32–38 set
`StartLimitIntervalSec=0`, `Restart=always` and `RestartSec=2s`. Automatic restart
would replace a lost process; it would not recover its original open-file claim,
partial wire exchange or unresolved native result. A future broker unit needs a
separately reviewed death/restart policy, not a copy of this service definition.

## Host supervisor conflict

The pinned `boot.rs:86–90` discards the result of the stdin reader and sends the
same signal for EOF and read failure. At lines 103–114, that signal or reporter
channel disconnection enters `stop_child`; an error from `try_wait` instead exits
the supervisor with status 1 and relinquishes its local child owner. The stop path
at lines 122–135 sends SIGTERM and then can kill/wait, discarding signal and wait
errors. `wait_exit:141–145` treats any result other than `Ok(None)`, including an
error, as completed exit. `stop:234–237` closes the supervisor stdin and returns no
checked teardown receipt. Legacy App exit also sends Quit before calling stop
(`app.rs:868–875`).

Retaining that actual Child through a mixer worker join postpones a local stdin
close; it does not change these downstream policies. `OwnedStartup` already
provides the generic resource retention: its `SupervisorRef` deliberately never
calls the supplied value (`owned_startup.rs:112–134`), and retain stores it before
startup (`384–405`). Another generic ownership wrapper would not repair the
concrete EOF/error-to-kill transition.

## Smallest useful next implementation

Add an inactive, separately versioned successor for the supervisor's actual
observation/event branch. Preserve the frozen supervisor and keep main unbound.
Use an operation taking a borrowed `&mut Child`, a typed controller event and a
caller-retained journal, so returning an error cannot itself consume the child.
One invocation performs at most one actual child-status observation; it does not
spawn, terminate, restart, close supervisor stdin or dispose a guest graph.

Record `Running`, `Exited(ExitStatus)` and `PollError(kind, raw_os_error)` separately.
Preserve controller EOF, returned reader error and reporter disconnection as
separate observations. Each loss event closes permission for normal admission
and records that the child must remain held; it supplies no stop authority. An
actual `try_wait` error stays unknown and cannot become an exited/reaped receipt.
Capture the attempted call and its returned value before interpreting it.

The reader side must preserve its actual EOF/error distinction instead of the
current discarded `io::copy` result. The first slice should have no terminate,
restart or inferred-cleanup API. It should reuse existing caller custody rather
than invent another owner or fabricate a native-stop token. It also cannot claim
to close real route admission until a later explicit connection to the retained
lifecycle is made.

Focused new checks can use an actual private child to demonstrate that controller
EOF and reporter disconnection leave it alive and held, then record its natural
exit after a fixture-only command. A scripted polling error can test preservation
of that error branch without being labeled an actual OS polling failure. Cleanup
of such a fixture is independent and must precede the final oracle. No unchanged
stream/process suite needs replay for this observation-only successor.

## Guest service work still required

The stream APIs already define terminal retention, explicit next and graph-first
externally justified disposal (`stream.h:94–138`). A production service should use
those operations rather than reimplement their ownership state. The process
fixture's D command is not a production teardown protocol: it relies on known
serial modeled effects (`peer.c:230–244`). Turning it into a native command needs
independent quiescence, alias and uncertainty accounting that do not yet exist.
Construction failures and loss of the management channel must also retain the
actual service resources; the fixture's failure exit is not such a guarantee.

A blocking data read/write can prevent a single-threaded service from handling a
management request indefinitely. Simply adding a timeout or nonblocking mode is
not transparent: the frozen stream treats EINTR and EAGAIN as terminal errors
(`stream.h:101–110`). Signal policy also remains explicit: real POSIX write can
raise SIGPIPE before any errno is retained; only the fixture selected SIGPIPE
ignore. Serial inspection, process death, final owner Drop, service-manager
termination and OS descriptor closure remain distinct lifetime boundaries.

A later binding still needs a separately supplied authoritative acquisition plan,
an exclusive and authenticated peer relationship, a real management/lifetime
contract and an independently justified stop decision. A fresh handshake,
matching full state, local write completion, worker join or child exit cannot
supply those facts. Linux ARM64 compile/link closure does not exercise them.
This review implements no production service or main wiring and performs no
guest restart, installation, physical-device operation or release. All existing
runtime and product gates remain CLOSED.
