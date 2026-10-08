#include "hud_core.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hud_pages.h"
#include "hud_scan.h"
#include <Preferences.h>

// NM-CYD-C5: ST7789 on the shared SPI bus, driven via esp_lcd (IDF native).
// The Arduino core SPIClass would not apply a >~10 MHz clock on the C5 and
// Arduino_GFX's own register-level SPI didn't drive this panel; esp_lcd uses
// the IDF spi_master driver (correct C5 clock + DMA).
#define LCD_SCLK 6
#define LCD_MOSI 7
#define LCD_MISO 2    // bus MISO: the display ignores it (write-only), the touch chip needs it
#define LCD_DC   24
#define LCD_CS   23
#define LCD_RST  -1
#define LCD_BL   25
#define LCD_HOST SPI2_HOST
#define LCD_PCLK (40 * 1000 * 1000)   // 40 MHz; esp_lcd sets the C5 divider correctly

static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static bool s_dispOk = false;
static SemaphoreHandle_t s_flush_done = NULL;

// Fires (in ISR) when a draw_bitmap DMA transfer finishes.
static bool IRAM_ATTR on_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(s_flush_done, &hp);
  return hp == pdTRUE;
}

// Strong override of the weak default in hud_core.cpp: push the whole buffer
// over SPI DMA, then WAIT for the transfer to finish before the caller reuses
// the framebuffer -- otherwise the next frame overwrites it mid-DMA and tears.
volatile uint32_t g_push_us = 0;
void hud_present_fb(const uint16_t* fb, int w, int h) {
  uint32_t t0 = micros();
  esp_lcd_panel_draw_bitmap(s_panel, 0, 0, w, h, (void*)fb);
  xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100));
  g_push_us = micros() - t0;
}

static bool lcd_init() {
  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = LCD_SCLK;
  buscfg.mosi_io_num = LCD_MOSI;
  buscfg.miso_io_num = LCD_MISO;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = HUD_W * HUD_H * 2 + 16;
  if (spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = LCD_DC;
  io_config.cs_gpio_num = LCD_CS;
  io_config.pclk_hz = LCD_PCLK;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = 0;
  io_config.trans_queue_depth = 10;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io) != ESP_OK) return false;

  s_flush_done = xSemaphoreCreateBinary();
  esp_lcd_panel_io_callbacks_t cbs = {};
  cbs.on_color_trans_done = on_trans_done;
  esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL);

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = LCD_RST;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  if (esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel) != ESP_OK) return false;

  esp_lcd_panel_reset(s_panel);
  esp_lcd_panel_init(s_panel);
  esp_lcd_panel_invert_color(s_panel, false);   // this panel wants inversion OFF (matches vendor)
  esp_lcd_panel_swap_xy(s_panel, false);         // portrait 240x320 (ST7789 native, no axis swap)
  esp_lcd_panel_mirror(s_panel, false, false);   // provisional -- adjust after eyeballing orientation
  esp_lcd_panel_disp_on_off(s_panel, true);
  return true;
}

// ---- XPT2046 resistive touch (shares SPI2 with the display; esp_lcd owns the
// bus, so the touch chip is a second spi_master device on it). ----
#define TOUCH_CS 1
static spi_device_handle_t s_touch = NULL;

static void touch_init() {
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = 2 * 1000 * 1000;   // XPT2046 is slow
  dev.mode = 0;
  dev.spics_io_num = TOUCH_CS;
  dev.queue_size = 1;
  spi_bus_add_device(LCD_HOST, &dev, &s_touch);
}
static uint16_t xpt(uint8_t cmd) {
  uint8_t tx[3] = { cmd, 0, 0 }, rx[3] = { 0, 0, 0 };
  spi_transaction_t t = {};
  t.length = 24; t.tx_buffer = tx; t.rx_buffer = rx;
  spi_device_polling_transmit(s_touch, &t);
  return (uint16_t)(((rx[1] << 8) | rx[2]) >> 3);   // 12-bit
}

