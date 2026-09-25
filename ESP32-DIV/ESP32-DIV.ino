#include <Arduino.h>
#include <PCF8574.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "BootLock.h"
#include "Stealth.h"
#include "SettingsStore.h"
#include "Touchscreen.h"
#include "config.h"
#include "FastPairScan.h"
#include "DroneScan.h"
#include "Spotter.h"
#include "TrackerHunt.h"
#include "ducky.h"
#include "Branding.h"
#include "icon.h"
#include "gps.h"
#include "rfid.h"
#include "shared.h"
#include "utils.h"

#if !BOARD_HAS_ESP32S3
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#endif

TFT_eSPI tft = TFT_eSPI();

PCF8574 pcf(PCF8574_I2C_ADDR);

void setBrightness(uint8_t value) {
  ledcWrite(PWM_CHANNEL, value);
}

bool feature_exit_requested = false;

/* The main menu, in COLUMN-MAJOR order. displayMenu() lays it out with
 * column = i / 4 and row = i % 4, so 0..3 are the left column top to bottom
 * and 4..7 are the right. Read as a list this looks shuffled; read as two
 * columns it is what is on the screen. Adding an entry means thinking about
 * which column it lands in, not appending.
 *
 * "NRF24" was "2.4GHz" until 0.4.11. It is the Nordic module -- Scanner,
 * Proto Kill, ESB Sniffer, MouseJack -- and naming it by its band put it in
 * competition with the two tiles above it, which are also 2.4 GHz radios.
 * Somebody looking for the BLE jammer had a sound reason to open it. */
const int NUM_MENU_ITEMS = 8;
const char *menu_items[NUM_MENU_ITEMS] = {
    "WiFi",
    "NRF24",
    "More",
    "Settings",
    "Bluetooth",
    "SubGHz",
    "Tools",
    "About"};

/* These names are upstream's and several no longer describe where they are
 * used: the NRF24 tile wears bitmap_icon_jammer, Bluetooth wears
 * bitmap_icon_spoofer, SubGHz wears bitmap_icon_analyzer. They render
 * correctly -- they are generic glyphs, reused across menus -- so they are
 * not renamed, because the other use sites would then be the misleading
 * ones. Read the position, not the name. */
const unsigned char *bitmap_icons[NUM_MENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_jammer,
    bitmap_icon_dots,
    bitmap_icon_setting,
    bitmap_icon_spoofer,
    bitmap_icon_analyzer,
    bitmap_icon_stat,
    bitmap_icon_question};

int current_menu_index = 0;
bool is_main_menu = false;

const int NUM_SUBMENU_ITEMS = 12;
const char *submenu_items[NUM_SUBMENU_ITEMS] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Request Flood",
    "Deauth Detector",
    "WiFi Scanner",
    "Captive Portal",
    "Hidden SSID Revealer",
    "WPS Scanner",
    "ARP Scanner",
    "Karma Attack",
    "Back to Main Menu"};

// WiFi submenu is split across two pages (features after Hidden SSID on page 2).
// Bottom row: icon | Main Menu                 Next/Prev Page | icon
static constexpr int WIFI_PAGE0_FEATURES = 6;
static constexpr int WIFI_PAGE1_FEATURES = 5;
static int wifi_submenu_page = 0;

const char *wifi_page0_items[WIFI_PAGE0_FEATURES] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Request Flood",
    "Deauth Detector",
    "WiFi Scanner"};

const char *wifi_page1_items[WIFI_PAGE1_FEATURES] = {
    "Captive Portal",
    "Hidden SSID Revealer",
    "WPS Scanner",
    "ARP Scanner",
    "Karma Attack"};

// Bluetooth submenu uses the same paged footer layout as WiFi.
static constexpr int BT_PAGE0_FEATURES = 6;
static constexpr int BT_PAGE1_FEATURES = 5;
static int bluetooth_submenu_page = 0;

const char *bluetooth_page0_items[BT_PAGE0_FEATURES] = {
    "BLE Jammer",
    "BLE Spoofer",
    "Sour Apple",
    "AirTag Spoofer",
    "AirTag Sniffer",
    "Sniffer"};

const char *bluetooth_page1_items[BT_PAGE1_FEATURES] = {
    "BLE Scanner",
    "BLE Rubber Ducky",
    "Skimmer Detect",
    "Hunt",
    "Fast Pair"};

static FeatureUI::Button s_pagedFooterBtns[2];
static int s_pagedFooterFocus = -1;  // 0=back, 1=page btn, -1=none

const int nrf_NUM_SUBMENU_ITEMS = 7;
const char *nrf_submenu_items[nrf_NUM_SUBMENU_ITEMS] = {
    "Scanner",
    "Proto Kill",
    "ESB Sniffer",
    "ESB Replay",
    "MouseJack Scan",
    "MouseJack Inject",
    "Back to Main Menu"};

const int subghz_NUM_SUBMENU_ITEMS = 6;
const char *subghz_submenu_items[subghz_NUM_SUBMENU_ITEMS] = {
    "Replay Attack",
    "SubGHz Jammer",
    "De Bruijn / Brute",
    "Jamming Detector",
    "Saved Profile",
    "Back to Main Menu"};

const int tools_NUM_SUBMENU_ITEMS = 5;
const char *tools_submenu_items[tools_NUM_SUBMENU_ITEMS] = {
    "Serial Monitor",
    "Update Firmware",
    "Touch Calibrate",
    "SD File Manager",
    "Back to Main Menu"};

static constexpr uint8_t OTHER_LAYER_HOME = 0;
static constexpr uint8_t OTHER_LAYER_RFID = 1;
static constexpr uint8_t OTHER_LAYER_GPS  = 2;

const int other_NUM_SUBMENU_ITEMS = 5;
static constexpr int OTHER_GRID_COLS = 2;
const char *other_submenu_items[other_NUM_SUBMENU_ITEMS] = {
    "RFID/NFC",
    "GPS",
    "Surveillance",
    "Drone Detector",
    "Main Menu"};

const int rfid_NUM_SUBMENU_ITEMS = 9;
const char *rfid_submenu_items[rfid_NUM_SUBMENU_ITEMS] = {
    "Card Reader",
    "Card Clone",
    "Erase",
    "Dump",
    "Decode Access",
    "Jam Reader",
    "Tag Disrupt",
    "Disrupt Emulate",
    "Back to More"};

const int gps_NUM_SUBMENU_ITEMS = 3;
const char *gps_submenu_items[gps_NUM_SUBMENU_ITEMS] = {
    "Wardriver",
    "Satellite Scanner",
    "Back to More"};

