# Placement, enclosure & temperature tuning

## Sensor placement

The SCD-41 should sample the room's air, not the device's own microclimate:

- Put the sensor **near an opening** in the enclosure so ambient air reaches it, and
  keep the dead volume around it small.
- Keep it **away from heat sources** — the ESP32, the 3.3 V regulator, the display
  backlight, and especially the LiPo while charging all run warm and will bias the
  temperature (and therefore humidity) reading.
- Avoid **direct sunlight** and any **direct fan/vent draft** on the sensor.
- Give it a **stable supply** — brownouts show up in the event log as `brownout` resets
  and can disturb the sensor and flash.

## Lux sensor window (optional VEML7700)

If the unit auto-dims, the lux sensor needs to see the room:

- Put a thin **window** over the VEML7700 — a couple of layers of clear/transparent
  filament is enough; only *relative* light matters, and the lux endpoints are tuned to
  the as-built window anyway.
- **Shield it from the display's own backlight glow** so the screen doesn't brighten
  itself in a feedback loop. Keep the sensor close to its window and walled off from
  internal light bleed.

The dimming behavior and how to tune the lux endpoints/gamma to that window are covered
in [brightness.md](brightness.md).

## Temperature / humidity offset

The SCD-41 measures temperature at its own die, which sits warmer than the room because
of self-heating plus nearby electronics. It subtracts a **temperature offset** to report
ambient:

```
reported = raw − offset
```

The factory default is **4.0 °C** (stored here as `40`, in 0.1 °C units). The offset
corrects the displayed temperature **and** humidity; it does **not** affect CO₂
accuracy.

The right offset is specific to the finished build, so tune it once everything is in its
enclosure:

1. Assemble the device the way it will actually run — in its case, on its normal power,
   in its normal spot, with WiFi in its normal mode (always-on WiFi adds noticeable
   self-heat).
2. Let it reach thermal **steady state** — at least ~1 hour, undisturbed, no draft or
   sun.
3. Read the device's reported temperature and a trusted reference thermometer placed
   right next to it, in the same air.
4. Compute, in °C:

   ```
   new_offset = current_offset + (reported − reference)
   ```

   Example: offset 4.0, device reads 2.1 °C high → new offset 6.1 → enter `61`.
5. Save, **restart** (the offset is applied at boot), let it re-settle, and re-check.
   One pass usually nails it.

Do the arithmetic in °C even if the display shows °F (Δ°F ÷ 1.8 = Δ°C). Change the
enclosure, power source, or WiFi mode later and the offset drifts — re-tune.

## Thermal sanity check

After the final assembly equilibrates, compare the displayed temperature to a reference.
A few degrees high *before* tuning is normal (self-heating); large or drifting error
points to the sensor sitting too close to a heat source — relocate it within the
enclosure before trying to cancel it with the offset.
