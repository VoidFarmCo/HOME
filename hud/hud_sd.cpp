#include "hud_sd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include <dirent.h>
#include <stdio.h>

#define SD_HOST SPI2_HOST       // the same bus esp_lcd + touch already use
#define SD_CS   10

static sdmmc_card_t* s_card = nullptr;
static bool s_ok = false;
static esp_err_t s_err = ESP_FAIL;

bool hud_sd_begin() {
  // The SPI bus is already up (esp_lcd's spi_bus_initialize), so we only add the
  // SD as a device on it -- do NOT re-init the bus here.
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SD_HOST;
  host.max_freq_khz = 20000;                 // 20 MHz; the driver negotiates down if needed

  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs  = (gpio_num_t)SD_CS;
  dev.host_id  = SD_HOST;

  esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
  mcfg.format_if_mount_failed = true;        // owner opted in: wipe + FAT32 a card we can't mount
  mcfg.max_files = 4;
  mcfg.allocation_unit_size = 16 * 1024;     // 16 KB clusters (used for the format too)

  s_err = esp_vfs_fat_sdspi_mount("/sd", &host, &dev, &mcfg, &s_card);
  s_ok = (s_err == ESP_OK);
  return s_ok;
}

bool hud_sd_ok() { return s_ok; }
const char* hud_sd_err() { return esp_err_to_name(s_err); }

uint32_t hud_sd_size_mb() {
  if (!s_ok || !s_card) return 0;
  uint64_t bytes = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
  return (uint32_t)(bytes / (1024ULL * 1024ULL));
}

int hud_sd_root_count() {
  if (!s_ok) return -1;
  DIR* d = opendir("/sd");
  if (!d) return -1;
  int n = 0;
  while (readdir(d)) n++;
  closedir(d);
  return n;
}

bool hud_sd_append(const char* path, const char* line) {
  if (!s_ok || !path || !line) return false;
  char full[80];
  snprintf(full, sizeof(full), "/sd/%s", path);
  FILE* fp = fopen(full, "a");
  if (!fp) return false;
  fputs(line, fp);
  fputc('\n', fp);
  fclose(fp);                       // flush now; logging is infrequent (per few sec)
  return true;
}
