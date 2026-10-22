# Hook installation and lifetime for a passive task observer

The pinned core supports a process-lifetime passive observer, provided each hook
forwards through the exact original-pointer slot supplied to `claim_hook` and
all capture resources remain valid until the process ends. A failed second
claim cannot undo the first. Turning capture off is neither unhooking nor a
barrier that waits for an already admitted invocation.

This offline review concerns submission entry `0x237a440` and executor entry
`0x1b029c0`. It adds no mod, hook, core patch, navigation action or runtime
installation. It does not establish native task lifetime or accepted input.
The prior [task borrowing contract](../task-capture-contract/LIFETIME.md) and
[call/return wrapper](../task-call-return-abi/) remain separate evidence.

## Exact source and scope

All core source comes from `git show` at upstream
`3e0c5cb010454228f4ee63226b60d060494eeaae`, never the dirty working tree.
The [validation record](validation.json) fingerprints `inline_hooks.c`,
`community.c`, `azmod-mod.h`, `sdk_vslot.c`, the extracted functions, this
fixture and its runner. Firmware metadata is read from the existing local ELF
with SHA-256
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
No firmware instructions are executed.

Source citations below are upstream paths and one-based lines in that pinned
commit. They are not citations to the mutable local checkout.

## Original publication and a claim that fails midway through initialization

`src/azmod/community.c:1226` (`cm_claim_hook`) checks the caller's module,
init/loading ownership, declared hook permission, exact firmware policy,
address range and overlap under `g_cm_mu`. It invokes the installer under that
mutex. The SDK return is **0** on success; the internal installer's success is
**1**. The initialization rule is explicit in `src/azmod/azmod-mod.h:453`:
claim hooks last, after fallible setup; an already claimed hook remains live
until the next start even if `init()` returns failure.

For the late island path, `src/azmod/inline_hooks.c:274` does the following:

1. Check the opening instructions and obtain an eligible nearby island. With
   no island in the late phase, refuse before allocating a trampoline.
2. Create a private writable trampoline, copy one covered instruction, append
   `LDR X17` / `BR X17` to the original function at `+4`, flush its instruction
   cache and make the trampoline executable.
3. Populate and flush the island, then obtain writable access to the function
   entry. None of those earlier failures publishes the original or redirects
   the function; private trampoline storage is discarded as applicable.
4. Store the trampoline directly into the supplied `orig` slot using
   `__ATOMIC_RELAXED`, then store the aligned entry branch using
   `__ATOMIC_RELEASE` (`inline_hooks.c:334`). Flush that entry and record success.
   A protection-restore warning after redirection is still a successful hook;
   it does not remove the now reachable trampoline.

The function can enter the observer after the redirect and **before the claim
call returns**. An integration must pass an aligned, permanent original slot
used directly by its assembly, not a temporary followed by a post-claim copy.
Keep one original slot per hook, initialize other required state before the
first claim, and leave the slots untouched after success. Native forwarding
must not depend on capture being live or a worker being available.

The existing submission wrapper uses `LDAR` for its original-pointer load.
The fixture below proves the installer's source order and modeled resource
behavior; it does not turn the relaxed original store plus instruction fetch
of the release-written branch into a C/C++ acquire/release proof. The core's
cross-core entry-patch and instruction-cache behavior remains the existing
native installer contract, untested here. Use a separate release/acquire
capture-ready gate to publish observer configuration; do not treat loading the
original pointer as publication of all observer state.

With two claims, keep that capture gate closed until both succeed. If the
second is refused, retain the first original, its forwarding code, queues and
state; leave capture inert and return the failure. `community.c:1852` removes
normal module callbacks after failed init and labels already patched code as
requiring Reload to clear. It does not restore patched instructions. The host
fixture exercises a successful first install followed by a refused second
address; it does not execute the full community loader.

## Trampoline and CFI boundary

The selected late path copies **one** instruction. The following continuation
facts come from the exact original ELF FDEs, not a generated trampoline FDE.

| Entry | Covered instruction | Original continuation | FDE state at continuation |
| --- | --- | --- | --- |
| Submission `0x237a440` | `stp x29,x30,[sp,#-0x30]!` | `0x237a444` | CFA = SP + 48; saved x29 at CFA − 48, x30 at CFA − 40 |
| Executor `0x1b029c0` | `ldr x3,[x0,#0x18]` | `0x1b029c4` | CFA = SP; no saved-register rules yet |

Neither copied instruction is a call. The trampoline's jump back is `BR`,
which does not create another LR continuation. For the submission call/return
wrapper, native `STP` therefore saves the wrapper's return LR. Once execution
is back inside the original function, its original FDE describes the native
prologue state; an ordinary exception originating later can unwind that native
frame and then use the wrapper's own CFI. The lack of a trampoline FDE alone
is **not evidence that such a native-body exception is broken**. A synthetic
wrapper exception test remains distinct from testing the installed path.

