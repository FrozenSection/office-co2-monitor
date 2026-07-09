# stuffy — user manual

Everything you need to operate the monitor, in one place. Deeper background lives in
[calibration.md](calibration.md) (why/where to recalibrate), [placement.md](placement.md)
(enclosure & temp-offset tuning), and [brightness.md](brightness.md) (auto-dimming).

## The button

One momentary button does everything:

| Gesture | Action |
|---|---|
| **Tap** | Cycle views: CO₂ → Time → Diagnostics |
| **Double-tap** | Start recalibration (confirm screen: tap = yes, hold = cancel) |
| **Hold 3 s** | WiFi screen — setup AP + QR code when off-network; LAN address when on home WiFi. Tap to exit |
| **Hold 10 s** | **Reboot** (keep holding right through the WiFi screen). The no-tools restart — there's no power switch |

A true power-off requires unplugging USB *and* disconnecting the LiPo (back panel off).

## The screens

**CO₂ view (main).** Outer ring + status word carry the air-quality tier:
GOOD (green) → FAIR (yellow) → POOR (orange) → BAD (red); thresholds configurable.
The big number is white when trustworthy and **grey when it shouldn't be trusted** —
either the reading is stale or calibration is overdue. Other cues:

- **STALE** (amber status word): no fresh sensor reading for ~30 s. The number shown is
  the last good one, greyed.
- **"no sensor"** (red): the sensor never produced a reading after boot — check the QT
  cabling.
- **Trend arrow** (grey, neutral): CO₂ direction over the last ~2 minutes.
- **Battery glyph** (top): grey = healthy, amber ≤ 35%, red ≤ 15%. Only shown when the
  fuel gauge is fitted.
- **Calibration dot** (bottom): appears when calibration is aging — cool colors escalate
  teal → blue → purple → violet as it goes aging → stale → overdue. When *overdue*, the
  main number also greys.

**Time view.** Big clock; small CO₂ number with tier dot up top (greys when
stale/absent); temp + humidity below.

**Diagnostics view.** Network address & signal, battery, firmware version + uptime,
light/brightness, calibration age, free memory.

## Recalibration (the fresh-air walk)

The CO₂ sensor drifts; in **Sealed** profile you recalibrate it manually against outdoor
air (~420 ppm reference). Do it when the calibration dot escalates, or after moving the
device. Full background: [calibration.md](calibration.md).

1. Take the device outside to genuine background air — away from traffic, vents, people
   breathing, and still corners. It runs on its battery for hours; the walk needs ~5 min.
2. **Double-tap → tap to confirm.** A 3-minute countdown runs with a blue progress ring.
   Hold to cancel at any time.
3. At the end the device checks its own samples and either **calibrates** (shows the
   correction applied) or **refuses**, telling you why:
   - *"Not fresh air"* — the reading isn't plausible outdoor air; you're near a CO₂ source
     or still indoors.
   - *"Air not settled"* — readings were still moving; wait a minute and retry.
   - *"Sensor error"* — no/stale readings; check the sensor before trusting anything.
4. Every attempt (success or refusal) is recorded in the event log.

If the clock isn't set, a successful calibration can't be date-stamped — the result
screen warns, and the confidence dot stays unknown until time is set.

## Profiles

- **Sealed office (ASC off)** — for rooms that never see outdoor air. Calibration is
  manual (the walk above). *This is the right mode for the sealed office.*
- **Ventilated (ASC on)** — the sensor self-calibrates by assuming the room regularly
  reaches outdoor-fresh levels. Only correct for spaces with real air exchange.

Switching profiles applies after a restart. After a switch, do a fresh-air walk so the
new trust model starts from a known baseline.

## WiFi & the web pages

**WiFi on/off** is a toggle in Settings → Network. Changing it (then Save) restarts the
device automatically. With WiFi off, the radio is dead until you hold the button 3 s to
raise the **setup AP** — join it (QR on screen), configure, save.

On home WiFi, browse to **`http://stuffy.local`** (or the IP shown on the Diagnostics
view). Login is user **admin** + your web password. Pages:

- **Settings** — everything configurable: brightness/auto-dim + gamma, temp unit &
  offset, rotation, AQ thresholds, calibration reference/altitude/reminders, profile,
  log interval, device name, WiFi, time zone, web password. The footer notes which
  changes need a restart (button provided).
- **History** — CO₂ + temp graph; 24 h / 7 d / All ranges; **smooth** checkbox
  (display-only averaging — the stored data and CSV are always raw); CSV download.
- **Event log** — boots (with reset reason), calibration attempts, faults. Newest first.
- **Diagnostics** — live sensor/calibration/network/system/logging cards, refreshing
  every 5 s. **Copy diagnostics** produces a paste-able text dump (great for remote
  troubleshooting). **Erase logged data** wipes the graph history (settings, calibration,
  and events are kept).
- **Firmware** (`/update`) — OTA update: upload a compiled `.bin`, it flashes and
  reboots. Version confirms on the boot splash and Diagnostics.

Time is set from NTP: automatically when home WiFi connects, or via **Save & sync
time** (works from the setup AP too — it borrows your home WiFi for a moment). The
coin-cell RTC keeps time through power-offs afterward.

## Power & battery

USB-powered at the desk; the LiPo covers the calibration walk (hours) — it's not meant
for all-day cordless use, and always-on WiFi roughly halves battery runtime. The
Diagnostics page shows **Est. runtime** measured from the actual discharge rate.

## Tuning (one-time)

- **Temp offset** — the displayed temperature reads high until tuned in the final
  enclosure at thermal equilibrium: procedure in [placement.md](placement.md).
- **Altitude** — set your elevation (Settings → Calibration) so pressure compensation is
  right; applies after restart.
- **Auto-brightness** — lux low/high map room light to the min/max brightness; gamma
  shapes how gently it dims at the low end ([brightness.md](brightness.md)).

## Taking it to the office (checklist)

1. Profile → **Sealed office (ASC off)** → Save → Restart.
2. Do a **fresh-air walk** so it starts service freshly calibrated.
3. Settings → Network → **untick WiFi** → Save (it restarts, radio off).
4. Optional: note the last IP / hostname somewhere — reconfiguration at the office goes
   through the button-hold setup AP.

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| **STALE** on screen | Sensor stopped answering — check QT cables; hold 10 s to reboot |
| **"no sensor"** at boot | SCD-41 not detected — reseat the QT chain, reboot |
| Number is grey | Stale reading, or calibration overdue — recalibrate |
| Calibration keeps refusing | Read the reason on screen; find better outdoor air or let it settle |
| Can't reach stuffy.local | Try the IP from the Diagnostics view; check WiFi toggle is on |
| Forgot the web password | No recovery over the air — reflash over USB (erases NVS via `pio run -t erase`, then re-set everything) |
| Wrong time | Save & sync time (needs internet); check time zone setting |
| Temp reads high | Normal before the offset tune — see [placement.md](placement.md) |
| Device frozen / weird | Hold the button 10 s → reboot. Check the event log's reset reason after |
