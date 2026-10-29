# Inactive ACK settings mapper

This separate successor derives exact host setting bytes from a complete
`FrameAck`. It always maps the browse counter and 51 analog bytes, explicitly
selects selector/TIME refresh, and preserves supported unselected fields.
Ambiguous, malformed or unsupported saved maps fail before any file operation.

[The contract](CONTRACT.md) defines the strict parser, selected fields and
receipt limits. [Production source](settings_mapper.rs) and
[focused fixtures](settings_mapper_tests.rs) use the unchanged actual preview
types. The [parent writer audit](../emulator-settings-pair/WRITER-BOUNDARIES.md)
explains the byte offsets and competing legacy writers.

The preparation script verifies the frozen settings-pair chain, exports a new
private candidate and adds only an inactive Unix module declaration plus two
source files. Dependencies and all other parent source bytes are unchanged.
It preserves parent captured inputs separately from successor inputs.
Full candidate sources, compiler output and binaries remain ignored.

This is a pure mapping component. It is not yet a `SettingsSink`, a live
filesystem integration or a restoration coordinator. Exact ACK authenticity,
ordered callback delivery, external writer exclusion and recovery remain
separate requirements. No application main, window, emulator or device is run.

Production host compilation, 27 focused tests and 13 strict controls pass, with
a fresh independent reproduction. The 162 inherited tests remain filtered.
[Host evidence](HOST-TESTS.md), [source review](REVIEW.md),
[comparison](comparison.json) and [retained initial failure](initial-failure.json)
state the exact scope and limits. No existing runtime gate is reopened.
