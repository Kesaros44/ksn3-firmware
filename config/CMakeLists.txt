# KSN3 config module. See config/src/ksn3_peripheral_indicators.c for why this
# exists: ZMK's core zmk,indicator-leds driver can't drive an LED that is
# physically wired to a split peripheral (the left half here), because it
# links against central-only endpoint/HID-indicator symbols. Same pattern as
# ksn1-firmware.
target_sources(app PRIVATE src/ksn3_peripheral_indicators.c)

# status_led (COND) central<->PC connection relay: one file per side, each is
# a no-op (empty translation unit) on the side it doesn't apply to - see the
# comment block at the top of ksn3_conn_status_relay_peripheral.c for why
# this exists.
target_sources(app PRIVATE src/ksn3_conn_status_relay_peripheral.c)
target_sources(app PRIVATE src/ksn3_conn_status_relay_central.c)