const int about_NUM_SUBMENU_ITEMS = 1;
const char *about_submenu_items[about_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

const int setting_NUM_SUBMENU_ITEMS = 1;
const char *setting_submenu_items[setting_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

int current_submenu_index = 0;
bool in_sub_menu = false;
int last_submenu_index = -1;
bool submenu_initialized = false;
uint8_t other_layer = OTHER_LAYER_HOME;
int last_other_menu_index = -1;
bool other_menu_grid_initialized = false;

const char **active_submenu_items = nullptr;
int active_submenu_size = 0;

const unsigned char *wifi_submenu_icons[NUM_SUBMENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_Skull_3,
    bitmap_icon_eye2,
    bitmap_icon_jammer,
    bitmap_icon_bash,
    bitmap_icon_eye_blind,
    bitmap_icon_key,
    bitmap_icon_list,
    bitmap_icon_devil,
    bitmap_icon_go_back
};

const unsigned char *wifi_page0_icons[WIFI_PAGE0_FEATURES] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_Skull_3,
    bitmap_icon_eye2,
    bitmap_icon_jammer
};

const unsigned char *wifi_page1_icons[WIFI_PAGE1_FEATURES] = {
    bitmap_icon_bash,
    bitmap_icon_eye_blind,
    bitmap_icon_key,
    bitmap_icon_list,
    bitmap_icon_devil
};

const unsigned char *bluetooth_page0_icons[BT_PAGE0_FEATURES] = {
    bitmap_icon_ble_jammer,
    bitmap_icon_spoofer,
    bitmap_icon_apple,
    bitmap_icon_tags,
    bitmap_icon_magnifying_glass,
    bitmap_icon_analyzer
};

const unsigned char *bluetooth_page1_icons[BT_PAGE1_FEATURES] = {
    bitmap_icon_graph,
    bitmap_icon_rubber_ducky,
    bitmap_icon_Wireless_4,
    bitmap_icon_compass,
    bitmap_icon_ble
};

const unsigned char *nrf_submenu_icons[nrf_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_scanner,
    bitmap_icon_kill,
    bitmap_icon_analyzer,
    bitmap_icon_follow,
    bitmap_icon_magnifying_glass,
    bitmap_icon_key,
    bitmap_icon_go_back
};

const unsigned char *subghz_submenu_icons[subghz_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_antenna,
    bitmap_icon_no_signal,
    bitmap_icon_graph_self_loop,
    bitmap_icon_Voice_Id,
    bitmap_icon_list,
    bitmap_icon_go_back
};

const unsigned char *tools_submenu_icons[tools_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_bash,
    bitmap_icon_follow,
    bitmap_icon_undo,
    bitmap_icon_sdcard,
    bitmap_icon_go_back
};

const unsigned char *other_submenu_icons[other_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_rfid_chip,
    bitmap_icon_satellite,
    bitmap_icon_eye,
    bitmap_icon_satellite,
    bitmap_icon_go_back
};

const unsigned char *rfid_submenu_icons[rfid_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_magnifying_glass,
    bitmap_icon_follow,
    bitmap_icon_recycle,
    bitmap_icon_dot_matrix,
    bitmap_icon_key,
    bitmap_icon_kill,
    bitmap_icon_flash,
    bitmap_icon_devil,
    bitmap_icon_go_back
};

const unsigned char *gps_submenu_icons[gps_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_satellite,
    bitmap_icon_satellite_dish,
    bitmap_icon_go_back
};

const unsigned char *about_submenu_icons[about_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char *setting_submenu_icons[setting_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char **active_submenu_icons = nullptr;

static int wifiFeatureCount() {
    return (wifi_submenu_page == 0) ? WIFI_PAGE0_FEATURES : WIFI_PAGE1_FEATURES;
}

static int bluetoothFeatureCount() {
    return (bluetooth_submenu_page == 0) ? BT_PAGE0_FEATURES : BT_PAGE1_FEATURES;
}

static int pagedFeatureCount() {
    if (current_menu_index == 4) {
        return bluetoothFeatureCount();
    }
    return wifiFeatureCount();
}

static int* pagedSubmenuPage() {
    return (current_menu_index == 4) ? &bluetooth_submenu_page : &wifi_submenu_page;
}

// Bottom row: [icon | Main Menu] ........ [Next/Prev Page | icon]
static int pagedBackBtnIndex() {
    return pagedFeatureCount();
}

static int pagedPageBtnIndex() {
    return pagedFeatureCount() + 1;
}

static int pagedNavRowY() {
    return tft.height() - 30;
}

static const char* pagedPageBtnLabel() {
    return (*pagedSubmenuPage() == 0) ? "Next Page" : "Prev Page";
}

static const unsigned char* pagedPageBtnIcon() {
    return (*pagedSubmenuPage() == 0) ? bitmap_icon_navigate_right : bitmap_icon_navigate_left;
}

static void layoutPagedFooterButtons() {
    const int y = pagedNavRowY();
    const int mid = tft.width() / 2;
    s_pagedFooterBtns[0] = {
        0, (int16_t)y, (int16_t)mid, 28,
        "Main Menu", FeatureUI::ButtonStyle::Secondary, false};
    s_pagedFooterBtns[1] = {
        (int16_t)mid, (int16_t)y, (int16_t)(tft.width() - mid), 28,
        pagedPageBtnLabel(), FeatureUI::ButtonStyle::Secondary, false};
}

static void drawPagedFooterButtons() {
    layoutPagedFooterButtons();
    const int y = pagedNavRowY();
    const int rowH = 28;
    const int iconSize = 16;
    tft.fillRect(0, y, tft.width(), rowH, UI_BG);

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    // Font 2 is ~16px; center icon + text on the same midline within the row.
    const int textH = 16;
    const int iconY = y + (rowH - iconSize) / 2;
    const int textY = y + (rowH - textH) / 2;

    {
        const uint16_t color = (s_pagedFooterFocus == 0) ? UI_ICON : UI_TEXT;
        tft.setTextColor(color, UI_BG);
        tft.drawBitmap(10, iconY, bitmap_icon_go_back, iconSize, iconSize, color);
        tft.setCursor(30, textY);
        tft.print("Main Menu");
    }

    {
        const uint16_t color = (s_pagedFooterFocus == 1) ? UI_ICON : UI_TEXT;
        const char* label = pagedPageBtnLabel();
        const int gap = 4;
        const int textW = tft.textWidth(label);
        // Right-aligned group: [label][gap][icon] — same vertical midline.
        const int iconX = tft.width() - 10 - iconSize;
        const int textX = iconX - gap - textW;
        tft.setTextColor(color, UI_BG);
        tft.setCursor(textX, textY);
        tft.print(label);
        tft.drawBitmap(iconX, iconY, pagedPageBtnIcon(), iconSize, iconSize, color);
    }
}

static void applyWifiSubmenuPage() {
    if (wifi_submenu_page == 0) {
        active_submenu_items = wifi_page0_items;
        active_submenu_icons = wifi_page0_icons;
    } else {
        active_submenu_items = wifi_page1_items;
        active_submenu_icons = wifi_page1_icons;
    }
    active_submenu_size = wifiFeatureCount() + 2;
    if (current_submenu_index >= active_submenu_size) {
        current_submenu_index = 0;
    }
    s_pagedFooterFocus = -1;
    last_submenu_index = -1;
    submenu_initialized = false;
}

static void applyBluetoothSubmenuPage() {
    if (bluetooth_submenu_page == 0) {
        active_submenu_items = bluetooth_page0_items;
        active_submenu_icons = bluetooth_page0_icons;
    } else {
        active_submenu_items = bluetooth_page1_items;
        active_submenu_icons = bluetooth_page1_icons;
    }
    active_submenu_size = bluetoothFeatureCount() + 2;
    if (current_submenu_index >= active_submenu_size) {
        current_submenu_index = 0;
    }
    s_pagedFooterFocus = -1;
    last_submenu_index = -1;
    submenu_initialized = false;
}

void updateActiveSubmenu() {
    switch (current_menu_index) {
        case 0:
            wifi_submenu_page = 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            break;
        case 1:
            active_submenu_items = nrf_submenu_items;
            active_submenu_size = nrf_NUM_SUBMENU_ITEMS;
            active_submenu_icons = nrf_submenu_icons;
            break;
        case 2:
            if (other_layer == OTHER_LAYER_HOME) {
                active_submenu_items = other_submenu_items;
                active_submenu_size = other_NUM_SUBMENU_ITEMS;
                active_submenu_icons = other_submenu_icons;
                        } else if (other_layer == OTHER_LAYER_RFID) {
                active_submenu_items = rfid_submenu_items;
                active_submenu_size = rfid_NUM_SUBMENU_ITEMS;
                active_submenu_icons = rfid_submenu_icons;
            } else if (other_layer == OTHER_LAYER_GPS) {
                active_submenu_items = gps_submenu_items;
                active_submenu_size = gps_NUM_SUBMENU_ITEMS;
                active_submenu_icons = gps_submenu_icons;
            } else {
                active_submenu_items = other_submenu_items;
                active_submenu_size = other_NUM_SUBMENU_ITEMS;
                active_submenu_icons = other_submenu_icons;
            }
            break;
        case 3:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
        case 4:
            bluetooth_submenu_page = 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            break;
        case 5:
            active_submenu_items = subghz_submenu_items;
            active_submenu_size = subghz_NUM_SUBMENU_ITEMS;
            active_submenu_icons = subghz_submenu_icons;
            break;
        case 6:
            active_submenu_items = tools_submenu_items;
            active_submenu_size = tools_NUM_SUBMENU_ITEMS;
            active_submenu_icons = tools_submenu_icons;
            break;
        case 7:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;

        default:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
    }
}

static bool touchButtonInputEnabled = false;
static bool touchButtonCueDrawn = false;
static bool s_touchNavLabelsConfigured = false;
static bool s_touchNavHeld[5] = {false, false, false, false, false};
#if HAS_PCF8574_BUTTONS
static bool s_pcfButtonLastState[8] = {true, true, true, true, true, true, true, true};
#endif
static FeatureUI::Button s_touchNavBtns[5];
static const char* s_touchNavLabels[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
static constexpr int16_t TOUCH_NAV_BAR_H = (FeatureUI::FOOTER_H * 4) / 5;  // 20% shorter than footer

static int touchNavPinForIndex(int idx) {
  switch (idx) {
    case 0: return BTN_LEFT;
    case 1: return BTN_DOWN;
    case 2: return BTN_SELECT;
    case 3: return BTN_UP;
    case 4: return BTN_RIGHT;
    default: return -1;
  }
}

void setTouchButtonInputEnabled(bool enabled) {
  if (touchButtonInputEnabled != enabled) {
    touchButtonCueDrawn = false;
    if (!enabled) {
      for (int i = 0; i < 5; ++i) {
        s_touchNavLabels[i] = nullptr;
      }
      s_touchNavLabelsConfigured = false;
      for (int i = 0; i < 5; ++i) {
        s_touchNavHeld[i] = false;
      }
    }
  }
  touchButtonInputEnabled = enabled;
#if TOUCH_BUTTON_CUE_ENABLED
  if (enabled && feature_active) {
    drawTouchNavBar();
    touchButtonCueDrawn = true;
  }
#endif
}

bool featureHasTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  return touchButtonInputEnabled && feature_active;
#else
  return false;
#endif
}

void setTouchNavLabels(const char* left, const char* down, const char* center,
                       const char* up, const char* right) {
  s_touchNavLabels[0] = left;
  s_touchNavLabels[1] = down;
  s_touchNavLabels[2] = center;
  s_touchNavLabels[3] = up;
  s_touchNavLabels[4] = right;
  s_touchNavLabelsConfigured = true;
  invalidateTouchButtonCue();
}

void invalidateTouchButtonCue() {
  touchButtonCueDrawn = false;
}

void resetTouchNavHeldState() {
  for (int i = 0; i < 5; ++i) {
    s_touchNavHeld[i] = false;
  }
}

void redrawTouchButtonBar() {
  invalidateTouchButtonCue();
  drawTouchButtonCue();
}

static void layoutTouchNavBtns() {
  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int totalW = tft.width();
  const int cellW = totalW / 5;

  for (int i = 0; i < 5; ++i) {
    const int x = i * cellW;
    const int w = (i == 4) ? (totalW - x) : cellW;
    s_touchNavBtns[i] = {
      (int16_t)x, (int16_t)barY, (int16_t)w, (int16_t)barH,
      nullptr, FeatureUI::ButtonStyle::Secondary, false};
  }
}

static String fitTouchNavLabel(const char* label, int maxWidth) {
  if (!label || !label[0]) {
    return String();
  }
  String out = label;
  if (tft.textWidth(out) <= maxWidth) {
    return out;
  }
  while (out.length() > 1 && tft.textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

static void drawTouchNavBar() {
  static const unsigned char* kIcons[5] = {
    bitmap_icon_LEFT,
    bitmap_icon_DOWN,
    bitmap_icon_go_back,
    bitmap_icon_UP,
    bitmap_icon_RIGHT,
  };
  constexpr int kIconSize = 16;

  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int barW = tft.width();

  layoutTouchNavBtns();

  tft.fillRect(0, barY, barW, barH, UI_FG);
  tft.drawFastHLine(0, barY, barW, UI_LINE);

  for (int i = 0; i < 5; ++i) {
    const auto& b = s_touchNavBtns[i];
    if (i > 0) {
      tft.drawFastVLine(b.x, barY + 3, barH - 6, UI_LINE);
    }

    if (s_touchNavLabels[i] && s_touchNavLabels[i][0]) {
      tft.setTextDatum(MC_DATUM);
      const uint16_t txtColor = (i == 2) ? UI_ICON : UI_TEXT;
      tft.setTextColor(txtColor, UI_FG);
      const String fit = fitTouchNavLabel(s_touchNavLabels[i], b.w - 8);
      tft.drawString(fit, b.x + b.w / 2, b.y + b.h / 2, 1);
    } else {
      const int ix = b.x + (b.w - kIconSize) / 2;
      const int iy = b.y + (b.h - kIconSize) / 2;
      const bool inactiveSlot = s_touchNavLabelsConfigured && !s_touchNavLabels[i];
      const unsigned char* icon = inactiveSlot ? bitmap_icon_dots : kIcons[i];
      const uint16_t iconColor = inactiveSlot ? LIGHT_GRAY : ((i == 2) ? UI_ICON : UI_TEXT);
      tft.drawBitmap(ix, iy, icon, kIconSize, kIconSize, iconColor);
    }
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
}

void maintainTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active || touchButtonCueDrawn) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

void featureClearContent(uint16_t color) {
  const int bottom = touchNavContentBottomY();
  if (bottom > 0) {
    tft.fillRect(0, 0, tft.width(), bottom, color);
  } else {
    tft.fillScreen(color);
  }
}

int16_t touchNavReservedHeight() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (touchButtonInputEnabled && feature_active) {
    return TOUCH_NAV_BAR_H;
  }
#endif
  return 0;
}

int16_t touchNavContentBottomY() {
  return (int16_t)(tft.height() - touchNavReservedHeight());
}

void drawTouchButtonCue() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

static int touchNavIndexForPin(int buttonPin) {
  for (int i = 0; i < 5; ++i) {
    if (touchNavPinForIndex(i) == buttonPin) {
      return i;
    }
  }
  return -1;
}

static bool isTouchNavSlotDown(int idx) {
  if (idx < 0 || !feature_active || !touchButtonInputEnabled) {
    return false;
  }

  int x = 0;
  int y = 0;
  if (!readTouchXYDismiss(x, y)) {
    return false;
  }

  layoutTouchNavBtns();

  const int stripTop = tft.height() - TOUCH_NAV_BAR_H;
  if (y < stripTop) {
    return false;
  }

  return FeatureUI::hit(s_touchNavBtns, 5, x, y) == idx;
}

bool isPhysicalButtonPressed(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    return !pcf.digitalRead(buttonPin);
  }
#endif
  return false;
}

bool isTouchNavButtonPressed(int buttonPin) {
  const int idx = touchNavIndexForPin(buttonPin);
  if (idx < 0) {
    return false;
  }
  return isTouchNavSlotDown(idx);
}

bool isButtonPressed(int buttonPin) {
  if (isPhysicalButtonPressed(buttonPin)) {
    return true;
  }
  return isTouchNavButtonPressed(buttonPin);
}

bool isTouchNavButtonPressedEdge(int buttonPin) {
  if (!feature_active || !touchButtonInputEnabled) {
    return false;
  }

  const int navIdx = touchNavIndexForPin(buttonPin);
  if (navIdx < 0) {
    return false;
  }

  const bool down = isTouchNavSlotDown(navIdx);
  const bool edge = down && !s_touchNavHeld[navIdx];
  s_touchNavHeld[navIdx] = down;
  return edge;
}

bool isButtonPressedEdge(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    const int idx = buttonPin % 8;
    const bool cur = pcf.digitalRead(buttonPin);
    const bool edge = !cur && s_pcfButtonLastState[idx];
    s_pcfButtonLastState[idx] = cur;
    if (edge) {
      return true;
    }
  }
#endif

  return isTouchNavButtonPressedEdge(buttonPin);
}

bool featureExitButtonPressed() {
  return isPhysicalButtonPressed(BTN_SELECT) || isTouchNavButtonPressed(BTN_SELECT);
}

static void showFeatureUnavailable(const char* featureName, const char* requirement) {
  feature_active = false;
  feature_exit_requested = false;
  showNotification(featureName, requirement);
  delay(250);
}

static void runBleDuckyFeature() {
#if FEATURE_BLE_DUCKY
  current_submenu_index = 5;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  Ducky::enter();
  while (current_submenu_index == 5 && !feature_exit_requested) {
      current_submenu_index = 5;
      in_sub_menu = true;
      Ducky::loop();
  }

  Ducky::exit();
  if (feature_exit_requested) {
      in_sub_menu = true;
      is_main_menu = false;
      submenu_initialized = false;
      feature_active = false;
      feature_exit_requested = false;
      displaySubmenu();
      delay(200);
  }
#else
  showFeatureUnavailable("BLE Rubber Ducky", "This feature requires ESP32-S3.");
#endif
}

float currentBatteryVoltage = readBatteryVoltage();
unsigned long last_interaction_time = 0;

int last_menu_index = -1;
bool menu_initialized = false;

/* Menu grid geometry. The 2.8" values are the originals and are kept
 * exactly, because the screen renders in render/ were drawn against them.
 * The 3.5" panel is 320x480 rather than 240x320, so its tiles are sized to
 * fill it instead of leaving a border of unused pixels: same 2x4 grid, same
 * 10 px margins and gaps, just bigger. */
#if TFT_WIDTH >= 320
const int TILE_W = 145;
const int TILE_H = 92;
const int COLUMN_WIDTH = 155;
const int X_OFFSET_LEFT = 10;
const int X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH;
const int Y_START = 44;
const int Y_SPACING = 106;
// icon(32) + 6 gap + label(16) = 54, centred in a 92 px tile
const int TILE_ICON_DY = 19;
const int TILE_TEXT_DY = 57;
#else
const int TILE_W = 100;
const int TILE_H = 60;
const int COLUMN_WIDTH = 120;
const int X_OFFSET_LEFT = 10;
const int X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH;
const int Y_START = 30;
const int Y_SPACING = 75;
const int TILE_ICON_DY = 10;
const int TILE_TEXT_DY = 30;
#endif

void displayOtherMenuGrid();
void displayPagedSubmenu();

// Last submenu item ("Back to Main Menu") is pinned to the bottom of the screen.
static int submenuItemY(int index) {
    if (active_submenu_size > 0 && index == active_submenu_size - 1) {
        return tft.height() - 30;
    }
    return 30 + index * 30;
}

void displaySubmenu() {
    setTouchButtonInputEnabled(false);

    if (current_menu_index == 2 && other_layer == OTHER_LAYER_HOME) {
        displayOtherMenuGrid();
        return;
    }

    if (current_menu_index == 0 || current_menu_index == 4) {
        displayPagedSubmenu();
        return;
    }

    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);
    tft.setTextSize(1);

    if (!submenu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < active_submenu_size; i++) {
            const int yPos = submenuItemY(i);
            const bool isBack = (i == active_submenu_size - 1);

            tft.setTextColor(UI_TEXT, UI_BG);
            tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, UI_TEXT);
            tft.setCursor(30, yPos);
            if (!isBack) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[i]);
        }

        submenu_initialized = true;
        last_submenu_index = -1;
    }

    if (last_submenu_index != current_submenu_index) {
        if (last_submenu_index >= 0) {
            const int prev_yPos = submenuItemY(last_submenu_index);
            const bool prevBack = (last_submenu_index == active_submenu_size - 1);

            tft.fillRect(0, prev_yPos, tft.width(), 28, UI_BG);
            tft.setTextColor(UI_TEXT, UI_BG);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, UI_TEXT);
            tft.setCursor(30, prev_yPos);
            if (!prevBack) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[last_submenu_index]);
        }

        const int new_yPos = submenuItemY(current_submenu_index);
        const bool newBack = (current_submenu_index == active_submenu_size - 1);

        tft.fillRect(0, new_yPos, tft.width(), 28, UI_BG);
        tft.setTextColor(UI_ICON, UI_BG);
        tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, UI_ICON);
        tft.setCursor(30, new_yPos);
        if (!newBack) {
            tft.print("| ");
        }
        tft.print(active_submenu_items[current_submenu_index]);

        last_submenu_index = current_submenu_index;
    }

    setStatusBarHeight(PUEO_STATUS_SHORT);  // a list, whose first row is at y=30
    drawStatusBar(currentBatteryVoltage, true);
}

