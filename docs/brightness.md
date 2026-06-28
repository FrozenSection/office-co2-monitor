# Display brightness & auto-dimming

The backlight can run at a fixed level or track ambient light (with the optional
VEML7700 lux sensor). The auto path is built so the display dims **smoothly at the
low end** — important for a nightstand — without slamming to full the moment a lamp
comes on.

## How auto-brightness works

Two facts drive the design: ambient light spans *decades* of lux, and the eye
perceives brightness *logarithmically*. So the pipeline has three stages:

1. **Lux → position (logarithmic).** The smoothed lux reading is mapped to a 0–1
   position between `luxLow` and `luxHigh` on a **log** axis. Equal *ratios* of light
   feel like equal steps, so a desk lamp nudges the level up the curve instead of
   pinning it to maximum. Below `luxLow` sits at the floor; above `luxHigh` at the
   ceiling.
2. **Position → perceptual level.** That 0–1 position is scaled between
   `brightnessMin` and `brightnessMax`, which are **perceptual** endpoints on a
   0–255 scale.
3. **Perceptual level → PWM duty (gamma).** The level is mapped to actual backlight
   duty through a **gamma** curve (`duty = (level/255)^γ`), driven as **12-bit PWM**
   (0–4095). Gamma + 12-bit is what gives the low end many fine, even-looking steps
   instead of a few coarse jumps.

Changes are **slewed** in perceptual space, so transitions fade evenly rather than
lurching.

When no lux sensor is present, or auto-brightness is off, the display simply holds the
fixed **`brightness`** level (still gamma-mapped). The same firmware therefore serves an
office unit (no sensor, fixed) and a bedroom unit (sensor, auto) unchanged.

## Settings

All live under **Settings → Display**.

| Setting | Meaning |
|---|---|
| **Auto-brightness** | Track ambient light. Requires a VEML7700; ignored if absent. |
| **Brightness** | Fixed perceptual level (0–255) used when auto is off or no sensor. |
| **Auto min / max** | Perceptual floor / ceiling for the auto curve (0–255). |
| **Lux low / high** | Ambient-light endpoints. Low → min, high → max, mapped on a log axis. `low` must be ≥ 1. |
| **Dimming gamma** | Curve shape, stored ×10 (`22` = 2.2). Higher = dimmer, gentler low end. |

## Effects to know

- **Min/max are perceptual, not raw duty.** Because of the gamma curve, a given
  `brightnessMin` renders **much dimmer in actual light** than a plain linear map would.
  Great for a dark night floor — but if it's *too* dark to read, **raise
  `brightnessMin`** (≈30–45 is a usual readable floor).
- **Never fully dark.** Any non-zero level keeps at least 1/4095 of duty, so the floor
  is dim but not black.
- **Diagnostics `br %`** reports the **perceptual level** (0–100% of the 0–255 scale),
  i.e. what the min/max endpoints set — not the raw PWM duty.
- **Lux changes are smoothed twice** — an exponential average on the lux reading, then a
  perceptual slew on the level — so flicker and sudden jumps are damped.

## Tuning

**Office (always lit, often no sensor).** Leave auto off and set a comfortable fixed
**Brightness**, or enable auto with a high `brightnessMin` so it never gets dim. Gamma
matters little when you live in the top half of the range.

**Bedroom nightstand (where this shines).**
1. Set **Lux low ≈ 2–3**, **Lux high ≈ 400** so the curve stretches from near-dark to a
   bright room.
2. Pick **Auto min** for how dim you want it at night (start ~20–35; lower is dimmer),
   and **Auto max** for daytime (often 255).
3. Tune **Gamma** to taste: **2.2** is a good start; push to **2.6–2.8** for an even
   gentler low end, drop toward **1.8** if the whole range feels too dark.
4. Test in an actually-dark room — the low end is exactly where the curve earns its
   keep, and it's invisible in daylight.

Re-check after the unit is in its enclosure: the printed window over the lux sensor
attenuates light, so the lux endpoints are calibrated to the *as-built* window, not bare
sensor readings. See [placement.md](placement.md) for the window itself.
