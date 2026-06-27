# Display design

Visual design from the contractor. **Design 1 (Aperture) is the chosen primary
look.** The Design 2 analog gauge/clock is an alternate we are *not* building for
now — the needle/hands render poorly without anti-aliasing on the GC9A01, flicker
under per-zone redraw, and duplicate the value the screen already prints.

- `HANDOFF.md` — redline spec: palette (hex), per-view zones/sizes/positions,
  navigation, render notes.
- `mockups/` — flat 240×240 reference renders:
  - `01-d1-co2-prominent`, `02-d1-time-prominent`, `03-diagnostics` — chosen views
  - `04-d2-co2-gauge`, `05-d2-time-clock` — alternate (not building)
  - `06-modal-recalibration`, `07-modal-wifi`, `08-boot-splash` — shared flows

Build target: Design 1 main + Time + Diagnostics views; shared restyled modals
(note the **cool-blue recalibration progress ring**, in the calibration hue family
so it never reads as an air-quality alert) + boot splash.

## Open decisions before implementing
- **AQ thresholds:** adopt the designer's (FAIR ≤1000, POOR ≤1400) as NVS defaults.
- **Calibration cadence:** the designer's 7 / 30 / 90 d is lenient for an ASC-off
  sensor — likely keep a shorter "overdue" cadence (TBD). All are NVS settings.
- **Fonts:** Inter via `fontconvert`; subset the large sizes (56/40 px) to digits to
  fit flash (already ~81 %). The `min_spiffs` partition (needed for OTA/logging) adds
  headroom.
- **Trend arrow** needs a small rolling CO₂ history — comes with the logging work.

Navigation (one button): **tap** = next view · **double-tap** = recalibrate ·
**hold 3 s** = WiFi setup.
