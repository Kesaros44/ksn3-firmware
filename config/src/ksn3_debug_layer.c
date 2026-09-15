/*
 * 임시 디버깅 전용 파일 (2026-09-15): Fn(mo 1)이 양쪽 다 안 먹는 문제 진단용.
 *
 * 주의(2026-09-15): 처음엔 zmk_layer_state_changed도 같이 구독하려 했으나,
 * 이 ZMK 버전(4.1.0) 빌드에서는 그 이벤트 구현(.c)이 링크에 포함되지 않아
 * (as_zmk_layer_state_changed / zmk_event_zmk_layer_state_changed undefined
 * reference) 빌드가 실패함 - 제거함. 대신 zmk_position_state_changed(물리
 * 매트릭스 이벤트, ksn3_word_flip.c가 이미 쓰는 zmk_keycode_state_changed와
 * 마찬가지로 이 빌드에서 정상 링크됨)만으로 "그 키가 central에 도달하는지"를
 * 확인한다. 원인 파악 후 이 파일과 CMakeLists.txt의 target_sources 줄을
 * 함께 제거할 것.
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

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
