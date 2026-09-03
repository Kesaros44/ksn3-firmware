# ksn3-firmware

ZMK firmware for the KSN-3 split keyboard — a new board built by reverse-engineering the KSN-3 EasyEDA schematic and porting the driver architecture and lessons from [ksn1-firmware](https://github.com/Kesaros44/ksn1-firmware).

* Keyboard Maintainer: [AJG](https://github.com/Kesaros44)
* Hardware Supported: KSN-3 split keyboard, nice!nano v2 (nRF52840), BLE

![KSN-3 렌더링 - 상면도](images/ksn3_render_top.png)
![KSN-3 렌더링 - 입체도](images/ksn3_render_iso.png)

## ⚠️ Read this before flashing

**Not yet tested on physical hardware.** GPIO pins, the encoder, LED wiring, and which half is central all come straight from the schematic and are double-checked — high confidence. The keymap's specific key assignments (`config/ksn_3.keymap`), on the other hand, are a first-draft guess from switch position/shape, not a labelled keycap layout — check them against the real board and re-flash as needed. No hardware risk either way; a wrong keycode is just an inconvenience.

If the status LED looks inverted after flashing, flip `GPIO_ACTIVE_HIGH` → `GPIO_ACTIVE_LOW` on `status_led` in `config/boards/shields/ksn_3/ksn_3_left.overlay` (KSN-1 needed this same fix).

## Hardware

- **MCU:** nice!nano v2 (nRF52840) per half, wireless (BLE)
- **Central:** right half (no `POS_STATE_LEN` overflow risk — same 6×22 matrix size as KSN-1's corrected layout)
- **Matrix:** 6 rows × 22 columns combined (11 per half), `col2row`
- **Encoder:** one EC11 encoder on the right half (central) only; its pushbutton is wired into the matrix itself, not a separate GPIO
- **LEDs (left/peripheral only):** Caps Lock LED, BLE connection-status LED
- **Backlight:** designed into the schematic (MOSFET switching circuit on both halves), but parts aren't populated and the feature is disabled in firmware, same as KSN-1/KSN-2 — no backlight on the physical board
- **RGB underglow:** none
- **Battery:** reported from both halves

## Keymap (`config/ksn_3.keymap`)

Three layers, structurally mirroring KSN-1 (see the hardware warning above — positions are schematic-verified, key assignments are a draft):

- **`default_layer`** — Windows base layer with an integrated numpad on the left. Encoder = volume. Numpad corner key runs a macro (Win+R → `calc` → Enter).
- **`func_layer`** (hold `&mo 1`) — Bluetooth profile select (0–4) and clear, output toggle, backlight inc/dec on the encoder (no-op — no backlight hardware), toggle (`&tog 2`) into `mac_layer`.
- **`mac_layer`** — same layout with Mac modifier order and Mac media/brightness keys; numpad corner key runs Cmd+Space → Spotlight calculator instead.

## Building

GitHub Actions builds on every push — grab the `.uf2` files (`ksn_3_left`, `ksn_3_right`, `settings_reset`) from the workflow run's artifacts.

Local build with `west`:

```sh
west init -l config
west update
west build -p -b nice_nano_v2 -- -DSHIELD=ksn_3_left -DZMK_EXTRA_MODULES=$(pwd)/config
west build -p -b nice_nano_v2 -- -DSHIELD=ksn_3_right -DZMK_EXTRA_MODULES=$(pwd)/config
```

## Flashing

Double-tap reset on the nice!nano to enter the UF2 bootloader, then drag the matching `.uf2` onto the `NICENANO` drive (left firmware → left half, right → right half). Flash both halves — they run different images.

## Re-pairing / clearing Bluetooth bonds

Flash the `settings_reset` artifact to a half to wipe its BLE bonds, then reflash normal firmware and re-pair.

## Schematic source

`config/` was derived from `SCH_KSN3L_20260831.json` / `SCH_KSN3R_20260831.json` (EasyEDA exports). If the physical board is revised, check the GPIO pin comments in `ksn_3.dtsi` and the two overlays against the new schematic's net labels.

---

# ksn3-firmware (한국어)

KSN-3 스플릿 키보드용 ZMK 펌웨어 설정입니다 — KSN-3 EasyEDA 회로도를 리버스 엔지니어링하고 [ksn1-firmware](https://github.com/Kesaros44/ksn1-firmware)의 드라이버 구조와 교훈을 이식해서 새로 만든 보드입니다.

* 키보드 관리자: [AJG](https://github.com/Kesaros44)
* 지원 하드웨어: KSN-3 스플릿 키보드, nice!nano v2 (nRF52840), BLE

## ⚠️ 플래싱 전에 꼭 읽어주세요

**아직 실제 하드웨어에서 테스트되지 않았습니다.** GPIO 핀, 인코더, LED 배선, 어느 half가 central인지는 모두 회로도에서 직접 나왔고 재검증까지 마쳐서 신뢰도가 높습니다. 반면 키맵의 구체적인 키 배정(`config/ksn_3.keymap`)은 라벨이 붙은 키캡 배열이 아니라 스위치 위치/모양으로 추론한 초안입니다 — 실물 보드와 대조해보고 필요하면 다시 플래시하세요. 어느 쪽이든 하드웨어 위험은 없습니다. 잘못된 키코드는 그냥 불편함일 뿐입니다.

플래시 후 상태 LED가 반대로 동작하면, `config/boards/shields/ksn_3/ksn_3_left.overlay`의 `status_led`에서 `GPIO_ACTIVE_HIGH`를 `GPIO_ACTIVE_LOW`로 바꿔보세요(KSN-1도 동일한 수정이 필요했습니다).

## 하드웨어

- **MCU:** half마다 nice!nano v2 (nRF52840), 무선(BLE)
- **Central:** 오른쪽 half (KSN-1의 수정된 배치와 매트릭스 크기(6×22)가 같아서 `POS_STATE_LEN` 오버플로우 위험 없음)
- **매트릭스:** 6행 × 22열 (half당 11열), `col2row`
- **인코더:** EC11 인코더 1개, 오른쪽(central) half에만 있음; 누름 버튼은 별도 GPIO가 아니라 매트릭스 자체에 배선됨
- **LED (왼쪽/peripheral 전용):** Caps Lock LED, BLE 연결상태 LED
- **백라이트:** 회로도에는 설계되어 있지만(양쪽 half 모두 MOSFET 스위칭 회로) 부품이 실장되지 않았고, KSN-1/KSN-2와 마찬가지로 펌웨어 기능도 꺼져 있음 — 실제 보드에는 백라이트 없음
- **RGB 언더글로우:** 없음
- **배터리:** 양쪽 half 모두 보고

## 키맵 (`config/ksn_3.keymap`)

KSN-1과 구조적으로 동일한 3개 레이어(위 경고 참고 — 위치는 회로도로 검증됐고, 키 배정은 초안):

- **`default_layer`** — 왼쪽에 넘버패드가 통합된 Windows 기본 레이어. 인코더 = 볼륨. 넘버패드 코너 키가 매크로(Win+R → `calc` → Enter)로 계산기 실행.
- **`func_layer`** (홀드 `&mo 1`) — 블루투스 프로필 선택(0–4) 및 clear, 출력 토글, 인코더로 백라이트 증감(백라이트 하드웨어 자체가 없어서 실제 동작은 없음), `mac_layer`로의 토글(`&tog 2`).
- **`mac_layer`** — 동일한 배열에 Mac 모디파이어 순서와 Mac 미디어/밝기 키; 넘버패드 코너 키는 대신 Cmd+Space → Spotlight 계산기 실행.

## 빌드

GitHub Actions가 push마다 자동으로 빌드합니다 — 워크플로우 실행의 아티팩트에서 `.uf2` 파일(`ksn_3_left`, `ksn_3_right`, `settings_reset`)을 받으면 됩니다.

`west`로 로컬 빌드:

```sh
west init -l config
west update
west build -p -b nice_nano_v2 -- -DSHIELD=ksn_3_left -DZMK_EXTRA_MODULES=$(pwd)/config
west build -p -b nice_nano_v2 -- -DSHIELD=ksn_3_right -DZMK_EXTRA_MODULES=$(pwd)/config
```

## 플래싱

nice!nano의 리셋 버튼을 더블탭해서 UF2 부트로더로 진입한 뒤, 마운트된 `NICENANO` 드라이브에 해당하는 `.uf2`(왼쪽 → 왼쪽 half, 오른쪽 → 오른쪽 half)를 드래그하면 됩니다. 양쪽 half 모두 플래시해야 합니다 — 서로 다른 이미지를 사용합니다.

## 재페어링 / 블루투스 본딩 초기화

`settings_reset` artifact를 해당 half에 플래시하면 BLE 본딩이 초기화됩니다. 그 다음 정상 펌웨어를 다시 플래시하고 재페어링하세요.

## 회로도 출처

`config/`는 `SCH_KSN3L_20260831.json` / `SCH_KSN3R_20260831.json`(EasyEDA 내보내기)에서 도출했습니다. 실물 보드가 리비전되면, `ksn_3.dtsi`와 두 overlay 파일의 GPIO 핀 주석을 새 회로도의 net label과 대조해보세요.
