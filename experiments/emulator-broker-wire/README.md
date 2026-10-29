# Inactive broker wire seam

This experiment adds a bounded C/Rust value protocol between an already-created
borrowed broker and a host backend that owns one caller-supplied byte stream.
It is a separate successor to retained worker startup, with the existing broker
source frozen. It creates no endpoint and is not wired into preview main.

Read [SPEC.md](SPEC.md) for canonical bytes and custody obligations, and
[REVIEW.md](REVIEW.md) for why the old raw control stream cannot provide matching
command receipts. [Focused verification](HOST-TESTS.md) passes 5,248 C checks per plain/ASan+UBSan/
TSan mode with five strict controls, plus 20 actual-crate Rust tests and seven
strict controls. One fresh independent reproduction and retained-file audit
agree within their stated scope. All existing runtime and release gates remain CLOSED.

The C dispatcher invokes actual mb_adopt/mb_submit and retains the full returned
result. Exact last-exchange retrieval is marked cached. The host accepts a fresh
full matching positive receipt only. An uncertain attempt never becomes a
definite rejection merely because a later call sends nothing.

Full source exports, build products and fixture logs live under ignored private/.
Only authored source and sanitized evidence belong on the development branch.
No parent suite should be repeated unchanged as evidence for this seam.

The 252 inherited tests stay filtered. No source-to-native authority, endpoint,
main wiring, guest acquisition or recovery operation is supplied. The evidence
handle retains copied diagnostics; only the backend retains its supplied stream.
