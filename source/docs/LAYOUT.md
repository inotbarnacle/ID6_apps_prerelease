# Layout rules

Plugin XML is written in design units on the 1440 × 540 canvas (see the [layout preview](https://github.com/inotbarnacle/idrive6-layout-preview)). Components sit in the 1260 × 477 content area, which starts right of the chrome.

- **Grid:** every x, y, width and height is a multiple of 6.
- **Gutter:** cards and tiles in the same group are exactly 12 apart, horizontally and vertically. A row's cards have equal width, so `n` cards span `n × width + (n − 1) × 12`.
- **Margins:** none on the left (the chrome already spaces it), 12 at the top, bottom and right (rows span 1248, from y = 12 to 465). The 12 at the bottom and the 303-wide card of four across (4 × 303 + 3 × 12 = 1248) are the two places that are not on the 6-unit grid.
- **Padding:** text inside a card starts 24 from the card edge. A label sits 12 below the card row it belongs to.
- **Artwork matches its box:** a picture is cropped, not resized, so the PNG is generated at exactly the size of its box.
- **Labels** are at least 42 high for the 40-unit font, and wide enough for the longest text.

The generators in `tools/` (including BoostDeck, which paints its own tiles) author these numbers directly. Apps without a generator (BEAMer, ShiftDeck and others) keep their XML in `native/manifests/` and are edited there.

The installer scales these layouts down for the 800 × 480 and 960 × 480 panels (`stage-payload.py`), so the rules apply to the 1260 × 477 base only.
