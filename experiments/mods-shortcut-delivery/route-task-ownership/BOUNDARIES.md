# Native encoder task capture and execution

This is an offline review of the exact EP147 ELF, SHA-256
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
It follows the four scalar records in the [burst observation](../route-observer-burst/runtime-evidence.md)
and the earlier [input route](../input-route/). It adds no hook, runtime
observation or community feature. Existing evidence is unchanged.

The task stores copies of the scalar payload. That does **not** establish
retained ownership of the dispatcher whose address it copies. Task-box allocation,
submission, cancellation and release are reviewed separately in
[TASKBOX.md](TASKBOX.md); that review and this one concern different lifetimes.

## Construction and submission

The observed entry `0x1b01cf8` has FDE extent `0x2e8`. It obtains a task box
through `0x1b00e10`, saves its virtual `+0x30` method, and requests 56 bytes with
alignment 8 from the allocator at task-box `+8` through its virtual `+0x10`.
On the reviewed allocation-success path, explicit stores construct the task
fields directly in that allocation. No separate task/lambda copy call is
identified in that local sequence; this does not exclude behavior inside
indirect or transitive helpers.

| Task offset | Verified store / provenance |
|---|---|
| `+0` | Vptr `0x2d444d8` at `0x1b01db8` |
| `+8` | 32-bit result of `0x7305f8`; its meaning is not needed here |
| `+0x10` | Incremented 64-bit task-box `+0x20` counter, stored at `0x1b01dc0`; the service review identifies it as TaskNo |
| `+0x18` | Raw `entry x0 - 0x118`, paired with the counter store |
| `+0x20` | Full entry `x2` at `0x1b01dc4` |
| `+0x28`, `+0x2c` | Entry `w3`, `w4` respectively at `0x1b01dc8` |

These are the fields read or written by the reviewed path, not a declaration
that every byte in the allocation has been initialized. The live observations
keep `x0/x1/x2` opaque; static RTTI and arithmetic do not identify a live object.
The task RTTI names the `didRotateEncoder` lambda with an encoder reference,
ID and two integer method arguments. The encoder reference itself is not
stored in this task payload.

At `0x1b01df8..0x1b01e00`, the virtual submission receives `x0=task box` and
`x1=task`. Its full `x0` result is tested for zero at `0x1b01e04`; the nonzero
branch may call task-box `+0x18`'s virtual `+0x10` callback. No child execution
result is examined here. Allocation failure takes a diagnostic path which
rejoins the submission call with the null task; a successful allocation or
entry observation alone therefore does not establish accepted submission.
Do not interpret an entry return as task completion. TaskNo wrap/exhaustion and
queue timing require their own treatment; observer slot ordinals are unrelated.

## Task-local destruction and raw captured lifetime

Vptr `0x2d444d8` has these four verified method words:

| Slot | Function | Verified local effect |
|---|---|---|
| `+0` | `0x1aff780`, extent `0xcc` | Decrements specialization-global counter `0x3fea588`; clears task `+8` |
| `+8` | `0x1aff270`, extent `0xdc` | Same local bookkeeping, then tail-calls the deallocation target with size `0x38` |
| `+0x10` | `0x1b029c0`, extent `0x104` | Task execution described below |
| `+0x18` | `0x1afeb68`, extent `0x3c` | Produces a diagnostic value through hidden result register `x8`, using a static lambda-name string |

The first two functions do not load the captured dispatcher, ID or integer
fields. Their atomic counter is shared by this task specialization; it is not
a reference count loaded from the captured dispatcher. The fourth slot does
not copy the task: it overwrites incoming `x0` with a stack temporary and uses
a static type-name string. These bounded paths provide no retain/release pair
for the dispatcher. Outside lifetime rules may still exist; absence in these
functions is not a whole-program proof that native code is unsafe.

## Execution and guards

`0x1b029c0` consumes `x0` as the task address. Its first instruction loads
`task+0x18`, **before any address check**. It then checks the address
`dispatcher+0x19c` against a constant lower bound `0x28fa0b4` and for four-byte
alignment, reads its 32-bit value, and compares it with `0x1b02858`'s return.
These checks do not establish a mapped allocation, allocation extent, retained
storage or exclusion of concurrent destruction.

