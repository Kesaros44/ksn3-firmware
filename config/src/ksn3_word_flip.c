/*
 * "단어 되돌리기" (word-flip) 트리거 behavior.
 *
 * 목적: 방금 입력한 단어가 한/영 오타(다른 언어 모드로 잘못 입력됨)일 때,
 * 사용자가 직접 판단하고 트리거 키를 눌러 그 단어를 지우고 반대 언어
 * 모드로 재입력한다. 호스트의 현재 IME 상태를 알 필요가 없다 - 무조건
 * "반대로 뒤집기"만 하므로 상태 추정이 틀릴 일이 없다 (사람이 트리거
 * 시점을 판단하기 때문).
 *
 * 동작:
 *   1. 이 파일의 리스너가 모든 A-Z 키 입력을 최근 순서대로 버퍼에 담아둔다.
 *      스페이스/엔터/탭/그 외 A-Z가 아닌 키가 눌리면 버퍼를 비운다(단어
 *      경계).
 *   2. 트리거 키(&word_flip <os>)를 누르면:
 *      a. 현재 버퍼를 로컬로 복사하고 전역 버퍼를 즉시 비운다.
 *      b. 한/영 전환 전송 - Windows(param1=0)는 LANG1, macOS(param1=1)는
 *         Caps Lock. 삭제보다 먼저 보내야 한다(아래 on_word_flip_binding_
 *         pressed의 순서 주석 참고).
 *      c. 단어 삭제 조합 전송 - Windows는 Ctrl+Backspace, macOS는
 *         Option+Backspace.
 *      d. 복사해둔 버퍼를 그대로 순서대로 재입력.
 *
 * 전부 zmk_behavior_queue_add()로 큐에 넣는다 - ZMK의 매크로 behavior가
 * 쓰는 것과 동일한 공식 비동기 큐 API라서, 이 파일이 어떤 스레드
 * 컨텍스트에서 호출되든 블로킹 없이 안전하게 순서대로 처리된다.
 *
 * 주의(2026-09-06): KSN-1의 ksn1_word_flip.c에서 그대로 가져온 파일이다
 * (KSN-1에서 실기 동작 확인된 버전). 이 세션에 west 툴체인이 없어 컴파일 검증을
 * 못 했음. ZMK 공식 문서와 이 저장소에 이미 있는 검증된 코드(calc_macro,
 * ksn3_conn_status_relay 등)의 실제 API를 최대한 그대로 재사용했지만,
 * GitHub Actions 빌드 결과를 반드시 먼저 확인할 것.
 */

/*
 * 수정 이력(2026-09-15): "웃참실패" 같은 실기 오작동 제보를 근거로 맥 삭제
 * 방식을 Option+Backspace -> 반복 Backspace로, 대기시간을 350->700ms로
 * 바꿨었으나, 이후 KSN-2에서 동일 계열 증상이 발생했을 때 로직을 건드리지
 * 않고 "KSN-1과 100% 동일한 버전"으로 되돌리는 것만으로 해결된 사례가
 * 확인됨 (ksn2_word_flip.c 커밋 이력 참고). 즉 Option+Backspace 자체가
 * 한글 조합 텍스트에 근본적으로 안 되는 것이 아니라, 당시 테스트 바이너리가
 * 최신 소스를 반영 못 했거나(스테일 빌드/플래시) 다른 요인이었을 가능성이
 * 높다고 판단, KSN-1과 다시 100% 동일하게(Option+Backspace, 350ms) 되돌림.
 * 코드보다 먼저 "클린 빌드 + 재플래시"부터 확인할 것.
 */

#define DT_DRV_COMPAT ksn_behavior_word_flip

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>

#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if (!defined(CONFIG_ZMK_SPLIT) || defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)) && \
    DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define WORD_FLIP_MAX_LEN 24
#define WORD_FLIP_TAP_MS 40
#define WORD_FLIP_WAIT_MS 30
/* macOS는 한/영 전환에 Caps Lock을 쓰는데(시스템 설정 "Caps Lock 키로 ABC
 * 입력 소스 전환"), 이 전환은 즉시 반영되지 않고 수백 ms 지연이 있는 것으로
 * 잘 알려져 있다. Windows의 LANG1처럼 30ms 뒤에 바로 다음 키를 보내면 아직
 * 이전 입력 모드인 상태에서 삭제/재입력이 들어가 버린다. 그래서 맥에서는
 * 전환 키 뒤에만 넉넉히 기다린다. */