void displayPagedSubmenu() {
    menu_initialized = false;
    last_menu_index = -1;

    const int featureCount = pagedFeatureCount();
    tft.setTextFont(2);
    tft.setTextSize(1);

    if (!submenu_initialized) {
        tft.fillScreen(UI_BG);
        for (int i = 0; i < featureCount; i++) {
            const int yPos = 30 + i * 30;
            tft.setTextColor(UI_TEXT, UI_BG);
            tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, UI_TEXT);
            tft.setCursor(30, yPos);
            tft.print("| ");
            tft.print(active_submenu_items[i]);
        }
        drawPagedFooterButtons();
        submenu_initialized = true;
        last_submenu_index = -1;
        s_pagedFooterFocus = -1;
    }

    if (last_submenu_index != current_submenu_index) {
        if (last_submenu_index >= 0 && last_submenu_index < featureCount) {
            const int prev_yPos = 30 + last_submenu_index * 30;
            tft.setTextColor(UI_TEXT, UI_BG);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, UI_TEXT);
            tft.setCursor(30, prev_yPos);
            tft.print("| ");
            tft.print(active_submenu_items[last_submenu_index]);
        }

        if (current_submenu_index >= 0 && current_submenu_index < featureCount) {
            const int new_yPos = 30 + current_submenu_index * 30;
            tft.setTextColor(UI_ICON, UI_BG);
            tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, UI_ICON);
            tft.setCursor(30, new_yPos);
            tft.print("| ");
            tft.print(active_submenu_items[current_submenu_index]);
            s_pagedFooterFocus = -1;
        } else if (current_submenu_index == pagedBackBtnIndex()) {
            s_pagedFooterFocus = 0;
        } else if (current_submenu_index == pagedPageBtnIndex()) {
            s_pagedFooterFocus = 1;
        } else {
            s_pagedFooterFocus = -1;
        }

        drawPagedFooterButtons();
        last_submenu_index = current_submenu_index;
    }

    setStatusBarHeight(PUEO_STATUS_SHORT);  // a list: first row is at y=30
    drawStatusBar(currentBatteryVoltage, true);
}

