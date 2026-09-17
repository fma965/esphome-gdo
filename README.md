# esphome-gdo [![Made for ESPHome](https://img.shields.io/badge/Made_for-ESPHome-black?logo=esphome)](https://esphome.io)

Forked from [tronikos/esphome-gdo](https://github.com/tronikos/esphome-gdo). See
[Changes in this fork](#changes-in-this-fork) below for what's different and why.

This [ESPHome](https://esphome.io) external component allows control of a Garage Door Opener with a relay and one or two reed sensors. Supports:

- open/close/stop control
- most importantly position reporting and control, distinguishing it from other similar projects
- obstruction sensor

See the included `example-gdo.yaml` for my personal setup with just one reed sensor at the fully-open position.

## Changes in this fork

Upstream assumes a specific button behavior for the closing direction (see
the original list under [Hardware requirements](#hardware-requirements)): a
single press while closing reverses straight to opening, with no stop
phase. My opener doesn't work that way -- a single press stops the door in
*either* direction, and only a separate, later press reverses it (a strict
open → stop → close → stop → open toggle cycle). Running the upstream
component as-is against that hardware caused two real bugs:

- **Stopping while closing** sent a double press, on the assumption that
  the first press would reverse it to opening and the second would then
  stop that. On my motor the first press already stops the door, so the
  second press fired it straight back open again -- stopping the door
  mid-close would make it re-open a moment later.
- **Reversing from closing to opening** sent a single press, on the
  assumption that alone reverses it. On my motor a single press while
  closing only stops the door; reversing needs a second, separate press.

Both are fixed in `gdo_cover.cpp` for the "press always stops, next press
reverses" behavior. Pressing once while **opening** was already correct
either way -- every controller of this kind agrees a single press stops the
door while it's opening.

This fork also makes "resume from a stopped, partial position" aware of
which direction the door was actually moving before it stopped (tracked
internally), so reversing from a stop is a clean single press in whichever
direction is actually a reversal. One case remains an inherent hardware
limit rather than something software can paper over: resuming the *same*
direction the door was already moving in before an explicit stop needs
three presses on this kind of toggle motor (stop → reverse → stop →
reverse), and the component only has single- and double-press actions.
That case falls back to a best-effort double press, which will look like a
brief flinch the wrong way before it settles in the direction you asked
for.

If your opener matches the *original* button behavior (closing + single
press reopens it, no stop phase), use upstream instead -- this fork's fixes
would be wrong for that hardware.

## Configuration

Add the component to your config:

```yaml
external_components:
  - source: github://fma965/esphome-gdo@main
```

### `cover` platform `gdo`

| Option                | Type     | Description                                                        |
|-----------------------|----------|--------------------------------------------------------------------|
| `open_duration`       | Required | Time the door takes to travel from fully closed to fully open.     |
| `close_duration`      | Required | Time the door takes to travel from fully open to fully closed.     |
| `single_press_action` | Required | Automation that pulses the relay once.                             |
| `double_press_action` | Required | Automation that pulses the relay twice.                            |
| `open_endstop`        | Optional | ID of a binary sensor that reads on when the door is fully open.   |
| `close_endstop`       | Optional | ID of a binary sensor that reads on when the door is fully closed. |

At least one of `open_endstop` / `close_endstop` is required. Without one the
position estimate can never be corrected, so it drifts.

The durations are used two ways: to interpolate the position while the door
moves, and as the basis for the endstop timeout. The timeout allows 25% plus 2s
over the configured duration before it gives up and reports an unknown position,
so the durations do not have to be exact to the millisecond.

Position is held just short of 100% / 0% until the corresponding endstop
actually confirms it, so the cover does not read "fully open" while the door is
still moving.

### `binary_sensor` platform `gdo`

Reports the state of the safety obstruction sensor.

| Option           | Type     | Description                                                                                                       |
|------------------|----------|---------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `input_obst_pin` | Required | Pin wired to the obstruction sensor circuit. Must be a pin on the ESP itself, since it is read with an interrupt. |

Defaults to `device_class: problem` and `entity_category: diagnostic`.

## Hardware requirements

- A Garage Door Opener that you open/close with a single button. The behavior of the button is expected to be:
  - If door is closed a single press opens it.
  - If door is (fully or partially) open a single press closes it.
  - If door is opening a single press stops it.
  - If door is closing a single press stops it (this is the one that differs from upstream -- see [Changes in this fork](#changes-in-this-fork)).
  - A single press while stopped reverses whatever direction the door was moving in just before it stopped.
- ESP board [compatible](https://esphome.io/#devices) with ESPHome.
- Relay to either press the physical button of the wall control panel (for Chamberlain Security + 2.0) or short the controls on the garage door opener itself (for Chamberlain Security + 1.0 or Genie etc.).
- One or two reed sensors to detect the fully-open and/or fully-closed states. If using a single reed sensor, it can be placed in either fully-open or fully-closed positions.
- Optional two 10kΩ resistors to detect the obstruction sensor.

## Credits

- Claude Sonnet for clanking away with me while i figured out what i needed!
- Forked from [tronikos/esphome-gdo](https://github.com/tronikos/esphome-gdo).
- Adopted from [Endstop Cover](https://esphome.io/components/cover/endstop) and [Time Based Cover](https://esphome.io/components/cover/time_based).
- The circuit for the obstruction sensor is from [rat-ratgdo](https://github.com/Kaldek/rat-ratgdo).
- The code for the obstruction sensor is from [esphome-ratgdo](https://github.com/ratgdo/esphome-ratgdo).

## Dev notes

```sh
python3 -m venv .venv
source .venv/bin/activate
# for Windows CMD:
# .venv\Scripts\activate.bat
# for Windows PowerShell:
# .venv\Scripts\Activate.ps1

pip install esphome pre-commit

pre-commit install

pre-commit run --all-files

# Compile with local code instead of pulling from Github
esphome -s external_components_source components compile example-gdo.yaml

# Deploy local code
esphome -s external_components_source components run example-gdo.yaml

# Validate the same configs CI does
esphome config tests/test-*.yaml
```