// ---- guided touch calibration (on-screen targets, saved to flash) ----
// Portrait axes line up: screen-x tracks raw X, screen-y tracks raw Y. We fit
// sx = ax*rawX + bx and sy = ay*rawY + by from taps on 5 drawn targets, then
// store the coefficients in NVS so the cal survives reboots (no reflash to keep it).
static Preferences s_prefs;
static float s_ax = 1, s_bx = 0, s_ay = 1, s_by = 0;
static bool  s_cal_valid = false;

static bool s_cal_mode = false;
static int  s_cal_idx  = 0;
#define CAL_N 5
static const int s_cal_tx[CAL_N] = { 28, HUD_W - 28, 28, HUD_W - 28, HUD_W / 2 };
static const int s_cal_ty[CAL_N] = { 54, 54, HUD_H - 60, HUD_H - 60, HUD_H / 2 };
static uint16_t s_cal_rx[CAL_N], s_cal_ry[CAL_N];

static void cal_load() {
  s_prefs.begin("hudcal", true);
  s_cal_valid = s_prefs.getBool("valid", false);
  if (s_cal_valid) {
    s_ax = s_prefs.getFloat("ax", 1); s_bx = s_prefs.getFloat("bx", 0);
    s_ay = s_prefs.getFloat("ay", 1); s_by = s_prefs.getFloat("by", 0);
  }
  s_prefs.end();
}
static void cal_save() {
  s_prefs.begin("hudcal", false);
  s_prefs.putBool("valid", true);
  s_prefs.putFloat("ax", s_ax); s_prefs.putFloat("bx", s_bx);
  s_prefs.putFloat("ay", s_ay); s_prefs.putFloat("by", s_by);
  s_prefs.end();
}
// Least-squares linear fit of screen target vs raw reading, each axis.
static void cal_compute() {
  double sX = 0, sRX = 0, sRX2 = 0, sXRX = 0;
  double sY = 0, sRY = 0, sRY2 = 0, sYRY = 0;
  for (int i = 0; i < CAL_N; i++) {
    double rx = s_cal_rx[i], tx = s_cal_tx[i];
    sX += tx; sRX += rx; sRX2 += rx * rx; sXRX += tx * rx;
    double ry = s_cal_ry[i], ty = s_cal_ty[i];
    sY += ty; sRY += ry; sRY2 += ry * ry; sYRY += ty * ry;
  }
  double n = CAL_N;
  double denX = n * sRX2 - sRX * sRX, denY = n * sRY2 - sRY * sRY;
  if (denX != 0) { s_ax = (n * sXRX - sRX * sX) / denX; s_bx = (sX - s_ax * sRX) / n; }
  if (denY != 0) { s_ay = (n * sYRY - sRY * sY) / denY; s_by = (sY - s_ay * sRY) / n; }
  s_cal_valid = true;
}
// Draw the current calibration target + prompt, then present.
static void cal_draw() {
  hud_clear(HUD_C_BG);
  int tx = s_cal_tx[s_cal_idx], ty = s_cal_ty[s_cal_idx];
  hud_ring(tx, ty, 11, HUD_C_AMBER); hud_ring(tx, ty, 4, HUD_C_AMBER);
  hud_line(tx - 15, ty, tx + 15, ty, HUD_C_RED); hud_line(tx, ty - 15, tx, ty + 15, HUD_C_RED);
  hud_text(18, 30, "TOUCH CALIBRATION", 1, HUD_C_WHITE);
  char m[32]; snprintf(m, sizeof(m), "TAP TARGET %d/%d", s_cal_idx + 1, CAL_N);
  hud_text(18, 46, m, 1, HUD_C_CYAN);
  hud_present_fb(hud_framebuffer(), HUD_W, HUD_H);
}
// Record one tap at the current target, advance, finish + save after the last.
static void cal_capture(uint16_t rx, uint16_t ry) {
  s_cal_rx[s_cal_idx] = rx; s_cal_ry[s_cal_idx] = ry;
  if (Serial.availableForWrite() > 48) Serial.printf("CAL %d raw x=%u y=%u\n", s_cal_idx, rx, ry);
  s_cal_idx++;
  if (s_cal_idx >= CAL_N) {
    cal_compute(); cal_save(); s_cal_mode = false;
    if (Serial.availableForWrite() > 48)
      Serial.printf("CAL DONE ax=%.4f bx=%.1f ay=%.4f by=%.1f\n", s_ax, s_bx, s_ay, s_by);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);     // backlight on

  s_dispOk = lcd_init();
  touch_init();
  if (!s_dispOk) {
    Serial.println("HUD: esp_lcd init failed");
  }
  if (!hud_init()) {
    Serial.println("HUD: framebuffer alloc failed - PSRAM/DMA RAM?");
  }
  hud_scan_begin();                 // bring up WiFi for live scanning

  cal_load();                       // load saved touch calibration if any
  bool held = (xpt(0xC0) < 3900);   // finger on the glass at boot -> force re-calibration
  if (!s_cal_valid || held) { s_cal_mode = true; s_cal_idx = 0; }
  Serial.printf("HUD up (esp_lcd). cal_valid=%d cal_mode=%d\n", s_cal_valid, s_cal_mode);
}

