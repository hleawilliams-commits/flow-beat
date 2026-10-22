# Independent review and repeat

Root independently ran the frozen host matrix on October 22, 2026. All 220
candidate assertions across nine scenarios passed in plain, ASan/UBSan and
TSan. Each mode reproduced the four strict controls' exact 1/1/2/2 named
failures, with no sanitizer diagnostics. The result is byte-identical to
`test-result.json`, SHA-256
`87d818c28c8c845425be6e91d74da0e1fb9b24e8f193b66ab5287205be2382d9`.
The independent immutable private snapshot is
`47df5364-edab-4b3b-9c1a-b0469992c604`; the author's snapshot is
`eb2059e4-c4fa-4fd0-9e25-2b58dd582b2b`.

Source review checked separate entry/return publication, unique bounded
reservations, immutable slots, fieldwise context validation, rejection before
native reads, and return recording after close without a native pointer.
`close()` permits a begin already past the Open check to publish later. It is
not an invocation drain; storage must remain alive and may not be reset.
Diagnostic counters are independent relaxed observations, and false reader
results leave their output buffers untouched.

The concurrent publication test uses real threads and reads each acquired
record before producer completion/join. Its reservation schedule is deliberate;
it is not a native scheduler or latency model. Negative publication controls
run only the serial polling fixture. Read-range checks observe the explicit
copy requests rather than proving the default memcpy implementation's loads.

The capture source remains unchanged. This review and sanitizer coverage do
not establish native task storage lifetime, caller provenance, hook ownership,
quiescence, physical timing or readiness to install. The separately reviewed
ARM64 integration connects the actual sources only to synthetic callees.
