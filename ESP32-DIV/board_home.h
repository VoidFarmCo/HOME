#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * H.O.M.E — the OWNER'S actual board.
 *
 * This is NOT Pueo's board. The device is a 3.5" "ESP32-32E N4" LCD board
 * (320x480 resistive touch) with JST BREAKOUT HEADERS -- verified from a photo
 * 2026-10-06. `board_pueo.h` describes magikh0e's Sunton 3248S035R and its pins
 * are WRONG here; do not use it. The display/touch/SD path (BOARD_CYD +
 * `Libraries/User_Setup cyd.h`: TFT 14/13/12/15, DC 2, TOUCH_CS 33, SD CS 5)
 * works and is kept.
 *
 * Silkscreened headers on this board (what we wire to -- no castellation solder):
 *   I2C  : 3V3 · IO32(SDA) · IO25(SCL) · GND
 *   SPI  : IO23(MOSI) · IO19(MISO) · IO18(SCK) · IO21(CS)
 *   UART : RXD · TXD · GND · 5V            (TXD=IO1, RXD=IO3)
 *   2-pin: IO35 · IO39                     (both input-only)
 *   + SPEAKER, BAT, microSD, RGB LED, RESET, BOOT, micro-USB.
 *
 * NOTHING is wired yet: this file SETS the pinout, then the owner solders to it.
 * This is the LEAN direct-pin build (CC1101 + nRF24 + GPS), which works with the
 * stock radio libraries. Adding LoRa uses the MCP23017 expander (owner has two)
 * to carry the chip-selects -- a separate unit, see docs/home/board.md.
 * ──────────────────────────────────────────────────────────────────────────── */

/* Display/touch/SD/UI follow the stock CYD path (it works on this panel). */
#define BOARD_CYD

#ifndef ESP32DIV_BOARD_NAME
#define ESP32DIV_BOARD_NAME "H.O.M.E (ESP32-32E 3.5)"
#endif

/* Backlight: kept at 27 (the current working value; the panel is lit). */
#define BACKLIGHT_PIN 27

/* Speaker on the SPEAKER header, GPIO 26 (DAC2), as on the stock CYD. */
#define BUZZER_PIN 26

/* ── MCP23017 I2C GPIO expander ───────────────────────────────────────────────
 * On the I2C header (SDA 32, SCL 25). Carries the SLOW radio control lines
 * (chip-selects, nRF CE) because the board breaks out only three direct output
 * pins -- not enough for CC1101 + nRF24 (+ LoRa) together. Pins with value
 * >= Mcp23017::PIN_BASE (100) are expander CHANNELS (pin - 100); below are GPIOs.
 * setup() calls Mcp23017::begin() so the chip is up before any radio inits. */
#define PUEO_HAS_MCP23017 1
#define MCP23017_SDA  32
#define MCP23017_SCL  25
#define MCP23017_ADDR 0x20

/* ── CC1101 SubGHz 433 ───────────────────────────────────────────────────────
 * Bus on the SPI header (18/23/19). CS lives on the expander (ch0). GDO0 is a
 * DIRECT GPIO (IO21) because raw TX bit-bangs it -- too fast for the expander.
 * GDO2 is a direct input (IO35). */
#define CC1101_CS     100  /* MCP23017 ch0 (= PIN_BASE + 0) */
#define SUBGHZ_TX_PIN 21   /* -> CC1101 GDO0, DIRECT (raw-TX, fast); SPI header "CS" pin */
#define SUBGHZ_RX_PIN 35   /* <- CC1101 GDO2, DIRECT input */

/* ── nRF24L01+PA/LNA 2.4 GHz ─────────────────────────────────────────────────
 * Bus shared on the SPI header; CSN + CE on the expander (ch1, ch2). */
#define CSN_PIN_1 101      /* MCP23017 ch1 */
#define CE_PIN_1  102      /* MCP23017 ch2 */
#define CSN_PIN_2 CSN_PIN_1
#define CE_PIN_2  CE_PIN_1
#define CSN_PIN_3 CSN_PIN_1
#define CE_PIN_3  CE_PIN_1
#define PUEO_NRF24_MODULE_COUNT 1

/* No PN532/RFID on this board (RFID is being dropped). -1 = unassigned. */
#define PN532_SS -1

/* ── LoRa (Core1262 / SX1262) ─────────────────────────────────────────────────
 * Shares the SPI header bus (18/23/19). CS + RESET on the MCP23017 (slow, fine
 * on the expander); BUSY on a DIRECT input (IO39) because the host must poll it
 * before every command; DIO1 IRQ is polled over SPI (GetIrqStatus), no pin. */
#define LORA_CS    103   /* MCP23017 ch3 */
#define LORA_RESET 104   /* MCP23017 ch4 */
#define LORA_BUSY  39    /* direct input (poll before each command) */

/* ── GPS ATGM336H / NEO-7M ───────────────────────────────────────────────────
 * On the UART header. GPS TX wires to the header's **TXD** pin (IO1), NOT RXD:
 * IO3/RXD is driven push-pull by the USB-UART chip and cannot be shared, while
 * IO1/TXD is an ESP output that goes high-Z once UART0 is released, so the GPS
 * can drive it (gpsUart0Release() in gps.cpp). USB serial logging is off while
 * GPS is active, by design. GPS VCC = 3.3 V (ATGM336H abs-max 3.6 V), not 5 V. */
#define GPS_UART_RX  1
#define GPS_UART_TX  -1
#define GPS_UART_NUM 2
#define PUEO_GPS_STEALS_UART0 1
