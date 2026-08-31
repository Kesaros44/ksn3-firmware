# ksn3-firmware

ZMK firmware configuration for the KSN-3 split keyboard. This is a **new board**, built from scratch by reverse-engineering the KSN-3 EasyEDA schematic (`SCH_KSN3L_20260831.json` / `SCH_KSN3R_20260831.json`) and porting the driver architecture, build-system structure, and hard-won operational lessons from [ksn1-firmware](https://github.com/Kesaros44/ksn1-firmware).

* Keyboard Maintainer: [AJG](https://github.com/Kesaros44)
* Hardware Supported: KSN-3 split keyboard, nice!nano v2 (nRF52840), BLE

## ⚠️ Read this before flashing

This repo has **not been tested on physical hardware yet**. It splits into two confidence levels:

- **High confidence — derived directly from the schematic, double-checked two independent ways** (manual visual reading of the rendered schematic images, and programmatic coordinate-matching of the raw EasyEDA JSON, with full agreement): every row/col GPIO pin, the encoder's GPIO pins and matrix role, which half the two status LEDs are wired to, and which half is central. These are the parts most likely to cause a *non-working board* if wrong, and they're the parts I'm most sure of.
- **My judgment call, needs your review — the keymap's specific key assignments.** The *physical switch positions* (which matrix cell is where) come straight from the schematic and are solid. But *what key each position sends* (`config/ksn_3.keymap`) is a first draft I wrote by inference from position/shape, not from a labelled keycap layout — I do not know what's silkscreened on your actual keycaps or plate. Treat the keymap as a working starting point, not a final answer: flash it, check it against the physical board, and re-flash after edits. No hardware risk either way — a wrong keycode is just an inconvenience, not a wiring problem.

One item flagged inline in the devicetree itself: `status_led` (COND) is set to `GPIO_ACTIVE_HIGH` as a first guess. KSN-1 had this exact LED inverted (lit when it should've been dark) and needed `GPIO_ACTIVE_LOW` instead — if KSN-3's status LED looks backwards after flashing, that's the first thing to flip (`config/boards/shields/ksn_3/ksn_3_left.overlay`).

## Hardware

- **MCU:** nice!nano v2 (nRF52840) per half, wireless (BLE)
- **Central:** the **right** half (matches the switch matrix layout — no `POS_STATE_LEN` overflow risk like KSN-1 originally had, since KSN-3's matrix is 6×22, same size as KSN-1's corrected layout)
- **Matrix:** 6 rows x 22 columns combined (11 columns per half, 55 switches on the left / 54 on the right), `col2row`
- **Encoder:** one EC11 encoder on the right half (central) only, with its pushbutton (SW50) wired into the matrix itself (row1/col10 on the right half) rather than a separate GPIO — treat it as an ordinary key in the keymap, not a special encoder-click binding
- **LEDs (left/peripheral only):** a Caps Lock LED (P0.02) and a BLE connection-status LED (P0.29, labelled COND on the schematic), each driven by a custom driver (see below) rather than ZMK's built-in indicator node
- **Backlight:** the schematic has a populated MOSFET switching circuit (Q1 + gate resistors) on both halves, same as KSN-2, so backlight is **enabled** by default here (unlike KSN-1, which ships with it off by request). Set `CONFIG_ZMK_BACKLIGHT=n` in both `.conf` files if you want it off like KSN-1.
- **RGB underglow:** not present on this board (`CONFIG_ZMK_RGB_UNDERGLOW=n`)
- **Battery:** reporting enabled on both halves; central also periodically fetches the peripheral's battery level and reports it to the host alongside its own (`CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING`/`_PROXY`, with `CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=1` set explicitly — see the KSN-2 lesson below)

### Custom LED drivers (why they exist)

Identical rationale to KSN-1: ZMK's built-in `zmk,indicator-leds` devicetree node calls central-only functions (`zmk_hid_indicators_get_current_profile()`, `zmk_endpoint_is_connected()`), which aren't linkable on a peripheral build — but both of this board's status LEDs are physically wired to the peripheral (left) half. Two custom drivers, ported near-verbatim from ksn1-firmware, work around this:

- **`ksn3_peripheral_indicators.c`** — drives the Caps Lock LED by subscribing to the `zmk_hid_indicators_changed` event instead, which *is* safe on a peripheral (central pushes the host's raw HID indicator bitmask over BLE when `CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS=y` is set on both sides).
- **`ksn3_conn_status_relay_central.c`** / **`ksn3_conn_status_relay_peripheral.c`** — there's no built-in ZMK channel for "does central have an active host connection" on a peripheral LED, so these add a private BLE GATT service (freshly generated UUIDs, distinct from KSN-1's) on top of the existing split link: central polls `zmk_endpoint_is_connected()` every 250ms and writes a byte to the peripheral over that private characteristic whenever it changes; the peripheral drives the LED solid when connected, blinking (300ms) when not. GATT discovery for this side-channel is deliberately delayed 3s after a split reconnect so it doesn't compete with ZMK's own critical-path discovery (position state, indicators, battery) for the connection's single-outstanding-ATT-request slot.

All three files are wired into the build via `config/zephyr/module.yml` (registers `config/` as a Zephyr module) + `config/CMakeLists.txt` (`target_sources(app PRIVATE src/...)`, paths relative to `config/`) + the source files under `config/src/`. If you ever add another custom `.c` file here, all three have to agree — a missing or misplaced piece silently compiles the file out with no build error.

Unlike KSN-1/KSN-2, this repo's `config/west.yml` does **not** pull in the old `zmk-poor-mans-led-indicator` module — it's dead weight left over from an earlier iteration on those boards and was never needed here since KSN-3 starts fresh with the GATT-relay approach already in place.

### Keymap (`config/ksn_3.keymap`)

Three layers, structurally mirroring KSN-1's layout (see the confidence note above — the *positions* are schematic-verified, the *assignments* are a draft):

- **`default_layer`** — Windows base layer with an integrated numpad on the left columns. Encoder = volume up/down. The numpad's corner key runs a macro (Win+R → `calc` → Enter) to launch the Windows calculator, ported verbatim from KSN-1 (same timing).
- **`func_layer`** (momentary, `&mo 1`) — Bluetooth profile select (0–4) and clear, output toggle (`OUT_TOG`), backlight inc/dec on the encoder, and a toggle (`&tog 2`) into `mac_layer`.
- **`mac_layer`** — same base layout with Mac modifier ordering (Cmd/Opt swapped versus Windows) and Mac media/brightness keys in place of the F-row; its numpad corner key instead runs a macro (Cmd+Space → `calculator` → Enter) for Spotlight, with the longer wait times KSN-1 needed to account for Spotlight's indexing/animation delay.

## Building

CI (`.github/workflows/`) uses ZMK's standard reusable workflow (`zmkfirmware/zmk/.github/workflows/build-user-config.yml@main`) — no custom build logic, identical to KSN-1/KSN-2. `build.yaml` defines three targets:

```yaml
include:
  - board: nice_nano//zmk
    shield: ksn_3_left
    snippet: zmk-usb-logging
  - board: nice_nano//zmk
    shield: ksn_3_right
    snippet: zmk-usb-logging
  - board: nice_nano//zmk
    shield: settings_reset
    artifact-name: settings_reset
```

Pushing to this repo builds `ksn_3_left`, `ksn_3_right`, and a `settings_reset` firmware as GitHub Actions artifacts — grab the `.uf2` files from the workflow run.

To build locally with `west`:

```sh
west init -l config
west update
west build -p -b nice_nano_v2 -- -DSHIELD=ksn_3_left -DZMK_EXTRA_MODULES=$(pwd)/config
west build -p -b nice_nano_v2 -- -DSHIELD=ksn_3_right -DZMK_EXTRA_MODULES=$(pwd)/config
```

## Flashing

Double-tap reset on the nice!nano to enter its UF2 bootloader, then drag the matching `.uf2` (left firmware to the left half, right to the right half) onto the mounted `NICENANO` drive. Flash both halves — they run different firmware images (only the right/central image has `CONFIG_ZMK_SPLIT_ROLE_CENTRAL=y`, the encoder, and the conn-status relay's central half).

## Re-pairing / clearing Bluetooth bonds

Flash the `settings_reset` artifact (built alongside the two main targets) to a half to wipe its stored BLE bonds, then reflash that half's normal firmware and re-pair.

## Lessons carried over from KSN-1 / KSN-2 (proactively applied here)

These were real bugs found and fixed on the earlier boards. Rather than wait to hit them again, KSN-3 applies the fixes from day one:

- **`wakeup-source` on `kscan0` + `CONFIG_PM_DEVICE=y` on central** — without both, the board pairs but never reconnects after sleep ("paired but not connected"), because the peripheral's scan/encoder GPIOs can't raise an interrupt to wake the MCU. Already in `ksn_3.dtsi` / `ksn_3_right.conf`. (The EC11 encoder's `alps,ec11` binding does **not** support `wakeup-source` — it's deliberately not added there.)
- **`CONFIG_BT_MAX_CONN` left at its default (2)** — KSN-1 tried raising it to 3 for more profile slots and got a repeating `Security failed ... err 4` (`AUTH_REQUIREMENT`) loop, strictly worse than the slot-exhaustion symptom it was meant to fix. Not touched here.
- **`CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=1` set explicitly** — this Kconfig defaults to 0 if unset, which silently drops the peripheral's battery-level events on KSN-2 until someone noticed the reported battery just never updated. Set explicitly in `ksn_3_right.conf` from the start.
- **No external battery-voltage-divider node** — KSN-1 originally had one and got bogus readings (528mV/0%-type values); removed in favor of the nice!nano v2 board default (`zmk,battery-nrf-vddh`). KSN-3 never had a custom one to begin with.
- **Debugging tip:** don't set `CONFIG_LOG_DEFAULT_LEVEL` by hand to chase a specific symptom — it's a *global* Zephyr log level and can flood the log buffer with unrelated debug output (e.g. from the USB driver), dropping the messages you actually want. Use the `zmk-usb-logging` build snippet (already in `build.yaml`) instead.

## Schematic source

`config/` was derived from `SCH_KSN3L_20260831.json` (left half) and `SCH_KSN3R_20260831.json` (right half) — EasyEDA schematic exports. If the physical board ever gets revised, the GPIO pin comments in `ksn_3.dtsi` / `ksn_3_left.overlay` / `ksn_3_right.overlay` are the first place to check against the new schematic's net labels.
