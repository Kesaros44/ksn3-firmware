#pragma once

/*
 * Custom BLE UUIDs used to relay "is the central connected to a PC/host
 * right now?" from the central half (currently ksn_3_right) to the
 * peripheral half (currently ksn_3_left), so the peripheral can drive
 * status_led accordingly (solid = connected, blinking = not connected).
 *
 * These are private, randomly-generated UUIDs. They only need to:
 *   - match between ksn3_conn_status_relay_central.c and
 *     ksn3_conn_status_relay_peripheral.c (both include this header), and
 *   - not collide with any other GATT service on the same BLE connection.
 * ZMK's own split service (position state, HID indicators, battery, etc.)
 * uses entirely different UUIDs defined in
 * zmk/app/include/zmk/split/bluetooth/uuid.h, so there's no conflict -
 * this is just a second, independent GATT service layered on the same
 * physical link the two halves already use to talk to each other.
 *
 * Ported from ksn1-firmware's ksn1_conn_status_relay.h - freshly generated
 * UUIDs (distinct from KSN-1's) so the two boards' GATT services never
 * collide if they're ever both nearby / paired at once.
 */

#define KSN3_CONN_STATUS_SERVICE_UUID                                                            \
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xc485b355, 0xb80b, 0x413c, 0x9ef4, 0xd047bf9a5f3a))

#define KSN3_CONN_STATUS_CHAR_UUID                                                               \
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xbc61e89d, 0x187f, 0x4588, 0x97c3, 0x1d556723c8ee))
