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

# 한/영 오타 자동 되돌리기 트리거 behavior (&word_flip) - KSN-1의
# ksn1_word_flip.c를 그대로 가져온 것. DT_HAS_COMPAT_STATUS_OKAY로 자체
# 게이팅되므로 devicetree에 노드가 없는 빌드(예: settings_reset)에서는
# 그냥 빈 번역 단위가 됨.
target_sources(app PRIVATE src/ksn3_word_flip.c)

# 임시 디버깅 파일 (2026-09-15) - Fn(mo 1) 양쪽 다 안 먹는 문제 진단용.
# 원인 파악 후 이 줄과 config/src/ksn3_debug_layer.c를 함께 제거할 것.
target_sources(app PRIVATE src/ksn3_debug_layer.c)