#define WORD_FLIP_MAC_TOGGLE_WAIT_MS 600

struct word_flip_key {
    uint32_t keycode;
    uint8_t explicit_modifiers;
};

static struct word_flip_key buffer[WORD_FLIP_MAX_LEN];
static size_t buffer_len;

/* macOS 전용: 지금 입력 소스가 한글인지 추적한다. 캡스락 전환은 상태를
 * OS에 물어볼 방법이 없는 순수 토글이라, 이 behavior가 보낸 전환 횟수로
 * 직접 추적하는 수밖에 없다. word_flip 트리거로만 전환한다는 전제이며,
 * 메뉴바나 다른 방법으로 직접 전환하면 이 추적이 어긋날 수 있다.
 * 기본값은 영어(false)로 가정 - 부팅 직후 실제 입력 소스가 한글이라면
 * 처음 한 번은 어긋날 수 있음. */
static bool mac_is_korean = false;

/* 주의: 이벤트의 ev->keycode는 usage page가 빠진 순수 usage ID(A=0x04 ...
 * Z=0x1D)다. 반면 keys.h의 A/Z 매크로는 ZMK_HID_USAGE(page, id)로 page가
 * 상위 비트에 붙은 값(A=0x70004)이라 그대로 비교하면 절대 참이 되지 않는다.
 * page는 ev->usage_page로 따로 확인하고, 범위 비교는 ZMK_HID_USAGE_ID()로
 * 벗겨낸 ID끼리 해야 한다. */
static bool is_letter(uint16_t usage_page, uint32_t keycode) {
    return usage_page == HID_USAGE_KEY && keycode >= ZMK_HID_USAGE_ID(A) &&
           keycode <= ZMK_HID_USAGE_ID(Z);
}

/* ---- 2벌식 한글 자모 조합 시뮬레이션 (macOS 방향 삭제용) ----
 *
 * macOS의 Option+Backspace(단어 삭제)는 한글 조합 텍스트의 "단어" 경계를
 * 음절 단위로 애매하게 잘라 처리해서, 대기시간을 아무리 늘려도 일부만
 * 지워지는 문제를 근본적으로 해결하지 못한다(실기로 반복 확인됨).
 *
 * 대신 2벌식 자모 조합 규칙을 그대로 흉내내서, 입력했던 로마자 키
 * 시퀀스가 실제로 몇 개의 한글 음절(화면에 보이는 글자 수)로 표시됐을지
 * 정확히 계산한 뒤, 그 개수만큼만 일반 Backspace(모디파이어 없음)를
 * 보낸다. 이미 전환(언어 스위치)이 끝나 조합이 커밋된 뒤라면, 일반
 * Backspace는 "단어"가 아니라 화면에 보이는 글자 하나를 정확히 하나씩
 * 지우는 결정적 동작이므로, 글자 수만 정확히 알면 정확히 지울 수 있다.
 */
static bool is_vowel_key(uint32_t keycode) {
    switch (keycode) {
    case ZMK_HID_USAGE_ID(Y): /* ㅛ */
    case ZMK_HID_USAGE_ID(U): /* ㅕ */
    case ZMK_HID_USAGE_ID(I): /* ㅑ */
    case ZMK_HID_USAGE_ID(O): /* ㅐ */
    case ZMK_HID_USAGE_ID(P): /* ㅔ */
    case ZMK_HID_USAGE_ID(H): /* ㅗ */
    case ZMK_HID_USAGE_ID(J): /* ㅓ */
    case ZMK_HID_USAGE_ID(K): /* ㅏ */
    case ZMK_HID_USAGE_ID(L): /* ㅣ */
    case ZMK_HID_USAGE_ID(B): /* ㅠ */
    case ZMK_HID_USAGE_ID(N): /* ㅜ */
    case ZMK_HID_USAGE_ID(M): /* ㅡ */
        return true;
    default:
        return false;
    }
}

/* prev(현재 중성)와 next(새로 들어온 모음 키)가 하나의 이중모음으로
 * 결합되는 조합인지 - 2벌식에서 유효한 조합은 ㅗ/ㅜ/ㅡ 세 가지뿐이다. */
