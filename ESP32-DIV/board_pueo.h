#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Pueo — custom handheld, CYD ESP32-2432S028R base.
 *
 * This is an OVERLAY, not a new board branch. Every pin macro in shared.h is
 * wrapped in #ifndef, and BoardConfig.h is included before those defaults are
 * evaluated, so defining a pin here wins without touching shared.h. That keeps
 * `git merge upstream/main` to a one-line conflict at worst.
 *
 * Display, touch, SD and UI behaviour follow the stock BOARD_CYD path, which
 * this header turns on. Only the external radio wiring differs.
 *
 * Physical map (see docs/hardware.md for the reasoning):
 *
 *   VSPI (shared bus)   SCK 18   MOSI 23   MISO 19
 *     SD (onboard)      CS  5
 *     CC1101            CS  27      GDO0 22 (TX)   GDO2 35 (RX)
 *     NRF24L01+PA+LNA   CSN 4       CE   16        IRQ unconnected
 *     PN532 V3 (SPI)    SS  17
 *   GPS GT-U7           ESP32 RX on GPIO 1, GPS RX not connected
 *
 * GPIO 4/16/17 are the CYD's onboard RGB LED. Using them means the LED is gone.
 * That is intended — they are the only pins left.
 * ──────────────────────────────────────────────────────────────────────────── */

#define BOARD_CYD

#ifndef ESP32DIV_BOARD_NAME
#define ESP32DIV_BOARD_NAME "Pueo (CYD 2.8)"
#endif

/* ── CC1101 SubGHz ──────────────────────────────────────────────────────────
 * Matches the stock CYD defaults; pinned explicitly so a future upstream
 * change to the CYD block cannot silently move our wiring.
 *
 * GDO0 is the TX data line (ESP32 -> radio), GDO2 is RX (radio -> ESP32).
 * ELECHOUSE setGDO(gdo0, gdo2) is called as setGDO(TX, RX) in subghz.cpp,
 * which is correct. GPIO 35 is input-only, which suits GDO2 and makes the
 * assignment impossible to get backwards. */
#define CC1101_CS     27
#define SUBGHZ_TX_PIN 22   /* -> CC1101 GDO0 */
#define SUBGHZ_RX_PIN 35   /* <- CC1101 GDO2, input-only pin */

/* ── NRF24L01+PA+LNA ────────────────────────────────────────────────────────
 * One module, not three. Stock CYD defaults put CSN_PIN_1 on 17 (we need that
 * for the PN532), CSN_PIN_2 on 27 (collides with CC1101 CS) and CSN_PIN_3 on
 * 25 (collides with the XPT2046 touch clock — a real bug on the stock CYD
 * profile, not just a Pueo problem).
 *
 * radio2/radio3 in bluetooth.cpp are aliased onto the same physical module.
 * The three-radio BLE jammer modes therefore run degraded on one radio; they
 * are out of scope for the initial feature set either way.
 *
 * IRQ is left unconnected. Upstream never reads it — there is no IRQ pin macro
 * anywhere in the tree and no whatHappened()/maskIRQ() call — so the GPIO 17
 * double-assignment in the handoff resolves to "PN532 takes 17, NRF24 IRQ
 * stays off the board". */
#define CE_PIN_1  16
#define CSN_PIN_1 4
#define CE_PIN_2  CE_PIN_1
#define CSN_PIN_2 CSN_PIN_1
#define CE_PIN_3  CE_PIN_1
#define CSN_PIN_3 CSN_PIN_1

#define PUEO_NRF24_MODULE_COUNT 1

/* ── PN532 V3 (SPI mode: DIP CH1=OFF, CH2=ON) ───────────────────────────────
 * Stock CYD default is SS 25, which is the XPT2046 touch clock. Moved to 17. */
#define PN532_SS 17

/* ── GPS GT-U7 ──────────────────────────────────────────────────────────────
 * The GPS TX line lands on GPIO 1, the ESP32's UART0 TX. That looks wrong and
 * is deliberate: GPIO 3 (UART0 RX) is driven by the USB-UART bridge's TX
 * output, so tying the GPS output there puts two push-pull drivers on one net.
 * GPIO 1 is an ESP32 output feeding the bridge's RX input, which is high-Z, so
 * the GPS can drive it once the ESP32 stops.
 *
 * "Once the ESP32 stops" is the catch: UART0 must be released before UART2 can
 * take GPIO 1 as an input. See gpsUart0Release() in gps.cpp. USB serial
 * logging is dead while GPS is active, by design.
 *
 * GPS_UART_TX = -1 leaves UART2's TX unattached. It must not be left at the
 * core default (GPIO 17), which is the PN532 select line. */
#define GPS_UART_RX  1
#define GPS_UART_TX  -1
#define GPS_UART_NUM 2

/* Set when GPS_UART_RX sits on a UART0 pin and the console has to be torn
 * down first. Checked in gps.cpp. */
#define PUEO_GPS_STEALS_UART0 1