void displayOtherMenuGrid() {
    applyThemeToPalette(settings().theme);

    submenu_initialized = false;
    last_submenu_index = -1;
    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);

    if (!other_menu_grid_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_FG);
            tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_LINE);
            drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                             y_position + TILE_ICON_DY, other_submenu_icons[i],
                             16, 16, UI_ICON, PUEO_TILE_ICON / 16);

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(other_submenu_items[i]);
            int textX = x_position + (TILE_W - textWidth) / 2;
            int textY = y_position + TILE_TEXT_DY;
            tft.setCursor(textX, textY);
            tft.print(other_submenu_items[i]);
        }

        other_menu_grid_initialized = true;
        last_other_menu_index = -1;
    }

    if (last_other_menu_index != current_submenu_index) {
        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_other_menu_index) {
                tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_FG);
                tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_LINE);
                tft.setTextColor(UI_TEXT, UI_FG);
                drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                                 y_position + TILE_ICON_DY,
                                 other_submenu_icons[last_other_menu_index],
                                 16, 16, UI_ICON, PUEO_TILE_ICON / 16);
                int textWidth = tft.textWidth(other_submenu_items[last_other_menu_index]);
                int textX = x_position + (TILE_W - textWidth) / 2;
                int textY = y_position + TILE_TEXT_DY;
                tft.setCursor(textX, textY);
                tft.print(other_submenu_items[last_other_menu_index]);
            }
        }

        int column = current_submenu_index % OTHER_GRID_COLS;
        int row = current_submenu_index / OTHER_GRID_COLS;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        /* Filled, to match the main menu -- same tile, same size, and it
         * had the same problem. See the note in displayMenu(). */
        tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_ICON);
        tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_ICON);

        tft.setTextColor(UI_BG, UI_ICON);
        drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                         y_position + TILE_ICON_DY,
                         other_submenu_icons[current_submenu_index],
                         16, 16, UI_BG, PUEO_TILE_ICON / 16);
        int textWidth = tft.textWidth(other_submenu_items[current_submenu_index]);
        int textX = x_position + (TILE_W - textWidth) / 2;
        int textY = y_position + TILE_TEXT_DY;
        tft.setCursor(textX, textY);
        tft.print(other_submenu_items[current_submenu_index]);

        last_other_menu_index = current_submenu_index;
    }

    setStatusBarHeight(PUEO_STATUS_TALL);  // a tile grid, same 24 px of slack
    drawStatusBar(currentBatteryVoltage, true);
}


void displayMenu() {

  setTouchButtonInputEnabled(false);
  applyThemeToPalette(settings().theme);

const uint16_t icon_colors[NUM_MENU_ITEMS] = {
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON
};

    submenu_initialized = false;
    last_submenu_index = -1;
    other_menu_grid_initialized = false;
    last_other_menu_index = -1;
    tft.setTextFont(2);

    if (!menu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_FG);
            tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_LINE);
                drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                                 y_position + TILE_ICON_DY, bitmap_icons[i],
                                 16, 16, icon_colors[i], PUEO_TILE_ICON / 16);

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(menu_items[i]);
            int textX = x_position + (TILE_W - textWidth) / 2;
            int textY = y_position + TILE_TEXT_DY;
            tft.setCursor(textX, textY);
            tft.print(menu_items[i]);
        }
        menu_initialized = true;
        last_menu_index = -1;
    }

    if (last_menu_index != current_menu_index) {
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_menu_index) {
                tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_FG);
                tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_LINE);
                tft.setTextColor(UI_TEXT, UI_FG);
                    drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                                     y_position + TILE_ICON_DY, bitmap_icons[last_menu_index],
                                     16, 16, icon_colors[last_menu_index], PUEO_TILE_ICON / 16);
                int textWidth = tft.textWidth(menu_items[last_menu_index]);
                int textX = x_position + (TILE_W - textWidth) / 2;
                int textY = y_position + TILE_TEXT_DY;
                tft.setCursor(textX, textY);
                tft.print(menu_items[last_menu_index]);
            }
        }

        int column = current_menu_index / 4;
        int row = current_menu_index % 4;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        /* A selected tile is FILLED with the accent and its contents drop to
         * the background colour, rather than only swapping a hairline border.
         *
         * It had nowhere else to go. Every entry in the icon tables is
         * already UI_ICON and SELECTED_ICON_COLOR was defined as UI_ICON
         * again, so the icon was identical selected or not; the whole cue was
         * a 1 px outline and the label going white to accent, on a 100x60
         * tile. Filling flips about half the tile's area instead, which is
         * what carries at arm's length and in sunlight.
         *
         * The label goes dark rather than white because dark on accent is
         * about 6.4:1 and white on accent is 2.6:1. */
        tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_ICON);
        tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_ICON);

        tft.setTextColor(UI_BG, UI_ICON);
            drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                             y_position + TILE_ICON_DY, bitmap_icons[current_menu_index],
                             16, 16, UI_BG, PUEO_TILE_ICON / 16);
        int textWidth = tft.textWidth(menu_items[current_menu_index]);
        int textX = x_position + (TILE_W - textWidth) / 2;
        int textY = y_position + TILE_TEXT_DY;
        tft.setCursor(textX, textY);
        tft.print(menu_items[current_menu_index]);

        last_menu_index = current_menu_index;
    }
    setStatusBarHeight(PUEO_STATUS_TALL);  // the one screen with room above its tiles
    drawStatusBar(currentBatteryVoltage, true);
}

void handleWiFiSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(70);

        // Footer: Next / Prev
        if (current_submenu_index == pagedPageBtnIndex()) {
            wifi_submenu_page = (wifi_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        // Footer: Back to Main Menu
        if (current_submenu_index == pagedBackBtnIndex()) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            wifi_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PacketMonitor::ptmSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                PacketMonitor::ptmLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BeaconSpammer::beaconSpamSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BeaconSpammer::beaconSpamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Deauther::deautherSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                Deauther::deautherLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProbeRequestFlood::probeRequestFloodSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                ProbeRequestFlood::probeRequestFloodLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            DeauthDetect::deauthdetectSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                DeauthDetect::deauthdetectLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WifiScan::wifiscanSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                WifiScan::wifiscanLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 0) {
            current_submenu_index = 6;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            CaptivePortal::cportalSetup();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                current_submenu_index = 6;
                in_sub_menu = true;
                CaptivePortal::cportalLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 1) {
            current_submenu_index = 7;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            HiddenSsidReveal::hiddenSsidSetup();
            while (current_submenu_index == 7 && !feature_exit_requested) {
                current_submenu_index = 7;
                in_sub_menu = true;
                HiddenSsidReveal::hiddenSsidLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 2) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WpsScanner::wpsScannerSetup();
            while (wifi_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                WpsScanner::wpsScannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 3) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ArpScanner::arpScannerSetup();
            while (wifi_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                ArpScanner::arpScannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 4) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            KarmaAttack::karmaSetup();
            while (wifi_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                KarmaAttack::karmaLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        layoutPagedFooterButtons();
        const int footerHit = FeatureUI::hit(s_pagedFooterBtns, 2, x, y);
        if (footerHit == 0) {
            // Left: Main Menu
            current_submenu_index = pagedBackBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            wifi_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }
        if (footerHit == 1) {
            // Right: Next / Prev page
            current_submenu_index = pagedPageBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            wifi_submenu_page = (wifi_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        const int featureCount = wifiFeatureCount();
        for (int i = 0; i < featureCount; i++) {
            int yPos = 30 + i * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (wifi_submenu_page == 0 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PacketMonitor::ptmSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        PacketMonitor::ptmLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BeaconSpammer::beaconSpamSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BeaconSpammer::beaconSpamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Deauther::deautherSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        Deauther::deautherLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProbeRequestFlood::probeRequestFloodSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        ProbeRequestFlood::probeRequestFloodLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    DeauthDetect::deauthdetectSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        DeauthDetect::deauthdetectLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WifiScan::wifiscanSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        WifiScan::wifiscanLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 0) {
                    current_submenu_index = 6;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    CaptivePortal::cportalSetup();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        current_submenu_index = 6;
                        in_sub_menu = true;
                        CaptivePortal::cportalLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 1) {
                    current_submenu_index = 7;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    HiddenSsidReveal::hiddenSsidSetup();
                    while (current_submenu_index == 7 && !feature_exit_requested) {
                        current_submenu_index = 7;
                        in_sub_menu = true;
                        HiddenSsidReveal::hiddenSsidLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 2) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WpsScanner::wpsScannerSetup();
                    while (wifi_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        WpsScanner::wpsScannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 3) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ArpScanner::arpScannerSetup();
                    while (wifi_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        ArpScanner::arpScannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 4) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    KarmaAttack::karmaSetup();
                    while (wifi_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        KarmaAttack::karmaLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleBluetoothSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(70);

        if (current_submenu_index == pagedPageBtnIndex()) {
            bluetooth_submenu_page = (bluetooth_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        if (current_submenu_index == pagedBackBtnIndex()) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            bluetooth_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleJammer::blejamSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleJammer::blejamLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleJammer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSpoofer::spooferSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BleSpoofer::spooferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleSpoofer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SourApple::sourappleSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                SourApple::sourappleLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            SourApple::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            AirTagSpoofer::airTagSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                AirTagSpoofer::airTagLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            AirTagSpoofer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            AirTagSniffer::airTagSnifferSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                AirTagSniffer::airTagSnifferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            AirTagSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSniffer::blesnifferSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                BleSniffer::blesnifferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 1 && current_submenu_index == 0) {
            current_submenu_index = 6;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleScan::bleScanSetup();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 6;
                in_sub_menu = true;
                BleScan::bleScanLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleScan::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (bluetooth_submenu_page == 1 && current_submenu_index == 1) {
            runBleDuckyFeature();
        }

        if (bluetooth_submenu_page == 1 && current_submenu_index == 2) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSkimmer::bleSkimmerSetup();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleSkimmer::bleSkimmerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            BleSkimmer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }





        if (bluetooth_submenu_page == 1 && current_submenu_index == 3) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            TrackerHunt::setup();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                TrackerHunt::loop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            TrackerHunt::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 1 && current_submenu_index == 4) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            FastPairScan::fastPairSetup();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                FastPairScan::fastPairLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            FastPairScan::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        layoutPagedFooterButtons();
        const int footerHit = FeatureUI::hit(s_pagedFooterBtns, 2, x, y);
        if (footerHit == 0) {
            current_submenu_index = pagedBackBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            bluetooth_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }
        if (footerHit == 1) {
            current_submenu_index = pagedPageBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            bluetooth_submenu_page = (bluetooth_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        const int featureCount = bluetoothFeatureCount();
        for (int i = 0; i < featureCount; i++) {
            int yPos = 30 + i * 30;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 30;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (bluetooth_submenu_page == 0 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleJammer::blejamSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleJammer::blejamLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleJammer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSpoofer::spooferSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BleSpoofer::spooferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSpoofer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SourApple::sourappleSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SourApple::sourappleLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    SourApple::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    AirTagSpoofer::airTagSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        AirTagSpoofer::airTagLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    AirTagSpoofer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    AirTagSniffer::airTagSnifferSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        AirTagSniffer::airTagSnifferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    AirTagSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSniffer::blesnifferSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        BleSniffer::blesnifferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 0) {
                    current_submenu_index = 6;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleScan::bleScanSetup();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 6;
                        in_sub_menu = true;
                        BleScan::bleScanLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleScan::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 1) {
                    runBleDuckyFeature();
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 2) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSkimmer::bleSkimmerSetup();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleSkimmer::bleSkimmerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSkimmer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 3) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    TrackerHunt::setup();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        TrackerHunt::loop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    TrackerHunt::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 4) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    FastPairScan::fastPairSetup();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        FastPairScan::fastPairLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    FastPairScan::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleNRFSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 6) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Scanner::scannerSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                Scanner::scannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            Scanner::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProtoKill::prokillSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                ProtoKill::prokillLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            ProtoKill::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            EsbSniffer::esbSnifferSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                EsbSniffer::esbSnifferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            EsbSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            EsbReplay::esbReplaySetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                EsbReplay::esbReplayLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            EsbReplay::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            MouseJack::mouseJackSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                MouseJack::mouseJackLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            MouseJack::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            MouseJackInject::mouseJackInjectSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                MouseJackInject::mouseJackInjectLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            MouseJackInject::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = submenuItemY(i);

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 28;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 6) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Scanner::scannerSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        Scanner::scannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    Scanner::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProtoKill::prokillSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        ProtoKill::prokillLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    ProtoKill::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    EsbSniffer::esbSnifferSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        EsbSniffer::esbSnifferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    EsbSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    EsbReplay::esbReplaySetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        EsbReplay::esbReplayLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    EsbReplay::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    MouseJack::mouseJackSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        MouseJack::mouseJackLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    MouseJack::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    MouseJackInject::mouseJackInjectSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        MouseJackInject::mouseJackInjectLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    MouseJackInject::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleSubGHzSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 5) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            replayat::ReplayAttackSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                replayat::ReplayAttackLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            subjammer::subjammerSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                subjammer::subjammerLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SubBrute::subBruteSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                SubBrute::subBruteLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            jammingdetector::Setup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                jammingdetector::Loop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SavedProfile::saveSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                SavedProfile::saveLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = submenuItemY(i);

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 28;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 5) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    replayat::ReplayAttackSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        replayat::ReplayAttackLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    subjammer::subjammerSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        subjammer::subjammerLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SubBrute::subBruteSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SubBrute::subBruteLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    jammingdetector::Setup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        jammingdetector::Loop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SavedProfile::saveSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        SavedProfile::saveLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

constexpr int TOOLS_IDX_TERMINAL = 0;
constexpr int TOOLS_IDX_UPDATE   = 1;
constexpr int TOOLS_IDX_TOUCH    = 2;
constexpr int TOOLS_IDX_SD_FILES = 3;
constexpr int TOOLS_IDX_SETTINGS = -1;
constexpr int TOOLS_IDX_BACK     = 4;

static void runToolsFeatureExitCleanup() {
    in_sub_menu = true;
    is_main_menu = false;
    submenu_initialized = false;
    feature_active = false;
    feature_exit_requested = false;
    setTouchButtonInputEnabled(false);
    setTouchNavLabels(nullptr, nullptr, nullptr, nullptr, nullptr);
    resetTouchNavHeldState();
    displaySubmenu();
    delay(200);
    while (isButtonPressed(BTN_SELECT)) {
    }
}

static void runToolsFeature(int idx, void (*setupFn)(), void (*loopFn)()) {
    const bool useTouchNav = (idx != TOOLS_IDX_TOUCH);
    current_submenu_index = idx;
    in_sub_menu = true;
    feature_active = true;
    feature_exit_requested = false;
    if (useTouchNav) {
        setTouchButtonInputEnabled(true);
    }
    setupFn();
    while (current_submenu_index == idx && !feature_exit_requested) {
        current_submenu_index = idx;
        in_sub_menu = true;
        loopFn();
        if (feature_exit_requested) {
            break;
        }
        if (!useTouchNav && isButtonPressed(BTN_SELECT)) {
            break;
        }
    }
    runToolsFeatureExitCleanup();
}

static void launchToolsFeature(int idx) {
    switch (idx) {
        case TOOLS_IDX_TERMINAL:
            runToolsFeature(idx, Terminal::terminalSetup, Terminal::terminalLoop);
            break;
        case TOOLS_IDX_UPDATE:
            runToolsFeature(idx, FirmwareUpdate::updateSetup, FirmwareUpdate::updateLoop);
            break;
        case TOOLS_IDX_TOUCH:
            runToolsFeature(idx, TouchCalib::setup, TouchCalib::loop);
            break;
        case TOOLS_IDX_SD_FILES:
            runToolsFeature(idx, SdFileManager::setup, SdFileManager::loop);
            break;
        default:
            break;
    }
}

void handleToolsSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == TOOLS_IDX_BACK) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        launchToolsFeature(current_submenu_index);
        return;
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) {
            return;
        }

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = submenuItemY(i);

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 220;
            int button_y2 = yPos + 28;

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == TOOLS_IDX_BACK) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else {
                    launchToolsFeature(current_submenu_index);
                }
                break;
            }
        }
    }
}

