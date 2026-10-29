# Inactive fixed-generation settings route

This separate Unix successor constructs one retained preview route whose settings
bridge and producer baseline come from the same complete backend receipt. The
backend is queried once during initialization; the route can obtain that captured
receipt once, and cannot request a newer epoch after settings failure or closure.

The component consumes an explicitly supplied backend and directory. It adds no
backend factory, worker or guest startup, endpoint, main wiring, native adapter,
recovery action or community release. Main remains inactive. Frozen route,
transport, lifecycle, settings sink, mapper and file-pair sources remain unchanged.

Read [the contract](CONTRACT.md) for initialization ordering, retained resources,
poison and panic semantics, the distinction between wrapper observations and
producer acceptance, and the unresolved runtime gates.

## Source and preparation

- [fixed_session.rs](fixed_session.rs) owns the one-attempt constructor, private
  backend/settings proxies and copied diagnostic evidence.
- [prepare-candidate.py](prepare-candidate.py) prepares a separate private source
  candidate from the frozen settings-sink successor.
- [run-tests.py](run-tests.py) compiles the production configuration and executes
  only the 25 reviewed fixed-session cases and 16 designated controls against
  pinned source and test inventories.

The constructor returns an unarmed route. Ordinary exact baseline adoption and
an explicit released-controls assertion still belong to the caller. A captured
receipt establishes historical correspondence, not current state, authentication,
native ownership or exclusion of other users. Creating a separate session with an
aliased backend does not inherit this route's one-generation restriction.

Production host compilation, 25 focused tests and 16 exact-assertion compiling
controls pass in the primary and an independent fresh run. The 211 inherited
tests are compiled/listed and filtered. Read [host evidence](HOST-TESTS.md),
[independent comparison](comparison.json) and [review](REVIEW.md) for limits.
All existing runtime, staging, native-ownership, USB and release gates remain
CLOSED. No emulator or physical-device contact is part of this component.
