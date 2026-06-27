# CO₂ Monitor — display handoff / redline

Round 240 × 240 px GC9A01 IPS, RGB565, black background. All content inside the
Ø210 safe circle (≈15 px margin). Flat fills only; design as discrete per-zone
redraws. Coordinates below are panel pixels; **center = (120, 120)**.

Two designs are provided. **Design 1 (Aperture)** is locked as the primary.
**Design 2 (Analog)** is an alternate. Modal + boot screens are shared.

- Interactive mockup: `CO2 Monitor Screens.dc.html` (open in a browser; pan/zoom)
- Flat 240×240 reference PNGs: `mockups/`

---

## 1. Palette (hex → convert to RGB565)

**Air-quality tier (CO₂)** — always paired with the text label for color-blind safety.
| Tier | Range (ppm) | Hex |
|------|-------------|-----|
| GOOD | ≤ 800 | `#16C46A` |
| FAIR | 801–1000 | `#F5C518` |
| POOR | 1001–1400 | `#FF7A1A` |
| BAD | > 1400 | `#FF3B30` |

**Calibration confidence** — cool hue family ONLY, never amber/red, so it can never be
mistaken for an air-quality warning. (Days-since-recal shown only on Diagnostics.)
| State | Age | Hex |
|-------|-----|-----|
| FRESH | ≤ 7 d | `#2BD4C4` |
| AGING | 8–30 d | `#3E8BF0` |
| STALE | 31–90 d | `#7A6CF0` |
| OVERDUE | > 90 d | `#B05CF0` |

**Text / UI**
| Role | Hex |
|------|-----|
| Primary text | `#FFFFFF` |
| Secondary | `#B8BDC4` |
| Muted / footer | `#6B7077` |
| Faint label | `#8A8F96` |
| Background | `#000000` |
| Diag ring (neutral) | `#242424` |
| Dim tier ring (bedside) | `#5A4E1E` |

**Type** — Inter (humanist sans; convert TTF → GFX bitmap via `fontconvert` at the sizes
below). Numbers use tabular figures. Sizes: primary number 40–56, clock 56, tier label
14–18, secondary 13–15, micro 11–13 (≈16 px is the legible floor on-device).

**Icons** — 1-bit monochrome, firmware applies color: battery (rounded rect + nub + fill
bar), wifi (3 ascending bars), calibration (ring + center dot / target), trend arrow
(↗ rising, → flat, ↘ falling).

---

## 2. Views — zones, sizes, positions

### Design 1 — Aperture (PRIMARY)

**CO₂-prominent** (`mockups/01`) — vertically-centered stack; **tier ring = 8 px stroke at
the panel edge, colored by AQ tier**.
- `13:15` — 16 px, secondary
- `835` — 56 px, **white** (number stays white; ring + label carry tier color)
- `CO₂ ppm` — 13 px, faint
- `FAIR ↗` — 17 px tier + 14 px trend
- `74°F · 55%` — 15 px, secondary
- calibration dot — 14 px ring, cal-state color (hidden or FRESH when healthy)

**Time-prominent / bedside** (`mockups/02`) — clock dead-center; tier ring dimmed to 4 px
`#5A4E1E`.
- center: `13:15` — 56 px white, at (120,120)
- top zone @ y≈46: tier dot (10 px) + `835` (19 px) over `CO₂ ppm` (12 px faint)
- bottom zone @ y≈194: `74°F · 55%` — 15 px secondary

**Diagnostics** (`mockups/03`) — neutral 3 px ring `#242424`; 6 centered rows:
`CO₂ 835 FAIR` (15, tier word colored) · `74°F · 55%` · `lux 160 · 62%` ·
`wifi off · cal 3d` (cal days in `#2BD4C4`) · `batt 78%` · `v0.8.1 · up 2h` (12, muted).

### Design 2 — Analog (ALTERNATE)

**CO₂-prominent — needle gauge** (`mockups/04`)
- Tier arc = top semicircle, ring band radius 92–112 (inset 8, mask transparent→solid at
  92/94). Conic from 270° sweeping 180° clockwise; stops map 400→2000 ppm:
  green 0–45° (≤800) · yellow 45–67.5° (≤1000) · orange 67.5–112.5° (≤1400) · red 112.5–180°.
- Needle from center, white, length 86. **angle = 270° + (ppm−400)/1600 × 180°**
  (clamp 400–2000). 835 ppm → 319°. Hub = 13 px white at center.
- Below center: `835` 40 px white · `ppm FAIR ↗` (13 + 14 tier) · cal dot 9 px +
  `74°F · 55% · 13:15` 13 px. Scale ends `400` / `2k` at y≈106, 10 px muted.

**Time-prominent — analog clock** (`mockups/05`)
- 12 ticks at radius from edge: minor 2×9 px `#555`; major (12/3/6/9) 3×12 px `#8A8F96`.
- Hour hand 6×46 px white, **angle = (h%12 + m/60) × 30°**. Minute hand 4×70 px white,
  **angle = m × 6°**. Hub 12 px white. (13:15 → hour 37.5°, minute 90°.)
- top @ y≈42: `74°F · 55%` 13 px faint. bottom @ y≈196: tier dot 9 px + `835` 14 px +
  `FAIR` 13 px.

**Diagnostics** — identical to Design 1 Diagnostics.

### Shared — modal & boot (`mockups/06–08`)

- **Recalibration** — cool-blue progress ring (cal family, NOT a tier color): conic arc
  `#3E8BF0` over `#1C1C1C` track, ring radius ~102–112. Center: `RECALIBRATING` 12 px blue ·
  live `835 ppm` 46 px white · countdown `02:30` 20 px. Footer `hold to cancel` 12 px muted.
- **Wi-Fi setup** — wifi bars + `WI-FI SETUP` top; join QR centered, **≤ 128 px** (max
  full-bleed square is 169 px). Footer `scan, then tap to exit`. *QR in mockup is a
  placeholder pattern — generate the real join QR on-device.*
- **Splash / boot** — ring+dot logo mark (green `#16C46A`) · `CO₂ Monitor` 21 px ·
  `v0.8.1` 13 px muted. (Brand mark is a placeholder — swap for final logo.)

---

## 3. Navigation (one physical button)
- **Tap** → next passive view (CO₂ → Time → Diagnostics → …)
- **Double-tap** → start Recalibration
- **Hold 3 s** → Wi-Fi setup
- Modal screens use tap/hold to confirm/cancel/exit, labeled on-screen.

## 4. Render notes
- Per-zone partial updates — each datum is its own redraw region; no full-canvas redraw.
- Flat fills only (no gradients/shadows/glows). The mockup uses CSS conic gradients for the
  gauge/progress arcs purely for preview — **draw these on-device as discrete colored arc
  segments**, not a gradient.
- Backlight auto-dims to room light; verify contrast across the brightness range. Bedside
  view intentionally dims its ring.
- Units (°F/°C) configurable.