static void otherDismissPlaceholder() {
    delay(25);
    while (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
        delay(5);
    }
    while (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT)) {
        int x = 0, y = 0;
        if (!readTouchXYDismiss(x, y) && !readTouchXY(x, y)) {
            delay(12);
            continue;
        }
        if (isNotificationVisible()) {
            NotificationAction a = notificationHandleTouch(x, y);
            if (a != NotificationAction::None) {
                break;
            }
            hideNotification();
        }
        break;
    }
    if (in_sub_menu) {
        submenu_initialized = false;
        if (current_menu_index == 2 && other_layer == OTHER_LAYER_HOME) {
            other_menu_grid_initialized = false;
            last_other_menu_index = -1;
        }
        displaySubmenu();
    }
}

static void otherRfidReturnGuard() {
    delay(120);
    for (int i = 0; i < 120; i++) {
        if (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT) &&
            !isButtonPressed(BTN_RIGHT) && !isButtonPressed(BTN_UP) &&
            !isButtonPressed(BTN_DOWN) && !isTouchDownDismiss()) {
            break;
        }
        delay(5);
    }
    delay(120);
}

static void otherRfidPlaceholderAction(int idx) {
    feature_active = true;
    /* All of RFID/NFC, not one entry of it. A PN532 reads a card by
     * energising a 13.56 MHz field and waiting for the card to answer, so
     * "read" transmits exactly as much as "clone" does. Gated here because
     * every entry in that menu comes through this one function. */
    if (Stealth::refuse("RFID/NFC")) { feature_active = false; return; }
    if (!RfidNfc::begin()) {
        showNotification("RFID/NFC", "PN532 not found. Check SPI wiring/pins.");
        otherDismissPlaceholder();
        feature_active = false;
        return;
    }
    feature_exit_requested = false;
    setTouchButtonInputEnabled(true);
    for (;;) {
        RfidNfc::clearSessionRetry();
        switch (idx) {
            case 0:
                RfidNfc::sessionCardReader();
                break;
            case 1:
                RfidNfc::sessionClone();
                break;
            case 2:
                RfidNfc::sessionErase();
                break;
            case 3:
                RfidNfc::sessionDump();
                break;
            case 4:
                RfidNfc::sessionDecodeAccess();
                break;
            case 5:
                RfidNfc::sessionJamReader();
                break;
            case 6:
                RfidNfc::sessionTagDisrupt();
                break;
            case 7:
                RfidNfc::sessionDisruptEmulate();
                break;
            default:
                feature_active = false;
                restoreSdAfterSharedSpi();
                return;
        }
        if (feature_exit_requested || !RfidNfc::consumeSessionRetry()) {
            break;
        }
        feature_exit_requested = false;
    }
    restoreSdAfterSharedSpi();
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

static void otherGpsPlaceholderAction(int idx) {
    feature_active = true;
    if (idx == 0) {
        feature_exit_requested = false;
        setTouchButtonInputEnabled(true);
        for (;;) {
            GpsWardriver::clearSessionRetry();
            GpsWardriver::session();
            if (feature_exit_requested || !GpsWardriver::consumeSessionRetry()) {
                break;
            }
            feature_exit_requested = false;
        }
        setTouchButtonInputEnabled(false);
    } else {
        switch (idx) {
            case 1:
                GpsSatelliteScanner::session();
                break;
            default:
                feature_active = false;
                return;
        }
    }
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

void handleOtherSubmenuButtons() {
    if (other_layer == OTHER_LAYER_HOME) {
        const int og_rows =
            (other_NUM_SUBMENU_ITEMS + OTHER_GRID_COLS - 1) / OTHER_GRID_COLS;

        if (isButtonPressed(BTN_UP)) {
            int row = current_submenu_index / OTHER_GRID_COLS;
            if (row > 0) {
                current_submenu_index -= OTHER_GRID_COLS;
            } else {
                current_submenu_index += OTHER_GRID_COLS * (og_rows - 1);
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN)) {
            int row = current_submenu_index / OTHER_GRID_COLS;
            if (row < og_rows - 1) {
                current_submenu_index += OTHER_GRID_COLS;
            } else {
                current_submenu_index -= OTHER_GRID_COLS * (og_rows - 1);
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT)) {
            int col = current_submenu_index % OTHER_GRID_COLS;
            if (col > 0) {
                current_submenu_index--;
            } else {
                current_submenu_index++;
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT)) {
            int col = current_submenu_index % OTHER_GRID_COLS;
            if (col < OTHER_GRID_COLS - 1) {
                current_submenu_index++;
            } else {
                current_submenu_index--;
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }
    } else {
        if (isButtonPressed(BTN_UP)) {
            current_submenu_index =
                (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN)) {
            current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (other_layer == OTHER_LAYER_HOME) {
            if (current_submenu_index == other_NUM_SUBMENU_ITEMS - 1) {
                in_sub_menu = false;
                feature_active = false;
                feature_exit_requested = false;
                displayMenu();
                handleButtons();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_RFID;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_GPS;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 2) {
                feature_active = true;
                feature_exit_requested = false;
                Spotter::spotterSetup();
                while (!feature_exit_requested) {
                    Spotter::spotterLoop();
                    if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                        break;
                    }
                }
                Spotter::exit();
                feature_active = false;
                feature_exit_requested = false;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                submenu_initialized = false;
                displaySubmenu();
                delay(200);
            } else if (current_submenu_index == 3) {
                feature_active = true;
                feature_exit_requested = false;
                DroneScan::setup();
                while (!feature_exit_requested) {
                    DroneScan::loop();
                    if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                        break;
                    }
                }
                DroneScan::exit();
                feature_active = false;
                feature_exit_requested = false;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                submenu_initialized = false;
                displaySubmenu();
                delay(200);
            }
        } else if (other_layer == OTHER_LAYER_RFID) {
            if (current_submenu_index == rfid_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherRfidPlaceholderAction(current_submenu_index);
            }
        } else if (other_layer == OTHER_LAYER_GPS) {
            if (current_submenu_index == gps_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherGpsPlaceholderAction(current_submenu_index);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        int touched_slot = -1;
        if (other_layer == OTHER_LAYER_HOME) {
            for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
                int column = i % OTHER_GRID_COLS;
                int row = i / OTHER_GRID_COLS;
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;
                int button_x1 = x_position;
                int button_y1 = y_position;
                /* TILE_W/TILE_H, not 100x60. Those are the 2.8" tile size,
                 * and on the 3.5" the tiles are 145x92 -- so two thirds of
                 * every tile in this grid did not respond to a tap. It
                 * survived the panel sweep by looking like a hit box rather
                 * than like a dimension. */
                int button_x2 = x_position + TILE_W;
                int button_y2 = y_position + TILE_H;
                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    touched_slot = i;
                    break;
                }
            }
        } else {
            for (int i = 0; i < active_submenu_size; i++) {
                int yPos = submenuItemY(i);

                int button_x1 = 10;
                int button_y1 = yPos;
                int button_x2 = 220;
                int button_y2 = yPos + 28;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    touched_slot = i;
                    break;
                }
            }
        }

        if (touched_slot < 0) {
            return;
        }

        current_submenu_index = touched_slot;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);

        if (other_layer == OTHER_LAYER_HOME) {
            if (current_submenu_index == other_NUM_SUBMENU_ITEMS - 1) {
                in_sub_menu = false;
                feature_active = false;
                feature_exit_requested = false;
                displayMenu();
                handleButtons();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_RFID;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_GPS;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 2) {
                feature_active = true;
                feature_exit_requested = false;
                Spotter::spotterSetup();
                while (!feature_exit_requested) {
                    Spotter::spotterLoop();
                    if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                        break;
                    }
                }
                Spotter::exit();
                feature_active = false;
                feature_exit_requested = false;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                submenu_initialized = false;
                displaySubmenu();
                delay(200);
            } else if (current_submenu_index == 3) {
                feature_active = true;
                feature_exit_requested = false;
                DroneScan::setup();
                while (!feature_exit_requested) {
                    DroneScan::loop();
                    if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                        break;
                    }
                }
                DroneScan::exit();
                feature_active = false;
                feature_exit_requested = false;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                submenu_initialized = false;
                displaySubmenu();
                delay(200);
            }
        } else if (other_layer == OTHER_LAYER_RFID) {
            if (current_submenu_index == rfid_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherRfidPlaceholderAction(current_submenu_index);
            }
        } else if (other_layer == OTHER_LAYER_GPS) {
            if (current_submenu_index == gps_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherGpsPlaceholderAction(current_submenu_index);
            }
        }
    }
}

