#include "hud_core.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

// NM-CYD-C5: ST7789 on the shared SPI bus, driven via esp_lcd (IDF native).
// The Arduino core SPIClass would not apply a >~10 MHz clock on the C5 and
// Arduino_GFX's own register-level SPI didn't drive this panel; esp_lcd uses
// the IDF spi_master driver (correct C5 clock + DMA).
#define LCD_SCLK 6
#define LCD_MOSI 7
#define LCD_MISO -1
#define LCD_DC   24
#define LCD_CS   23
#define LCD_RST  -1
#define LCD_BL   25
#define LCD_HOST SPI2_HOST
#define LCD_PCLK (40 * 1000 * 1000)   // 40 MHz; esp_lcd sets the C5 divider correctly

static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static bool s_dispOk = false;

// Strong override of the weak default in hud_core.cpp: push the whole buffer
// over SPI DMA. hud_rgb already stores bytes MSB-first for the ST7789.
volatile uint32_t g_push_us = 0;
void hud_present_fb(const uint16_t* fb, int w, int h) {
  uint32_t t0 = micros();
  esp_lcd_panel_draw_bitmap(s_panel, 0, 0, w, h, (void*)fb);
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

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);     // backlight on

  s_dispOk = lcd_init();
  if (!s_dispOk) {
    Serial.println("HUD: esp_lcd init failed");
  }
  if (!hud_init()) {
    Serial.println("HUD: framebuffer alloc failed - PSRAM/DMA RAM?");
  }
  Serial.println("HUD V1 core up (esp_lcd).");
}

void loop() {
  hud_tick(millis());
  // Non-blocking status: only print when the USB-CDC TX buffer has room, so the
  // loop never stalls on Serial when nothing is reading the port.
  static uint32_t t = 0;
  if (millis() - t > 1000 && Serial.availableForWrite() > 48) {
    t = millis();
    Serial.printf("FPS ~%u disp=%d push=%luus\n", hud_fps_x10() / 10, s_dispOk, (unsigned long)g_push_us);
  }
}
