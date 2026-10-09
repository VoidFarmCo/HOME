#pragma once
#include <stdint.h>

// microSD on the shared SPI bus (the one esp_lcd set up: SCLK 6 / MOSI 7 / MISO 2),
// mounted via the IDF sdspi driver as another device on that bus (CS 10) -- same
// way the XPT2046 touch shares it. FAT (FAT16/FAT32). Mounted at /sd.
bool     hud_sd_begin();        // mount; false if no card / not FAT / wiring
bool     hud_sd_ok();
uint32_t hud_sd_size_mb();      // card capacity in MB (0 if not mounted)
int      hud_sd_root_count();   // entries in the root dir (quick read sanity)
const char* hud_sd_err();       // esp_err name of the last mount attempt
bool     hud_sd_append(const char* path, const char* line);  // append line + '\n' to /sd/<path>
int      hud_sd_list(char names[][24], uint32_t* sizes, int maxn);  // list files -> names/sizes, returns count
bool     hud_sd_remove(const char* name);                    // delete /sd/<name>