/* About, in two pages.
 *
 * It was one page of text and no artwork at all. The owl is a fixed 200x200
 * bitmap and drawBitmap does not scale, so on a 240x320 panel it does not
 * share a page with four rows of credits -- there is nowhere for them to go.
 * Two pages fits it on both panels instead of picking one to go without.
 *
 * Page 1 is the mark and who made it. Page 2 is the detail: board, contact,
 * where the source is. Tap or SELECT moves on, and again leaves.
 */
void drawAboutPage(int page) {
  tft.fillScreen(UI_BG);
  setStatusBarHeight(PUEO_STATUS_SHORT);
  drawStatusBar(readBatteryVoltage(), true);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  if (page == 0) {
    /* The mark, the name, what it is, who made it.
     *
     * This page used to print upstream's details and nothing else: the
     * obfuscated strings in shared.h decode to ESP32-DIV, CiferTech, and
     * their email, GitHub and site. Reasonable when this was their sketch;
     * wrong on a fork that never rebranded the page. The credit to them is
     * on page two, where it belongs and where it is a credit rather than
     * the only thing the screen says. */
    int y = 40;
    const int lx = (PUEO_SCREEN_W - PUEO_LOGO_W) / 2;
    const int ly = 24 + ((PUEO_SCREEN_H - 24) - PUEO_LOGO_H) / 2 - 22;
    tft.drawBitmap(lx, ly, PUEO_LOGO_BITMAP,
                   PUEO_LOGO_W, PUEO_LOGO_H, UI_ICON);
    y = ly + PUEO_LOGO_H + 14;

    /* No name line: the artwork carries the wordmark, which is what
     * PUEO_LOGO_HAS_WORDMARK records and why displayLogo() drops its own. */
    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, UI_BG);
    tft.drawCentreString(PUEO_TAGLINE, PUEO_SCREEN_W / 2, y, 1);
    y += 14;
    tft.setTextColor(UI_DIM_TEXT, UI_BG);
    tft.drawCentreString("by " PUEO_AUTHOR "  -  " PUEO_VERSION,
                         PUEO_SCREEN_W / 2, y, 1);

    tft.setTextColor(UI_DIM_TEXT, UI_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setCursor(16, PUEO_SCREEN_H - 20);
    tft.print("SELECT / tap for details");
    return;
  }

  tft.setTextFont(2);
  tft.setTextColor(UI_ICON, UI_BG);
  tft.setCursor(16, 40);
  tft.print(PUEO_NAME " " PUEO_VERSION);

  tft.setTextFont(1);
  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(16, 62);
  tft.print(PUEO_TAGLINE);

  tft.drawFastHLine(12, 78, PUEO_SCREEN_W - 24, UI_LINE);

  const int xLabel = 16;
  const int xValue = 76;
  const int step = 20;
  int y = 94;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("Board");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tft.print(ESP32DIV_BOARD_NAME);
  y += step;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("By");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tft.print(PUEO_AUTHOR);
  y += step;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("Web");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tft.print(PUEO_URL);
  y += step + 6;

  /* The credit back to the project this was forked from.
   *
   * ESP32-DIV is MIT, and the licence's requirement is the notice in
   * LICENSE, which is kept. This is not that -- it is here because the code
   * came from somewhere and saying so costs nothing.
   *
   * Their project and repository, not their personal email: an address on a
   * fork's About screen points support at someone who did not ship it. */
  tft.drawFastHLine(12, y, PUEO_SCREEN_W - 24, UI_LINE);
  y += 12;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print(PUEO_UPSTREAM);
  y += 14;
  tft.setCursor(xLabel, y);
  tft.print(PUEO_UPSTREAM_URL);
  y += 14;
  tft.setCursor(xLabel, y);
  tft.print("forked at ");
  tft.print(ESP32DIV_VERSION);

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(16, PUEO_SCREEN_H - 20);
  tft.print("SELECT / tap to go back");
}

void handleAboutPage() {
  feature_active = true;
  feature_exit_requested = false;

  currentBatteryVoltage = readBatteryVoltage();
  int page = 0;
  drawAboutPage(page);

  while (!feature_exit_requested) {
    bool advance = false;

    if (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
      last_interaction_time = millis();
      advance = true;
    } else {
      int x, ty;
      if (readTouchXY(x, ty)) {
        last_interaction_time = millis();
        advance = true;
      }
    }

    if (advance) {
      delay(200);
      /* Wait for the release before deciding again, or one press walks
       * both pages and leaves. */
      while (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
        delay(10);
      }
      if (page == 0) {
        page = 1;
        drawAboutPage(page);
      } else {
        feature_exit_requested = true;
        break;
      }
    }

    delay(20);
  }

  feature_active = false;
  feature_exit_requested = false;
  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}


void handleSettingsSubmenuButtons() {

  feature_active = true;
  feature_exit_requested = false;

  AppSettingsUI::setup();
  while (!feature_exit_requested) {
    AppSettingsUI::loop();
  }

  feature_active = false;
  feature_exit_requested = false;

  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}

