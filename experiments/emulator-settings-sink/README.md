# Inactive retained settings callback

This separate successor implements the actual preview `SettingsSink` callback
using the frozen strict ACK mapper and retained Unix file-pair engine. It keeps
the complete ACK, source mixer image, mapped target bytes and partial outcomes
together, and refuses later callbacks after failure or an interrupted attempt.

[The contract](CONTRACT.md) states ordering, panic and retained-evidence behavior.
[Production source](settings_sink.rs) and [focused fixtures](settings_sink_tests.rs)
compose actual preview types. The [parent writer audit](../emulator-settings-pair/WRITER-BOUNDARIES.md)
still governs competing writers and pair readers.

Preparation exports a fresh private candidate from the pinned mapper chain.
Only an inactive Unix module declaration changes among parent files. Main,
dependencies, native adapter and all frozen parents remain unchanged.
Full exported sources, fixture directories, runtime output and binaries remain
private. No application, emulator or physical endpoint is run.

The bridge's closure does not permanently fence native admission: the frozen
route can adopt a new epoch after a settings failure, then commit its FRAME
before the closed bridge refuses the callback. A focused counterexample retains
this limit. An external owner must establish baseline correspondence and prevent
failed-route rearming before live integration.

[Host evidence](HOST-TESTS.md) and [review](REVIEW.md) describe the bounded checks.
This is source development, not an installed candidate or community release.
No runtime, recovery, staging or product gate is reopened.
