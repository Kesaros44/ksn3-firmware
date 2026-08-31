/*
 * Central-side counterpart to ksn3_conn_status_relay_peripheral.c.
 *
 * Every KSN3_POLL_MS, checks whether this half (currently ksn_3_right,
 * the split central) has an active connection to a PC/host, and - only
 * when that state actually changes - writes a single byte to the custom
 * GATT characteristic the peripheral (left half) exposes for this. See
 * ksn3_conn_status_relay_peripheral.c for the full rationale on why a
 * dedicated GATT service is used instead of piggybacking on HID
 * indicators.
 *
 * FINDING THE RIGHT BLE CONNECTION
 * ---------------------------------
 * This half has up to two simultaneous BLE connections:
 *   1. To the PC/host - on that link we are the GATT SERVER (peripheral
 *      role), since we present as a HID keyboard to it.
 *   2. To the other keyboard half - on that link we are the GATT CLIENT
 *      (central role), since we're the one discovering the peripheral's
 *      services (ZMK's own split protocol works the same way).
 * bt_conn_get_info()->role tells these apart without touching any
 * ZMK-internal split state: BT_CONN_ROLE_CENTRAL only ever matches
 * connection #2.
 *
 * Only builds on the central (currently ksn_3_right).
 *
 * Ported verbatim from ksn1-firmware's ksn1_conn_status_relay_central.c
 * (same poll/discovery-delay constants, same connection-tracking logic -
 * none of this depends on KSN-3's specific matrix/pinout, only on the
 * central/peripheral split role, which is architecturally identical).
 *
 * NOTE: as of the ZMK version this is built against, zmk_endpoint_is_connected()
 * takes no arguments - it reports the connection state of whichever
 * endpoint is currently selected internally. If this breaks on a future
 * ZMK update, check the current zmk/app/include/zmk/endpoints.h for the
 * exact signature and adjust - the rest of this file doesn't need to
 * change.
 */

#include <zephyr/devicetree.h>

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include <zmk/endpoints.h>

#include "ksn3_conn_status_relay.h"

LOG_MODULE_REGISTER(ksn3_conn_status_relay_central, CONFIG_ZMK_LOG_LEVEL);

#define KSN3_POLL_MS 250
/* Wait this long after (re)detecting the split link before starting our
 * own GATT discovery. ZMK's own split central code (central.c) also runs
 * its discovery right after a (re)connection - position state, HID
 * indicators, battery level, physical layout, etc. - and that's on the
 * critical path for the keyboard actually working again. Our discovery is
 * just for an LED and isn't urgent, but competing for the same
 * single-outstanding-request-per-connection ATT bearer at exactly the
 * same moment slowed that down on KSN-1, which is why reconnects got
 * noticeably slower after that file was added there. Deferring ours
 * fixes it, same as on KSN-1. */
#define KSN3_DISCOVERY_DELAY_MS 3000

static struct bt_conn *peripheral_conn;
static uint16_t char_value_handle;
static bool discovery_done;
static bool have_sent;
static bool last_sent_state;

static struct bt_gatt_discover_params discover_params;
static struct bt_uuid_128 discover_svc_uuid = KSN3_CONN_STATUS_SERVICE_UUID;
static struct bt_uuid_128 discover_char_uuid = KSN3_CONN_STATUS_CHAR_UUID;

static struct k_work_delayable poll_work;
static struct k_work_delayable discovery_start_work;

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params) {
    ARG_UNUSED(conn);

    if (!attr) {
        /* Nothing found this pass; refresh_peripheral_conn() will retry
         * on the next poll tick if we're still connected. */
        return BT_GATT_ITER_STOP;
    }

    if (params->type == BT_GATT_DISCOVER_PRIMARY) {
        discover_params.uuid = &discover_char_uuid.uuid;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
        discover_params.start_handle = attr->handle + 1;
        discover_params.end_handle = 0xffff;
        bt_gatt_discover(conn, &discover_params);
        return BT_GATT_ITER_STOP;
    }

    if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
        char_value_handle = bt_gatt_attr_value_handle(attr);
        discovery_done = true;
        LOG_INF("ksn3_conn_status: found peripheral char, handle %d", char_value_handle);
        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_STOP;
}