// Read the touch and, if pressed, map to screen coords. true = pressed.
// PORTRAIT map (provisional) -- to be fixed from the corner-tap raw dump below.
// The raw X/Y are exposed so the loop can print them for calibration.
static uint16_t g_rawX = 0, g_rawY = 0;
static bool touch_now(int* sx, int* sy) {
  if (xpt(0xC0) >= 3900) return false;             // z2 drops from ~4080 when pressed
  uint16_t rawX = xpt(0xD0), rawY = xpt(0x90);
  g_rawX = rawX; g_rawY = rawY;
  long x, y;
  if (s_cal_valid) {                               // saved 5-point calibration
    x = (long)(s_ax * rawX + s_bx);
    y = (long)(s_ay * rawY + s_by);
  } else {                                         // provisional portrait fallback
    x = (long)(3873 - rawX) * HUD_W / 3622;
    y = (long)(3807 - rawY) * HUD_H / 3612;
  }
  *sx = (int)(x < 0 ? 0 : x > HUD_W - 1 ? HUD_W - 1 : x);
  *sy = (int)(y < 0 ? 0 : y > HUD_H - 1 ? HUD_H - 1 : y);
  return true;
}

void loop() {
  // Guided touch calibration takes over the screen until all targets are tapped.
  if (s_cal_mode) {
    static bool calWas = false;
    bool pressed = (xpt(0xC0) < 3900);
    if (pressed && !calWas) {
      uint16_t rx = xpt(0xD0), ry = xpt(0x90);
      cal_capture(rx, ry);
    }
    calWas = pressed;
    if (s_cal_mode) cal_draw();
    return;
  }

  int sx, sy;
  bool down = touch_now(&sx, &sy);
  static bool wasDown = false;
  static uint32_t lastRepeat = 0;
  if (down) {
    hud_set_touch(sx, sy);                         // marker for feedback
    uint32_t nowMs = millis();
    if (!wasDown) {
      hud_on_press(sx, sy);                        // fresh press: tab / enter / one scroll step
      lastRepeat = nowMs;
    } else if (nowMs - lastRepeat > 160) {         // held: auto-repeat the scroll zones
      hud_on_repeat(sx, sy);
      lastRepeat = nowMs;
    }
  } else {
    hud_set_touch(-1, -1);
  }
  wasDown = down;

  hud_scan_tick(millis());    // drive the async WiFi scan (non-blocking)
  hud_tick(millis());

  static uint32_t t = 0;
  if (millis() - t > 1000 && Serial.availableForWrite() > 48) {
    t = millis();
    Serial.printf("FPS ~%u disp=%d wifi=%d\n", hud_fps_x10() / 10, s_dispOk, hud_scan_count());
  }
}