static bool vowels_compound(uint32_t prev, uint32_t next) {
    if (prev == ZMK_HID_USAGE_ID(H)) { /* ㅗ + ㅏ/ㅐ/ㅣ = ㅘ/ㅙ/ㅚ */
        return next == ZMK_HID_USAGE_ID(K) || next == ZMK_HID_USAGE_ID(O) ||
               next == ZMK_HID_USAGE_ID(L);
    }
    if (prev == ZMK_HID_USAGE_ID(N)) { /* ㅜ + ㅓ/ㅔ/ㅣ = ㅝ/ㅞ/ㅟ */
        return next == ZMK_HID_USAGE_ID(J) || next == ZMK_HID_USAGE_ID(P) ||
               next == ZMK_HID_USAGE_ID(L);
    }
    if (prev == ZMK_HID_USAGE_ID(M)) { /* ㅡ + ㅣ = ㅢ */
        return next == ZMK_HID_USAGE_ID(L);
    }
    return false;
}

/* keys[0..len)의 로마자 키 시퀀스가 2벌식으로 조합됐을 때 화면에 보이는
 * 한글 음절(글자) 개수를 계산한다. state: 0=새 음절 시작 전, 1=초성만,
 * 2=초성+중성, 3=초성+중성+종성. */
static size_t count_hangul_syllables(const struct word_flip_key *keys, size_t len) {
    size_t syllables = 0;
    int state = 0;
    uint32_t last_jung = 0;

    for (size_t i = 0; i < len; i++) {
        uint32_t kc = keys[i].keycode;
        bool vowel = is_vowel_key(kc);
        bool next_is_vowel = (i + 1 < len) && is_vowel_key(keys[i + 1].keycode);

        switch (state) {
        case 0:
            syllables++;
            if (vowel) {
                state = 2;
                last_jung = kc;
            } else {
                state = 1;
            }
            break;
        case 1: /* 초성만 있음 */
            if (vowel) {
                state = 2;
                last_jung = kc;
            } else {
                /* 초성 단독으로 음절 확정, 새 음절 시작 */
                syllables++;
                state = 1;
            }
            break;
        case 2: /* 초성+중성 */
            if (vowel) {
                if (vowels_compound(last_jung, kc)) {
                    /* 이중모음으로 결합, 같은 음절 유지 */
                } else {
                    /* 결합 불가 - 새 음절(모음 단독 시작) */
                    syllables++;
                    last_jung = kc;
                }
            } else if (next_is_vowel) {
                /* 뒤에 모음이 오면 이 자음은 종성이 아니라 다음 음절의
                 * 초성으로 재분석됨(연음) - 새 음절 시작 */
                syllables++;
                state = 1;
            } else {
                state = 3; /* 종성으로 결합, 같은 음절 */
            }
            break;
        case 3: /* 초성+중성+종성 */
            if (vowel) {
                /* 종성이 다음 음절의 초성으로 넘어가고, 이 모음이 그
                 * 음절의 중성이 됨 - 새 음절 시작 */
                syllables++;
                state = 2;
                last_jung = kc;
            } else {
                /* 겹받침은 2벌식에서 별도 키가 없으므로 새 음절 시작 */
                syllables++;
                state = 1;
            }
            break;
        }
    }

    return syllables;
}

