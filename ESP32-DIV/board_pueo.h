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
 * Physical map (see docs/pueo/hardware.md for the reasoning):
 *
 *   VSPI (shared bus)   SCK 18   MOSI 23   MISO 19
 *     SD (onboard)      CS  5
 *     CC1101            CS  21 *    GDO0 22 (TX)   GDO2 35 (RX)
 *                       * 27 on the 2.8" panel; see CC1101_CS below
 *     NRF24L01+PA+LNA   CSN 4 *     CE   16        IRQ unconnected
 *                       * 25 on the 3.5" panel; see CSN_PIN_1 below
 *     PN532 V3 (SPI)    SS  17
 *   GPS ATGM336H        ESP32 RX on GPIO 1, GPS RX not connected
 *
 * GPIO 4/16/17 are the CYD's onboard RGB LED. Using them means the LED is gone.
 * That is intended — they are the only pins left.
 * ──────────────────────────────────────────────────────────────────────────── */

/* Which CYD panel. shared.h guards its own definition with #ifndef, so
 * setting it here wins -- and it has to be here, because this overlay is
 * included before shared.h evaluates its defaults, and CC1101_CS below
 * depends on it.
 *
 *   0  2.8" ESP32-2432S028R   ILI9341  240x320   backlight GPIO 21
 *   1  3.5" ESP32-3248S035R   ST7796   320x480   backlight GPIO 27
 */
#ifndef PUEO_PANEL_35
#define PUEO_PANEL_35 1
#endif

#define BOARD_CYD

#ifndef ESP32DIV_BOARD_NAME
#if PUEO_PANEL_35
#define ESP32DIV_BOARD_NAME "Pueo (CYD 3.5)"
#else
#define ESP32DIV_BOARD_NAME "Pueo (CYD 2.8)"
#endif
#endif

/* ── CC1101 SubGHz ──────────────────────────────────────────────────────────
 * Matches the stock CYD defaults; pinned explicitly so a future upstream
 * change to the CYD block cannot silently move our wiring.
 *
 * GDO0 is the TX data line (ESP32 -> radio), GDO2 is RX (radio -> ESP32).
 * ELECHOUSE setGDO(gdo0, gdo2) is called as setGDO(TX, RX) in subghz.cpp,
 * which is correct. GPIO 35 is input-only, which suits GDO2 and makes the
 * assignment impossible to get backwards. */
/* CS is 21 rather than the stock CYD's 27, and that is panel-driven.
 * On the 3.5" ESP32-3248S035R the backlight is on GPIO 27, so the stock
 * assignment fights it: driving the chip select would dim the screen.
 * The two boards swap the pair -- 21 is the backlight on the 2.8" and 27
 * on the 3.5" -- so 21 is free here and 27 is not.
 *
 * It also lands CS on the Expand IO header (P3: GND, IO35, IO22, IO21)
 * alongside GDO0 and GDO2, which puts all three CC1101 control lines on a
 * connector instead of a solder pad.
 *
 * [VERIFY] on the 2.8" board 21 IS the backlight, so this has to move back
 * to 27 there. It is the one pin the two panels cannot share.
 */
#if PUEO_PANEL_35
#define CC1101_CS     21   /* 27 is the backlight on this panel */
#else
#define CC1101_CS     27
#endif
#define SUBGHZ_TX_PIN 22   /* -> CC1101 GDO0 */
#define SUBGHZ_RX_PIN 35   /* <- CC1101 GDO2, input-only pin */

/* ── Backlight ──────────────────────────────────────────────────────────────
 * The other half of the swap above, which this header argued for and then
 * never carried out. shared.h defaults BOARD_CYD to GPIO 21, that default
 * was left standing, and on the 3.5" build CC1101_CS and BACKLIGHT_PIN were
 * both 21 -- the exact conflict the comment above exists to avoid, on the
 * pin it moved CS onto.
 *
 * It was invisible because neither half failed loudly. Nothing is soldered
 * to CC1101 yet, so the chip select never toggles; and the backlight on
 * this board sits on a pull-up and comes on by itself, so PWM into the
 * wrong pin looks like a screen that works. What it actually cost was the
 * Brightness setting, which has never done anything on this panel: it was
 * driving a pad with nothing on the end of it.
 *
 * Confirmed on the board -- moving the slider changed nothing before this,
 * which is what says 21 is not the backlight here.
 */