void handleButtons() {
    if (in_sub_menu) {
        switch (current_menu_index) {

            case 0: handleWiFiSubmenuButtons(); break;
            case 1: handleNRFSubmenuButtons(); break;
            case 2: handleOtherSubmenuButtons(); break;
            case 3: /* Settings: full-screen AppSettings, not list submenu */ break;
            case 4: handleBluetoothSubmenuButtons(); break;
            case 5: handleSubGHzSubmenuButtons(); break;
            case 6: handleToolsSubmenuButtons(); break;
            default: break;
        }
    } else {

        if (isButtonPressed(BTN_UP) && !is_main_menu) {
            current_menu_index--;
            if (current_menu_index < 0) {
                current_menu_index = NUM_MENU_ITEMS - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN) && !is_main_menu) {
            current_menu_index++;
            if (current_menu_index >= NUM_MENU_ITEMS) {
                current_menu_index = 0;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index >= 4) {
                current_menu_index = row;
            } else if (current_menu_index == 0) {
                current_menu_index = 3;
            } else {
                current_menu_index = row - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index < 4) {
                current_menu_index = row + 4;
            } else if (current_menu_index == 7) {
                current_menu_index = 0;
            } else {
                current_menu_index = row + 5;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_SELECT)) {
            last_interaction_time = millis();
            delay(200);

            if (current_menu_index == 3) {
                handleSettingsSubmenuButtons();
            } else if (current_menu_index == 7) {
                handleAboutPage();
            } else {
                updateActiveSubmenu();

                if (active_submenu_items && active_submenu_size > 0) {
                    current_submenu_index = 0;
                    if (current_menu_index == 2) {
                        other_layer = OTHER_LAYER_HOME;
                        other_menu_grid_initialized = false;
                        last_other_menu_index = -1;
                    }
                    in_sub_menu = true;
                    submenu_initialized = false;
                    displaySubmenu();
                }

                if (is_main_menu) {
                    is_main_menu = false;
                    displayMenu();
                } else {
                    is_main_menu = true;
                }
            }
        }

        static unsigned long lastTouchTime = 0;
        const unsigned long touchFeedbackDelay = 100;

        if (!feature_active && (millis() - lastTouchTime >= touchFeedbackDelay)) {
            int x, y;
            if (!readTouchXY(x, y)) { return; }
            delay(10);
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
                int column = i / 4;
                int row = i % 4;
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;

                int button_x1 = x_position;
                int button_y1 = y_position;
                /* TILE_W/TILE_H, not 100x60. Those are the 2.8" tile size,
                 * and on the 3.5" the tiles are 145x92 -- so two thirds of
                 * every tile in this grid did not respond to a tap. It
                 * survived the panel sweep by looking like a hit box rather
                 * than like a dimension. */
                int button_x2 = x_position + TILE_W;
                int button_y2 = y_position + TILE_H;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    current_menu_index = i;
                    last_interaction_time = millis();
                    displayMenu();

                    unsigned long startTime = millis();
                    while (isTouchDownDismiss() && (millis() - startTime < touchFeedbackDelay)) {
                        delay(10);
                    }

                    if (isTouchDownDismiss()) {

                        if (current_menu_index == 3) {
                            handleSettingsSubmenuButtons();
                        } else if (current_menu_index == 7) {
                            handleAboutPage();
                        } else {
                            updateActiveSubmenu();

                            if (active_submenu_items && active_submenu_size > 0) {
                                current_submenu_index = 0;
                                if (current_menu_index == 2) {
                                    other_layer = OTHER_LAYER_HOME;
                                    other_menu_grid_initialized = false;
                                    last_other_menu_index = -1;
                                }
                                in_sub_menu = true;
                                submenu_initialized = false;
                                displaySubmenu();
                            } else {
                                if (is_main_menu) {
                                    is_main_menu = false;
                                    displayMenu();
                                } else {
                                    is_main_menu = true;
                                }
                            }
                        }
                    }
                    delay(200);
                    break;
                }
            }
        }
    }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("[boot] start");

#if !BOARD_HAS_ESP32S3
  // Weak USB / backlight load can brownout classic ESP32 during intro.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif

  tft.init();
  tft.setRotation(TFT_ROTATION);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(BACKLIGHT_PIN, PWM_CHANNEL);
  setBrightness(80);

  applyThemeToPalette(settings().theme);

  tft.fillScreen(TFT_BLACK);

  /* Boot splash, and which mark gets the longer beat.
   *
   * Nothing here is waiting on init -- both are blocking delays, and the
   * split was never chosen. The skull is upstream's animation at its
   * upstream timing, 2 repeats x 10 frames x 100 ms, and the owl got the
   * 500 ms that displayLogo() happened to be called with. The fork spent
   * four times as long on the inherited mark as on its own.
   *
   * One pass of the skull is still a nod to where this came from; the owl
   * now holds long enough to read the name under it. Total splash goes from
   * 2.5 s to 2.2 s, so this costs nothing at boot. */
  loading(100, UI_ICON, 0, 0, PUEO_BOOT_SKULL_REPEATS, true);

  tft.fillScreen(TFT_BLACK);
  displayLogo(TFT_WHITE, PUEO_BOOT_LOGO_MS);

  initSDCard();

#if BOARD_HAS_ESP32S3
  settingsLoad();
#else
  /* Settings, at last.
   *
   * This branch applied board touch defaults, printed "settings defaults
   * (v1, SD deferred)", and called nothing. Deferred turned out to mean
   * never: nothing anywhere else called settingsLoad(), so every setting
   * this firmware has offered was written to the card by Save and read back
   * by no one, on every boot since the fork. Brightness, theme, accent, auto
   * scan -- all of them reset, with a perfectly correct file sitting on the
   * card.
   *
   * It was deferred for a real reason: a boot-time SD.begin() right after
   * tft.init() was rebooting the device. The specific cause of that,
   * gpio_reset_pin() on the shared SPI pins, is compiled out on this board
   * now, and every SD feature since has mounted through this same path.
   *
   * The defaults are still applied first, so a card that will not mount --
   * or is not there -- leaves the device exactly where it used to be. */
  settingsApplyBoardTouchDefaults();
  settingsLoad();
  /* Says which of the four it was. "loaded from SD" used to print for a card
   * with no settings.json on it, which is the reassuring half of a message
   * that had not read anything. */
  Serial.printf("[boot] settings: %s\n", settingsLastLoadText());
#endif
  applyThemeToPalette(settings().theme);
  setBrightness(settings().brightness);

#if HAS_PCF8574_BUTTONS
  if (!initPcf8574Buttons()) {
    Serial.println("PCF8574 buttons unavailable");
  }
#else
  Serial.println("PCF8574 buttons disabled for this board");
#endif

#if BOARD_HAS_ESP32S3
  ensureBleStackReady();
#else
  // Classic ESP32: defer NimBLE; also skip boot-time WiFi scan task (heap/WDT).
  Serial.println("[boot] BLE/WiFi-bg deferred (v1)");
#endif

#if FEATURE_BLE_DUCKY
  Ducky::setup();
#endif

#if BOARD_HAS_ESP32S3
  WifiScan::startBackgroundScanner();
  BleScan::startBackgroundScanner();
  startStatusBarTask();
#else
  // Keep boot lightweight on ESP32 — status bar updates from loop() instead.
#endif

  menu_initialized = false;
  currentBatteryVoltage = readBatteryVoltage();

  /* Touch comes up before the lock rather than after it, because the lock
   * screen is a keyboard and a keyboard you cannot touch is a device you
   * cannot get into. Nothing else here needed it early. */
  setupTouchscreen();


  /* Blocks until the password is right, and returns immediately when none
   * is set. Everything above this point has already run: this hides the
   * menu, not the boot. See BootLock.h for what that is worth. */
  BootLock::require();

  displayMenu();
  drawStatusBar(currentBatteryVoltage, false);

  last_interaction_time = millis();
  Serial.println("[boot] ready");
}

void loop() {
  /* TEMPORARY bring-up aid -- remove before committing. Runs the touch
   * calibrator once at startup, because a panel whose touch is wrong
   * enough to need calibrating is also too wrong to navigate to the menu
   * entry that starts it. */
#ifndef PUEO_FORCE_TOUCH_CALIB
#define PUEO_FORCE_TOUCH_CALIB 0
#endif
#if PUEO_FORCE_TOUCH_CALIB
  {
    static bool s_forcedCalibDone = false;
    if (!s_forcedCalibDone) {
      s_forcedCalibDone = true;
      feature_active = true;
      feature_exit_requested = false;
      TouchCalib::setup();
      while (!feature_exit_requested) {
        TouchCalib::loop();
        delay(10);
      }
      feature_active = false;
      feature_exit_requested = false;
      menu_initialized = false;
      displayMenu();
    }
  }
#endif
  applyThemeToPalette(settings().theme);
  handleButtons();
  updateStatusBar();
}