static int word_flip_keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE; /* release는 무시, press만 본다 */
    }

    if (is_letter(ev->usage_page, ev->keycode)) {
        if (buffer_len < WORD_FLIP_MAX_LEN) {
            buffer[buffer_len].keycode = ev->keycode;
            buffer[buffer_len].explicit_modifiers = ev->explicit_modifiers;
            buffer_len++;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* A-Z가 아닌 키(스페이스/엔터/백스페이스/화살표 등) = 단어 경계 */
    buffer_len = 0;
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(word_flip_capture, word_flip_keycode_listener);
ZMK_SUBSCRIPTION(word_flip_capture, zmk_keycode_state_changed);

static void queue_kp_ex(struct zmk_behavior_binding_event *event, uint32_t param1,
                        uint32_t post_wait_ms) {
    struct zmk_behavior_binding binding = {
        /* "KP"는 devicetree 라벨일 뿐이고, zmk_behavior_get_binding()이 찾는
         * 디바이스 이름은 노드 이름인 "key_press"다 (ZMK app/dts/behaviors/
         * key_press.dtsi의 `kp: key_press { ... }`). "KP"로 두면 조회가
         * NULL이 되어 "No behavior assigned" 경고만 남기고 아무것도 안 나간다. */
        .behavior_dev = "key_press",
        .param1 = param1,
        .param2 = 0,
    };
    zmk_behavior_queue_add(event, binding, true, WORD_FLIP_TAP_MS);
    zmk_behavior_queue_add(event, binding, false, post_wait_ms);
}

static void queue_kp(struct zmk_behavior_binding_event *event, uint32_t param1) {
    queue_kp_ex(event, param1, WORD_FLIP_WAIT_MS);
}

static int on_word_flip_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    struct word_flip_key snapshot[WORD_FLIP_MAX_LEN];
    size_t snapshot_len = buffer_len;

    if (snapshot_len == 0) {
        return ZMK_BEHAVIOR_OPAQUE; /* 되돌릴 게 없으면 아무것도 안 함 */
    }

    memcpy(snapshot, buffer, sizeof(struct word_flip_key) * snapshot_len);
    /* 아래에서 보낼 백스페이스가 이 파일의 리스너에도 잡혀서 버퍼를 지울
     * 것이므로, 재입력에 쓸 내용은 이미 snapshot에 복사해뒀으니 미리 비움 */
    buffer_len = 0;

    /* param1: 0 = Windows, 1 = macOS
     *
     * 단어 삭제:  Windows = Ctrl+Backspace,  macOS = Option+Backspace
     * 한/영 전환: Windows = LANG1(HID 0x90, 전용 한/영 키의 표준 코드)
     *             macOS   = Caps Lock
     *
     * macOS는 LANG1을 한/영 전환으로 처리하지 않는다 - 그 코드는 일본어 JIS
     * 가나 키(kVK_JIS_Kana)로 해석된다. Apple이 공식적으로 안내하는 전환
     * 방법은 Control+Space / Control+Option+Space / Caps Lock / Fn(지구본)
     * 뿐이고, 이 키보드 사용자는 Caps Lock 방식을 쓰므로 그쪽에 맞춘다.
     * (시스템 설정 > 키보드 > 입력 소스에서 "Caps Lock 키로 ABC 입력 소스
     * 전환"이 켜져 있어야 한다. 짧게 누르면 입력 소스 전환, 길게 누르면
     * 실제 Caps Lock이므로 여기서 보내는 40ms 탭은 전환으로 동작한다.) */
    bool is_mac = binding->param1 == 1;
    uint32_t delete_word = LC(BSPC); /* Windows 전용: Ctrl+Backspace 단어 삭제 */
    uint32_t lang_toggle = is_mac ? CLCK : LANG1;

    /* 순서: 전환을 먼저 보낸다 - 조합 중에 백스페이스를 먼저 보내면(이전
     * 시도) 캡스락 전환 단축키 자체가 씹히는 것이 실기로 확인됨(삭제 후
     * 같은 언어로 그대로 재입력되는 증상). */
    queue_kp_ex(&event, lang_toggle, is_mac ? WORD_FLIP_MAC_TOGGLE_WAIT_MS : WORD_FLIP_WAIT_MS);

    if (is_mac) {
        /* 이 전환 직전에 화면에 있던 텍스트가 한글이었는지 영어였는지에
         * 따라 지워야 할 개수가 다르다 - 한글이면 음절 개수(글자 수 <
         * 키 입력 수), 영어면 그냥 키 입력 수만큼(1글자 = 1키)이다.
         * 무조건 한글로 가정하면(이전 버전의 버그) 영→한 방향에서 너무
         * 적게 지워 앞부분이 그대로 남는 문제가 생긴다. */
        size_t delete_count = mac_is_korean ? count_hangul_syllables(snapshot, snapshot_len)
                                             : snapshot_len;
        for (size_t i = 0; i < delete_count; i++) {
            queue_kp(&event, BSPC);
        }
        mac_is_korean = !mac_is_korean; /* 방금 전환을 보냈으니 상태 뒤집기 */
    } else {
        queue_kp(&event, delete_word);
    }

    for (size_t i = 0; i < snapshot_len; i++) {
        uint32_t param1 = ((uint32_t)snapshot[i].explicit_modifiers << 24) | snapshot[i].keycode;
        queue_kp(&event, param1);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_word_flip_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api word_flip_driver_api = {
    .binding_pressed = on_word_flip_binding_pressed,
    .binding_released = on_word_flip_binding_released,
};

static int word_flip_init(const struct device *dev) {
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, word_flip_init, NULL, NULL, NULL, POST_KERNEL,
                         CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &word_flip_driver_api);

#endif /* central-only && DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