The helper lazily initializes cached value `0x3fea5f8`, guarded by
`0x3fea5f0`, using the atomic integer allocator `0x6f2398`. Constructor
`0x1afe214/218` calls that same helper and writes the result to dispatcher
`+0x19c`; the constructor failure path and destructor `0x1b039b4` zero the
field. This is evidence of a shared initialized sentinel. It is **not a
per-task epoch, event-cancellation token or synchronization fence**. A zeroing
store does not keep the containing memory alive or prove when queued tasks
stop reading it.

If the checks pass, the executor tries handlers in this order:

1. Child at dispatcher `+0x1a0`.
2. Child at dispatcher `+0x150`.
3. The dispatcher itself.

Each target uses virtual slot `+0x198`, with `x0=target`, `x1=copied full ID`,
`w2=copied delta`, `w3=copied cumulative value`. Null children and the default
handler `0x1add3a0` are skipped; that default returns zero. Each called child
has only its return's **low byte** tested. Nonzero stops fallback; zero causes
fresh loads of the copied integers and dispatcher pointer before the next
candidate. This does not prove ownership or stability of those children.

The final dispatcher call is a tail `br x5` at `0x1b02a88`, after restoring the
incoming LR and frame. Early address-check returns, sentinel mismatch,
default-handler skips, child results and the final handler do not supply a
single established accepted/completed return contract. A passive wrapper must
preserve the native return state and exception/unwind behavior without
interpreting it as a boolean task result.

## Conditional observation boundaries

The verifier extracts actual `azm_pcrel_kind`, `azm_ends_fn` and `azm_patch_ok`
from pinned upstream `3e0c5cb010454228f4ee63226b60d060494eeaae`, plus the SDK's
actual 16-byte hook-overlap predicate. It does not substitute a looser policy.

| Entry | FDE extent | First four classes | Static instruction-policy result |
|---|---|---|---|
| `0x1b029c0` task executor | `0x104` | LDR, ADRP, ADD, ADD (`0,2,0,0`) | Passes one- and four-word checks |
| `0x237a440` TimerTaskBox submission | `0x38` | STP, MOV, STR, MOV | Passes both checks |
| `0x237eac0` submission service | `0x1e0` | STP, MOV, STP, MOV | Passes both checks |

ADRP is class 2, permitted by the unchanged installer; ADR is class 1 and
refused. The installer scans all four opening instructions even when a late
SDK claim will overwrite one word. Such a claim still requires a reachable
island, ownership and other SDK checks. The executor's one-word island path
copies its LDR only; an early four-word path has ADRP relocation support.
No numeric-literal overlap was found in the pinned core sources for these
three entries. This is not an installed-hook table or a successful claim.

The executor has eight verified unwind rows: its initial CFA is `sp+0`; the
48-byte frame begins after the address guards; early-return and final-tail
paths restore CFA `sp+0`. An entry probe can initially record an entry count,
original LR and thread observation without retaining the task address or
dereferencing native storage. It would establish only entry arrival.
Correlating payloads requires a separately tested wrapper and bounded
copying **during** the native task lifetime; recording a raw address does not
authorize later reads. Submission observations, executor arrival and native
child handling must remain distinct. No new wrapper ABI fixture or runtime
claim was performed in this audit.

The generic submission service can receive a null task on allocation failure.
Any later payload-copy proposal must reject null before reading task fields,
restrict provenance to its reviewed caller/type, and copy only initialized
scalar fields during the owned invocation. Executor observation must not follow
the captured dispatcher or child pointers before native guards. New call/return
instrumentation needs its own return-ABI, CFI and exception review; the earlier
entry/tail-only wrapper evidence does not validate it. See [review](REVIEW.md).

## Reproduction and limits

Run with the existing private firmware and upstream checkout:

```sh
python verify-static.py --elf /private/path/EP147 --upstream /private/path/xdj-az-mods
```

The pinned result is [static-result.json](static-result.json): **133 instruction
anchors, eight data words, one RTTI identity, 22 strict rejection controls,
eight executor CFI rows and 61 actual-policy assertions** pass. An independent
parent execution reproduced byte-identical JSON. The verifier
checks selected instruction/data/RTTI anchors, function extents/body hashes,
executor CFI and task-local destructor reads; strict controls reject wrong
capture order/width, task size, slot, guard, return, entry and unwind claims.
The host fixture tests TBZ in every opening position, ADR versus ADRP,
short-return overwrite and the exact overlap edges. Raw review disassembly
and transient results are confined to ignored `private/`.

Static lifetime and return limits remain open. No existing successful runtime
sequence was repeated, no physical deck was accessed, and no watchdog,
quarantine flag or hook policy was changed.