The constructor-only fallback copies four instructions and resumes at `+16`.
The same metadata review finds submission CFA = SP + 48 with x19 additionally
saved at CFA − 32; executor CFA is still SP. The executor's covered ADRP may
be relocated or expanded by the installer. This fallback is not eligible for
a late community claim with no island, and the new fixture does not execute
that fallback or repeat the previously passed relocation-policy suite.

The installer creates anonymous executable code but supplies no unwind table
or dynamic unwind registration for that code. An asynchronous stack walk,
signal-origin unwind or fault whose interrupted PC is inside the island or
trampoline is a different case: the native ELF FDE does not cover that PC.
The submission trampoline may already have changed SP at that point. This
review leaves those cases unproved. It also provides no guarantee for throwing
through a helper declared `noexcept`, mandatory after-callback cleanup, or
exceptions that bypass a normal-return record. Before helpers must not retain
resources whose release requires the after helper.

Both island and trampoline use X17 as scratch. Wrapper-only tests do not
preserve an incoming X17 value across the actual core island. The chosen native
ABI must continue to treat X16/X17 as scratch, with no stack-argument forwarding
claim. Full installed-path execution and unwind behavior remain integration
gates; static policy eligibility is not successful live admission.

A vtable-only alternative does not meet this pinned SDK's policy: matching
slots `0x321b138` (submission) and `0x2d444e8` (executor) both lie in an ELF
`PT_LOAD` with flags 5 (readable and executable). `src/azmod/sdk_vslot.c:27`
accepts only non-executable loaded segments. Do not retry those slots or weaken
that rule.

## Removal, disable and process lifetime

`src/azmod/community.c:1803` deliberately never closes a successfully opened
library, because its code may still be referenced by native pointers or
threads. That includes validation and init failures after `dlopen`. The SDK
states the same lifetime explicitly at `azmod-mod.h:453`; its descriptor has
no fini callback or unclaim service. Successful trampoline/island allocations
have no removal path in the audited installer. `azcm_set_all_off`
(`community.c:2271`) writes a next-start setting, not an unpatch operation.
The normal Reload is a process restart, not a module unload transaction.

These facts remove the need for an in-process module-unmapping scheme in this
candidate. They do **not** stop threads, drain capture, wait for invocations,
retain native tasks, or make a captured native pointer safe. Even after a
capture gate becomes false, a producer that previously observed true can
finish. Keep observer storage mapped and valid, never reset/reuse an immutable
slot still reachable by a producer, and treat dropped/missing records as
unknown. A finite capture budget can close recording while native forwarding
continues. A permanently retained worker may return or go idle; neither
requires changing the original target.

## Executed discriminating checks

The runner extracts the complete actual `azm_install_hook_at` and nine relevant
helper functions from the pinned source. It instruments the original/entry
stores and mocks mapping, page protection, island allocation, cache flushing,
recording and logging. It executes the source on owned host buffers; emitted
AArch64 bytes are inspected but never executed.

**56 assertions in ten serial scenarios pass in plain and ASan/UBSan modes**:
both selected one-instruction trampolines, original-before-entry publication,
sealed-before-entry publication, exact absolute island target and `+4`
continuation, uncovered-byte preservation, mapping/sealing/unlock failures,
late no-island refusal, an earlier successful hook retained after a subsequent
refusal, and success retained through protection-restore warnings.

Two broken-source controls run in each mode. Moving the original store after
the redirect produces exactly four named failures. Changing the continuation
from `+4` to `+8` produces exactly three. The runner rejects unexpected failures,
exit codes and sanitizer diagnostic text. TSan was not run: this fixture is
serial and would not establish the missing native concurrency/cache contract.
No actual `mmap`, executable-page transition, native instruction-cache flush,
loader unload or firmware function execution is exercised by these host tests.

Run from the repository root with a Python environment containing pyelftools:

```sh
python experiments/mods-shortcut-delivery/task-hook-lifetime/run-tests.py \
  --upstream /path/to/pinned-upstream-checkout \
  --elf /path/to/existing/EP147 \
  --result /path/to/validation.json
```

Source snapshots, extracted input and complete build/test output are immutable
per run under ignored `out-history/`. An initial preflight found that the
fixture's executor ADRP word had been transcribed incorrectly; the exact ELF
comparison rejected it before compiling or executing any host test. The old
fixture/runner and a clearly labeled observed-failure receipt are retained;
the fixture now uses the exact word `0x90006fc1`. Earlier exploratory read-only
commands also encountered a wrong-repository `git show` path and a private
helper returning an integer FDE extent rather than an FDE object. Those reads
were corrected; neither was a native execution or a passing test.

The viable next step is combined-source validation with permanent per-hook
original slots, closed-until-ready capture, bounded copied-value publication,
proper submission and executor ABIs, and normal-return-only relations. No
extra native lifetime or completion claim follows from this audit.