#if PUEO_PANEL_35
#define BACKLIGHT_PIN 27
#endif

/* ── NRF24L01+PA+LNA ────────────────────────────────────────────────────────
 * One module, not three. Stock CYD defaults put CSN_PIN_1 on 17 (we need that
 * for the PN532), CSN_PIN_2 on 27 (collides with CC1101 CS) and CSN_PIN_3 on
 * 25 (collides with the XPT2046 touch clock — a real bug on the stock CYD
 * profile, not just a Pueo problem).
 *
 * CSN follows the panel, because the two boards do not agree about GPIO 4.
 *
 *   2.8" ESP32-2432S028R   GPIO 4 is the RGB LED's red channel
 *   3.5" ESP32-3248S035R   GPIO 4 is the audio amplifier's enable
 *                          (lcdwiki E32R35T; RGB red moves to 22)
 *
 * Spending the LED is the trade this board map already makes. Keying an
 * amplifier enable at chip-select rates is not the same trade: it clicks,
 * and it draws current off a rail already carrying the display.
 *
 * 25 is free on the 3.5" for the reason the paragraph above says it is not
 * free on the 2.8" — that panel puts touch on its own bus at 25/32/39,
 * while the 3.5" hangs its XPT2046 off the display's SPI behind TOUCH_CS.
 * The pin that collides on one board is the spare on the other.
 *
 * Not changed on the 2.8": anyone who followed the build guide has CSN
 * soldered to the GPIO 4 pad, and a published pin map is a thing people
 * have already acted on.
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
#if PUEO_PANEL_35
#define CSN_PIN_1 25
#else
#define CSN_PIN_1 4
#endif
#define CE_PIN_2  CE_PIN_1
#define CSN_PIN_2 CSN_PIN_1
#define CE_PIN_3  CE_PIN_1
#define CSN_PIN_3 CSN_PIN_1

#define PUEO_NRF24_MODULE_COUNT 1

/* ── sound ──────────────────────────────────────────────────────────────────
 * The 3.5" lcdwiki board carries an audio amplifier: GPIO 26 is its input
 * and GPIO 4 is its enable. Neither is claimed here -- moving NRF24's chip
 * select to 25 on this panel was partly to keep them, because keying an
 * amplifier enable at chip-select rates clicks and draws off the display's
 * rail.
 *
 * The 2.8" has neither. GPIO 4 is that board's RGB red and is this pin map's
 * NRF24 CSN, so driving it as an amplifier enable would key a radio. Sound
 * is 3.5"-only and the guards below say so rather than relying on nobody
 * calling it.
 *
 * The enable is ACTIVE LOW. lcdwiki's page for this board is explicit --
 * "Audio enable signal, low level enable, high level disable" -- and the
 * first version of this drove it high to enable, which is the one state
 * that guarantees silence. Worth the emphasis: the obvious polarity was
 * the wrong one.
 *
 * No speaker is fitted. The board brings the amplifier out to a 1.25 mm
 * 2-pin connector and you attach your own, so a board straight out of the
 * bag is silent however correct this code is. */
#if PUEO_PANEL_35
#define BUZZER_PIN       26   /* amplifier input (also DAC2) */
#define AMP_ENABLE_PIN    4   /* amplifier enable */
#define AMP_ENABLE_LEVEL  LOW /* active low -- see above */
#endif

/* ── PN532 V3 (SPI mode: DIP CH1=OFF, CH2=ON) ───────────────────────────────
 * Stock CYD default is SS 25, which is the XPT2046 touch clock. Moved to 17. */
#define PN532_SS 17

/* ── GPS ATGM336H ──────────────────────────────────────────────────────────────
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
