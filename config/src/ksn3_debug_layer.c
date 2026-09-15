/*
 * 임시 디버깅 전용 파일 (2026-09-15): Fn(mo 1)이 양쪽 다 안 먹는 문제 진단용.
 *
 * 목적: 물리 키 매트릭스 이벤트(zmk_position_state_changed)와 레이어 상태
 * 변화(zmk_layer_state_changed)를 그대로 로그로 찍어서, Fn 키를 눌렀을 때
 * (1) 그 물리 위치의 press 이벤트가 central까지 도달하는지,
 * (2) 그게 실제로 layer 1을 활성화시키는지를 눈으로 직접 확인한다.
 *
 * zmk_position_state_changed는 로컬(각 보드 자신의 매트릭스)에서도 발생하므로
 * 이 파일은 central/peripheral 양쪽 다 컴파일되게 두었다 - 왼쪽(peripheral)
 * 디버그 빌드에서는 "왼쪽 Fn 스위치 자체가 눌리는지"를, 오른쪽(central)
 * 디버그 빌드에서는 "그 이벤트가 실제로 레이어를 바꾸는지"를 각각 확인하는 용도.
 *
 * 원인 파악 후 반드시 이 파일과 CMakeLists.txt의 target_sources 줄을 제거할 것.
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static int ksn3_debug_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    LOG_INF("[KSN3-DEBUG] position=%d state=%d", ev->position, ev->state);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(ksn3_debug_position, ksn3_debug_position_listener);
ZMK_SUBSCRIPTION(ksn3_debug_position, zmk_position_state_changed);

static int ksn3_debug_layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    LOG_INF("[KSN3-DEBUG] layer=%d state=%d", ev->layer, ev->state);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(ksn3_debug_layer, ksn3_debug_layer_listener);
ZMK_SUBSCRIPTION(ksn3_debug_layer, zmk_layer_state_changed);
