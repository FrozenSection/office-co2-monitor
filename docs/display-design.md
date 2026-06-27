# Display design parameters — office-co2-monitor

A brief for the visual designer. The goal is a glanceable desk/bedside CO₂
monitor on a small round screen, with a couple of alternate views.

## Canvas / hardware
- Panel: Adafruit 1.28" round IPS TFT, **GC9A01** driver, **240 × 240 px**,
  ~32 mm active diameter. It is **circular** — the corners of the 240² square
  are not visible.
- Color: **16-bit RGB565** (~65k colors). No hardware anti-aliasing. Current
  text uses the Adafruit GFX 5×7 bitmap font (blocky). Custom fonts and icons
  are possible but must be supplied as assets (see handoff).
- Background is **black** (contrast + power). Backlight is PWM-dimmed and
  **auto-adjusts to room light**, so designs must read across a brightness range.
- Orientation is set in firmware; "up" is logical. Final mount TBD by enclosure.
- Viewing distance ~0.3–1 m. **Glanceable** is the priority.

## Safe area (circular!)
- Keep all content within a **~210 px-diameter circle** (≈15 px margin from bezel).
- Largest centered square that fits the circle ≈ **169 px** (limit for any
  full-bleed graphic such as a QR code).
- A ~6 px **status ring** sits at the extreme edge (radius 112–119 px) — reserve it.

## Rendering constraints (please design around these)
- **Per-zone partial updates.** Full-screen redraws flicker, so firmware redraws
  only the region whose value changed. Design as **discrete zones** (each datum in
  its own area), not one full-canvas illustration that must redraw as a unit.
- **Flat fills only** — no gradients, shadows, or glows (panel + library handle
  them poorly and they're slow to redraw).
- **Legible sizes:** ~16 px tall is the practical floor for secondary text; the
  primary number is ~40 px. Specify a size per element.
- **Icons**: simple **monochrome (1-bit) glyphs** (battery, wifi, calibration,
  trend arrow); firmware applies color.

## Data to display
CO₂ (ppm) + air-quality tier; temperature; humidity; time (HH:MM); calibration
state (days since last recalibration); battery % (when fuel gauge fitted); ambient
brightness (auto). Optional: CO₂ trend arrow (rising/falling).

## Color — the one hard requirement
There are **two independent color scales that must never be confused**:
1. **Air-quality tier** (CO₂ level): green → yellow → orange → red.
2. **Calibration confidence** (how much to trust the reading): fresh → aging →
   stale → overdue.

Today both lean amber and **collide**. Please give calibration a **visually
distinct treatment** — a different hue family, or an icon, or a separate element —
so "is this a CO₂ warning or a calibration warning?" is never ambiguous.

Also: **color-blind safety** (~8% of men) — never rely on red/green alone; pair
color with the text label (GOOD/BAD) and/or shape. High contrast on black;
saturated but not harsh at night.

## Views (please design all of these)
Passive views the user cycles through:
1. **CO₂-prominent (default)** — big CO₂ number + tier ring centered; time and
   temp/RH secondary; calibration cue when relevant.
2. **Time-prominent** — big clock centered; CO₂ reduced to a small colored
   number/dot; temp secondary. (clock / bedside mode)
3. **Diagnostics** — compact status list so the owner can confirm health without
   opening settings: CO₂/temp/RH, lux + brightness %, WiFi state, calibration age,
   firmware version, uptime, battery.

Modal flows (already built — please restyle):
4. **Recalibration** — confirm → equilibration countdown (live ppm) → result.
5. **WiFi setup** — status + **join QR** + "tap to exit".
6. **Splash / boot** — logo + version (brief).

## Navigation (one physical button) — proposed
- **Tap** → next passive view (CO₂ → Time → Diagnostics → …)
- **Double-tap** → start recalibration
- **Hold 3 s** → WiFi setup
Modal screens use tap/hold to confirm/cancel/exit, labeled on-screen.

## Deliverables
- **Palette**: exact hex (we convert to RGB565) for the 4 AQ tiers, the
  calibration states (distinct from tiers), text / secondary / muted, accents.
- **Per-view layouts** at 240×240 inside the safe circle — zone positions, sizes,
  font + size per element — for each view above.
- **Font** choice (we can convert most TTFs to GFX bitmap fonts at chosen sizes),
  or confirm the built-in font is fine.
- **Icons** as simple monochrome shapes (battery, wifi, calibration, trend arrow).
- Restyled **recalibration** and **WiFi** screens.
- Optional: cheap **transitions** (prefer instant or small motion; fades are costly).

## Handoff notes
- Deliver 240×240 PNG mockups + a redline (positions, hex, font sizes). We
  translate to GFX draw calls; fonts via `fontconvert`, bitmaps via `image2cpp`.
- Keep the **circular cut** and **per-zone redraw** model in mind — avoid designs
  that only work as a single full-canvas image.
