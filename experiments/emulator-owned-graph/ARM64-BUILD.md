# ARM64 production compile and link evidence

The selected corrected build compiled eight production translation units once
and linked two shared libraries from those same objects. It used the pinned
AArch64 Alpine/musl toolchain, real Linux headers, and the actual syscall and
pthread wrappers. All eight compile logs and both link logs were empty. An
independent file-byte audit passed on its first invocation. The sanitized
[build result](build-result.json) records source, toolchain and failure provenance.

The anonymous [export map](graph-exports.map) exposes exactly the seven
`mbog_*` graph entry points in the candidate. The baseline exports 53 production
APIs plus the toolchain's `_init` and `_fini` (55 total). The candidate localizes
the other helpers; this is not an alias-exclusion or security boundary. Both libraries have the same 18 strong and five weak external
imports; all strong imports exist in the exact retained musl provider.

| Retained artifact or receipt | SHA-256 |
| --- | --- |
| Candidate shared library | `d5e1ede4047aa09a6c024a296ff208f8a72e507d218ebfd81e316c46cf13b58f` |
| Baseline shared library | `ba41f0c2560560373d7c006fd7b23f772f23356e5ebe97f4b8f09bd31c58eb40` |
| Independent private audit receipt | `f8202a975299266f772c38a600c962bb7bfb96265373ffb20618412b3fd70a2f` |

The first actual target build failed at four adjacent `if` statements in frozen
`wire.c` under `-Werror=misleading-indentation`, before either library was linked.
The [formatting patch](wire-gcc-format.patch) splits only those four lines in the
copied build input; the frozen parent remains unchanged. Seven translation units
and the extracted core methods are unchanged. Strict flags, including
`-Wall -Wextra -Werror`, were preserved. The three objects compiled before that
failure match their corrected-build counterparts byte for byte. Existing host
matrices were not repeated.

A separate private tooling preflight failed because its inventory filter omitted
a nested `build-exit-code` evidence file. It never invoked Docker. The corrected
filter excludes only top-level build UUID directories. Both failures remain
retained separately. The eight-compilation/two-link count describes the selected
corrected build; the audit accounts for two selected target-build attempts and
the separate preflight, without claiming an exhaustive inventory of all private
attempts.

This establishes compile/link closure against the pinned musl provider.
No produced target artifact was loaded or executed; no syscall, synchronization
or disposal operation was exercised through these artifacts. It does not
establish compatibility with the running guest or firmware libc, live driver
behavior, native ownership, quiescence, recovery, audio quality or physical timing.
Raw builds, scripts, logs and binaries remain private.

The next integration is a guest stream owner that retains the supplied transport
and graph before I/O, records partial request reads and response writes, and keeps
graph state and pending response evidence after EOF, write failure or disconnect.
Connection loss must not trigger reopening, replay, graph disposal or an inference
of quiescence. Host/guest transport binding and supervisor custody remain separate.
