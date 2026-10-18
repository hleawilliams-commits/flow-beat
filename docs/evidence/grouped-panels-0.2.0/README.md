# Grouped Deck Panels 0.2.0: 2 DECK and 2/4 DECK (emulator, 2026-10-17)

0.1.0 only drew in 4 DECK, because the SDK's `deck_count()` returns 2 for both 2 DECK and 2/4 DECK, and 4-deck headers in 2 DECK would cover REMAIN/TEMPO.

- **2/4 DECK:** the lower panels are identical to 4 DECK, so all four headers show (`24deck-setting.png`).
- **2 DECK:** one header per side over the stock title row (x 10/650, y 561, 620×46), following the deck each side has selected (left 1/3, right 2/4). No LEFT/RIGHT label or highlight, since both visible decks are always the selected ones (`2deck-setting.png`).
- **4 DECK:** unchanged (`4deck-auto.png`).
- **Telling 2 DECK from 2/4:** a pass-through touch filter remembers the last native layout tab tapped (saved in the mod's settings). Mod Menu **Two-deck view: Auto / 2 DECK / 2/4 DECK** overrides it. In a two-deck view with nothing known, headers stay hidden.

Emulator limit: the emulator's virtio tablet isn't a multi-touch panel, and the core only passes multi-touch events to `touch_filter`, so Auto tab-watching can't be exercised here. The screenshots use the Mod Menu setting. Auto needs a physical-deck check.

Host: layout suite incl. tab hit-testing and 2/2-4 slot mapping; 4110 data checks; 432 paint scenarios (incl. 2 DECK slots) with no overlap or out-of-bounds drawing; SDK harness plain/ASan/TSan; ARM64 build + SDK validation.
