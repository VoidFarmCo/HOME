#include "hud_core.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hud_pages.h"

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
  esp_lcd_panel_swap_xy(s_panel, true);          // landscape 320x240
  esp_lcd_panel_mirror(s_panel, false, true);    // = Arduino_GFX rotation 3 (the orientation that fit)
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
// true if pressed; fills mapped screen coords (and raw for calibration).
static bool touch_read(int* sx, int* sy, uint16_t* rx, uint16_t* ry, uint16_t* zz) {
  uint16_t z1 = xpt(0xB0);
  *zz = z1;
  if (z1 < 400) return false;                        // threshold -- tune if needed
  uint16_t rawx = xpt(0xD0), rawy = xpt(0x90);
  *rx = rawx; *ry = rawy;
  // vendor cal: X 185..3700 -> 0..W, Y 250..3800 -> 0..H. Orientation TBD (calibrate by taps).
  long x = (long)(rawx - 185) * (HUD_W - 1) / (3700 - 185);
  long y = (long)(rawy - 250) * (HUD_H - 1) / (3800 - 250);
  *sx = (int)(x < 0 ? 0 : x > HUD_W - 1 ? HUD_W - 1 : x);
  *sy = (int)(y < 0 ? 0 : y > HUD_H - 1 ? HUD_H - 1 : y);
  return true;
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
  Serial.println("HUD V1 core up (esp_lcd).");
}

// Read the touch and, if pressed, map to screen coords. true = pressed.
// 5-point calibration (2026-10-08): in landscape the touch axes are swapped AND
// inverted -- screen_x from raw Y, screen_y from raw X.
static bool touch_now(int* sx, int* sy) {
  if (xpt(0xC0) >= 3900) return false;             // z2 drops from ~4080 when pressed
  uint16_t rawX = xpt(0xD0), rawY = xpt(0x90);
  long x = (long)(3807 - rawY) * HUD_W / 3612;
  long y = (long)(3873 - rawX) * HUD_H / 3622;
  *sx = (int)(x < 0 ? 0 : x > HUD_W - 1 ? HUD_W - 1 : x);
  *sy = (int)(y < 0 ? 0 : y > HUD_H - 1 ? HUD_H - 1 : y);
  return true;
}

void loop() {
  int sx, sy;
  bool down = touch_now(&sx, &sy);
  static bool wasDown = false;
  if (down) {
    hud_set_touch(sx, sy);                         // marker for feedback
    if (!wasDown) hud_on_touch(sx, sy);            // act once per fresh press
  } else {
    hud_set_touch(-1, -1);
  }
  wasDown = down;

  hud_tick(millis());

  static uint32_t t = 0;
  if (millis() - t > 1000 && Serial.availableForWrite() > 48) {
    t = millis();
    Serial.printf("FPS ~%u disp=%d\n", hud_fps_x10() / 10, s_dispOk);
  }
}
