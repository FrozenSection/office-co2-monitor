# Calibration — the fresh-air walk

The SCD-41 drifts over time and needs an occasional reference against known-good air.
In a normal building, the sensor's automatic self-calibration (ASC) handles this by
assuming the room periodically reaches the outdoor baseline (~420 ppm) — e.g. overnight
when it's empty. A **sealed office never reaches that baseline**, so ASC would slowly
calibrate the sensor down toward the room's lowest "stuffy" value and start reading low.

So for the sealed profile, ASC is **off** and you calibrate manually.

## How to recalibrate (FRC)

1. Pick genuine background outdoor air (see the don'ts below).
2. Carry the device outside and let it breathe for a few minutes.
3. Double-tap the button → tap to confirm → a 3-minute equilibration countdown runs.
4. At the end the device performs a forced recalibration (FRC) against your reference
   (default 420 ppm) and shows the correction it applied.

Every attempt — success or refusal — is recorded in the event log (`/events`).

## The quality gate

A bad FRC is worse than no FRC: it makes future readings look precise while being
offset. So before committing, the firmware inspects the equilibration samples and
**refuses** the calibration if either:

- the reading is **implausible for outdoors** (outside 250–600 ppm) — e.g. you never
  actually reached fresh air; or
- the reading **hasn't settled** (spread > 30 ppm across the last samples).

On refusal it tells you why ("Not fresh air" / "Air not settled") and changes nothing.
Let it settle, or find better air, and try again.

## Where NOT to recalibrate

Outdoor CO₂ is only ~420 ppm in *background* air. Avoid spots that read high or
unstable:

- near roads, traffic, or idling vehicles
- by exhaust vents, garages, loading docks, or combustion appliances (furnace flues,
  grills, generators)
- right next to people breathing
- still corners, alcoves, or hard against a building wall, where air pools

Open air, away from sources, on a calm-ish day is ideal. The countdown gives the sensor
and the air around it time to settle.

## Reference value & altitude

- **Fresh-air reference** (default **420 ppm**) is set in the web UI. Global background
  CO₂ rises ~2–3 ppm/year, so 420 is a reasonable 2026 value; bump it over time.
- **Altitude** affects the measurement, so set your altitude (meters above sea level) in
  settings — the sensor compensates for ambient pressure before calibrating. Applies
  after a restart.

## Calibration confidence

The device tracks days since the last successful FRC and escalates a cool-colored dot
(aging → stale → overdue). When **overdue**, the main number greys out to signal "don't
fully trust this until you recalibrate." All thresholds are configurable.

## Profiles

- **Sealed** (default): ASC off, manual FRC, the workflow above.
- **Ventilated**: ASC on — appropriate only if the space regularly reaches outdoor-like
  CO₂ (windows open, real air exchange). ASC *assumes* that periodic fresh-air exposure;
  in a sealed room it would read low.

Switching profiles changes the trust model, so re-establish calibration with a fresh-air
walk after a change.