static void start_discovery(struct bt_conn *conn) {
    discovery_done = false;
    char_value_handle = 0;

    discover_params.uuid = &discover_svc_uuid.uuid;
    discover_params.func = discover_func;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;

    int err = bt_gatt_discover(conn, &discover_params);
    if (err) {
        LOG_WRN("ksn3_conn_status: discovery start failed (%d)", err);
    }
}

static void discovery_start_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (peripheral_conn) {
        start_discovery(peripheral_conn);
    }
}

static bool conn_is_peripheral_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0) {
        return false;
    }
    return info.type == BT_CONN_TYPE_LE && info.role == BT_CONN_ROLE_CENTRAL;
}

static void find_conn_cb(struct bt_conn *conn, void *data) {
    struct bt_conn **out = data;
    if (*out) {
        return; /* already found one */
    }
    if (conn_is_peripheral_link(conn)) {
        /* bt_conn_foreach() only guarantees `conn` is valid for the
         * duration of this callback - take our own ref so it's still
         * valid once we get back to refresh_peripheral_conn() and beyond.
         * refresh_peripheral_conn() is responsible for unref'ing this. */
        *out = bt_conn_ref(conn);
    }
}

static void refresh_peripheral_conn(void) {
    struct bt_conn *found = NULL;
    bt_conn_foreach(BT_CONN_TYPE_LE, find_conn_cb, &found);

    /* bt_conn_foreach() only guarantees `found` is valid for the duration
     * of that call - it does NOT hand us a reference we can keep around
     * across poll ticks. Take our own reference before storing it, and
     * drop our old one whenever it's no longer the current link, or we'll
     * end up holding (and later dereferencing / writing to) a stale
     * connection object once it's freed elsewhere. */

    if (found == peripheral_conn) {
        if (found) {
            bt_conn_unref(found); /* drop the extra ref bt_conn_foreach gave us */
        }
        return;
    }

    if (peripheral_conn) {
        bt_conn_unref(peripheral_conn);
    }

    peripheral_conn = found; /* already ref'd via bt_conn_foreach, if non-NULL */
    discovery_done = false;
    char_value_handle = 0;
    have_sent = false;

    if (peripheral_conn) {
        /* Don't discover right away - see KSN3_DISCOVERY_DELAY_MS comment. */
        k_work_reschedule(&discovery_start_work, K_MSEC(KSN3_DISCOVERY_DELAY_MS));
    } else {
        k_work_cancel_delayable(&discovery_start_work);
    }
}

static void send_state(bool connected) {
    if (!peripheral_conn || !discovery_done || char_value_handle == 0) {
        return;
    }

    uint8_t val = connected ? 1 : 0;
    int err = bt_gatt_write_without_response(peripheral_conn, char_value_handle, &val,
                                              sizeof(val), false);
    if (err) {
        LOG_WRN("ksn3_conn_status: write failed (%d)", err);
        return;
    }
    last_sent_state = connected;
    have_sent = true;
}

static void poll_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    refresh_peripheral_conn();

    bool connected = zmk_endpoint_is_connected();

    if (!have_sent || connected != last_sent_state) {
        send_state(connected);
    }

    k_work_reschedule(&poll_work, K_MSEC(KSN3_POLL_MS));
}

static int ksn3_conn_status_relay_central_init(void) {
    k_work_init_delayable(&poll_work, poll_work_handler);
    k_work_init_delayable(&discovery_start_work, discovery_start_work_handler);
    k_work_reschedule(&poll_work, K_MSEC(KSN3_POLL_MS));
    return 0;
}

SYS_INIT(ksn3_conn_status_relay_central_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_ZMK_SPLIT_ROLE_CENTRAL */
