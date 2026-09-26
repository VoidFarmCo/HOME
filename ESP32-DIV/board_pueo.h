#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Pueo — custom handheld, CYD ESP32-3248S035R base.
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
 *     CC1101            CS  21      GDO0 22 (TX)   GDO2 35 (RX)
 *     NRF24L01+PA+LNA   CSN 25      CE   16        IRQ unconnected
 *     PN532 V3 (SPI)    SS  17
 *   GPS ATGM336H        ESP32 RX on GPIO 1, GPS RX not connected
 *
 * GPIO 4/16/17 are the CYD's onboard RGB LED. Using them means the LED is gone.
 * That is intended — they are the only pins left.
 * ──────────────────────────────────────────────────────────────────────────── */

/* One panel: the 3.5" ESP32-3248S035R, ST7796, 320x480, backlight GPIO 27.
 *
 * This used to branch on PUEO_PANEL_35 and carry a second set of values for
 * the 2.8" ESP32-2432S028R. That panel was built every release and never
 * booted, so what the branch actually held was an untested claim in the one
 * file where a wrong pin is a shorted output. It is gone, and so is every
 * "on the other panel" footnote that existed to serve it. */

#define BOARD_CYD

#ifndef ESP32DIV_BOARD_NAME
#define ESP32DIV_BOARD_NAME "Pueo (CYD 3.5)"
#endif

/* ── CC1101 SubGHz ──────────────────────────────────────────────────────────
 * Matches the stock CYD defaults; pinned explicitly so a future upstream
 * change to the CYD block cannot silently move our wiring.
 *
 * GDO0 is the TX data line (ESP32 -> radio), GDO2 is RX (radio -> ESP32).
 * ELECHOUSE setGDO(gdo0, gdo2) is called as setGDO(TX, RX) in subghz.cpp,
 * which is correct. GPIO 35 is input-only, which suits GDO2 and makes the
 * assignment impossible to get backwards. */
/* CS is 21 rather than the stock CYD's 27, because on this panel the
 * backlight is on 27 and the stock assignment fights it: driving the chip
 * select would dim the screen. 21 is free here for the same reason.
 *
 * It also lands CS on the Expand IO header (P3: GND, IO35, IO22, IO21)
 * alongside GDO0 and GDO2, which puts all three CC1101 control lines on a
 * connector instead of a solder pad.
 */
#define CC1101_CS     21   /* 27 is the backlight on this panel */
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
#define BACKLIGHT_PIN 27

/* ── NRF24L01+PA+LNA ────────────────────────────────────────────────────────
 * One module, not three. Stock CYD defaults put CSN_PIN_1 on 17 (we need that
 * for the PN532), CSN_PIN_2 on 27 (collides with CC1101 CS) and CSN_PIN_3 on
 * 25 (collides with the XPT2046 touch clock — a real bug on the stock CYD
 * profile, not just a Pueo problem).
 *
 * CSN is 25, and it is 25 rather than GPIO 4 for a reason that turned out to
 * be wrong. The belief was that 4 keyed an audio amplifier's enable on this
 * panel. Measured 2026-09-23 by driving each candidate low and watching which
 * colour came up: GPIO 4 is the RGB LED's red channel here, exactly as it is
 * on Sunton's other board. The amplifier belongs to lcdwiki's E32R35T, which
 * is a different board whose datasheet this tree followed for months.
 *
 * It stays on 25 anyway. 25 is free on this panel, the assignment is
 * published and built against, and what would be gained by moving it back is
 * one pin. One fewer pin taken on a datasheet's word is worth more than that.
 *
 * 25 is free here because this panel hangs its XPT2046 off the display's SPI
 * behind TOUCH_CS rather than giving touch its own bus at 25/32/39.
 *
 * [WARNING] free on the 3248S035**R**, which is the resistive-touch part.
 * The 3248S035**C** is the same board with a GT911 capacitive controller
 * on I2C, and GPIO 25 is one of the pins that goes to it. This image on a
 * -C board would drive a chip select into the touch controller. The two
 * are told apart by whether touch works at all: an XPT2046 behind
 * TOUCH_CS, which this build drives, is silent on a -C board.
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
#define CSN_PIN_1 25
#define CE_PIN_2  CE_PIN_1
#define CSN_PIN_2 CSN_PIN_1
#define CE_PIN_3  CE_PIN_1
#define CSN_PIN_3 CSN_PIN_1

#define PUEO_NRF24_MODULE_COUNT 1

/* ── sound ──────────────────────────────────────────────────────────────────
 * The board brings a speaker out to a 2-pin connector, driven from GPIO 26,
 * which is DAC2.
 *
 * There is no enable pin on this board, and there was never meant to be one
 * here. The whole amplifier-enable story came off lcdwiki's page for their
 * E32R35T -- "Audio enable signal, low level enable, high level disable" --
 * including a note about how the obvious polarity was the wrong one. The
 * reference board is Sunton's, and GPIO 4 on it is the RGB LED's red
 * channel, measured on 2026-09-23 by driving each candidate low and looking.
 *
 * So what AMP_ENABLE_PIN actually did on this hardware was flash the red LED
 * for the length of every beep. Asserting an enable that does not exist is
 * harmless; it is still an output driven on a pad whose real function the
 * tree had wrong, and the LED blinking on every beep was the tell nobody
 * read as one.
 *
 * BUZZER_PIN stays on 26. That is DAC2, and it is where Sunton's 2.8" drives
 * its speaker -- the pattern this board has now been shown to follow. It has
 * a 2-pin SPEAK connector and you attach your own speaker, so a board out of
 * the bag is silent however correct this is. [verify] nothing has metered
 * what is between GPIO 26 and that connector here. */
#define BUZZER_PIN       26   /* speaker drive, also DAC2 */

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
