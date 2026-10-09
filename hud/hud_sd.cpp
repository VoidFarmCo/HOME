#include "hud_sd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <Preferences.h>

#define SD_HOST SPI2_HOST       // the same bus esp_lcd + touch already use
#define SD_CS   10

static sdmmc_card_t* s_card = nullptr;
static bool s_ok = false;
static esp_err_t s_err = ESP_FAIL;

// Mount the SD on the shared SPI bus (already up from esp_lcd). allowFormat=true lets
// the driver reformat a card it cannot mount -- used ONLY by the deliberate FORMAT path,
// never at boot (a flaky card must not get auto-wiped).
static esp_err_t do_mount(bool allowFormat) {
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SD_HOST;
  host.max_freq_khz = 20000;                 // 20 MHz; the driver negotiates down if needed

  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs  = (gpio_num_t)SD_CS;
  dev.host_id  = SD_HOST;

  esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
  mcfg.format_if_mount_failed = allowFormat;
  mcfg.max_files = 4;
  mcfg.allocation_unit_size = 16 * 1024;
  return esp_vfs_fat_sdspi_mount("/sd", &host, &dev, &mcfg, &s_card);
}

bool hud_sd_begin() {
  // A one-shot "format on next boot" flag (set by the FORMAT button). Formatting a big
  // card needs a large work buffer that only allocates reliably at boot, when the heap
  // is pristine -- at runtime it fails ESP_ERR_NO_MEM. So we format here, once, then clear.
  Preferences p; p.begin("hudsd", false);
  bool fmt = p.getBool("fmt1", false);
  if (fmt) p.putBool("fmt1", false);
  p.end();
  s_err = do_mount(fmt);                      // format ONLY if the one-shot flag was set
  s_ok = (s_err == ESP_OK);
  return s_ok;
}

// FORMAT button: arm a one-shot boot format, then the caller reboots (format runs in
// hud_sd_begin with a pristine heap). Runtime formatting is unreliable (ESP_ERR_NO_MEM).
void hud_sd_request_format() {
  Preferences p; p.begin("hudsd", false); p.putBool("fmt1", true); p.end();
}

// Runtime format (used only for a mounted card; big-card recovery goes via reboot).
bool hud_sd_format() {
  if (s_ok && s_card) {                      // mounted: wipe it in place
    s_err = esp_vfs_fat_sdcard_format("/sd", s_card);
    s_ok = (s_err == ESP_OK);
    return s_ok;
  }
  s_err = do_mount(true);
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

// List files in /sd (one dir pass). Fills names[0..maxn)/sizes, returns the count.
int hud_sd_list(char names[][24], uint32_t* sizes, int maxn) {
  if (!s_ok) return 0;
  DIR* d = opendir("/sd");
  if (!d) return 0;
  int n = 0;
  struct dirent* e;
  while ((e = readdir(d)) && n < maxn) {
    if (e->d_type == DT_DIR) continue;
    strncpy(names[n], e->d_name, 23); names[n][23] = 0;
    char full[300]; snprintf(full, sizeof(full), "/sd/%s", e->d_name);
    struct stat st; sizes[n] = (stat(full, &st) == 0) ? (uint32_t)st.st_size : 0;
    n++;
  }
  closedir(d);
  return n;
}

bool hud_sd_remove(const char* name) {
  if (!s_ok || !name || !*name) return false;
  char full[300]; snprintf(full, sizeof(full), "/sd/%s", name);
  return remove(full) == 0;
}

// Read the LAST (bufsz-1) bytes of a file (the recent end of a log), NUL-terminated.
int hud_sd_read_tail(const char* name, char* buf, int bufsz) {
  if (!s_ok || !name || bufsz < 2) return 0;
  char full[300]; snprintf(full, sizeof(full), "/sd/%s", name);
  FILE* fp = fopen(full, "rb");
  if (!fp) return 0;
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  long want = bufsz - 1;
  if (fseek(fp, (sz > want) ? sz - want : 0, SEEK_SET) != 0) { fclose(fp); return 0; }
  int n = (int)fread(buf, 1, bufsz - 1, fp);
  if (n < 0) n = 0;
  buf[n] = 0;
  fclose(fp);
  return n;
}
