#include "hud_engage.h"
#include <WiFi.h>
#include <esp_wifi.h>

static uint32_t s_frames = 0, s_deauth = 0, s_lastMs = 0;  // written in WiFi task, read in UI
static const int HOPS[] = { 1, 6, 11 };     // the busy 2.4 GHz channels
static int      s_hopIdx = 0, s_ch = 1;
static uint32_t s_hopT = 0;

// Promiscuous RX callback (WiFi task context). Count mgmt frames; flag deauth/disassoc.
static void rxcb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
  s_frames++;
  uint8_t subtype = (p->payload[0] >> 4) & 0x0F;      // frame-control subtype
  if (subtype == 0x0C || subtype == 0x0A) {           // deauthentication / disassociation
    s_deauth++;
    s_lastMs = millis();
  }
}

void hud_engage_enter() {
  s_frames = 0; s_deauth = 0; s_lastMs = 0; s_hopIdx = 0; s_ch = HOPS[0]; s_hopT = 0;
  WiFi.setBandMode(WIFI_BAND_MODE_2G_ONLY);
  esp_wifi_set_promiscuous(true);
  wifi_promiscuous_filter_t filt = {}; filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&rxcb);
  esp_wifi_set_channel(s_ch, WIFI_SECOND_CHAN_NONE);
}

void hud_engage_leave() {
  esp_wifi_set_promiscuous(false);
}

void hud_engage_tick(uint32_t now) {
  if (now - s_hopT > 300) {                           // hop channels so we hear all nets
    s_hopT = now;
    s_hopIdx = (s_hopIdx + 1) % (int)(sizeof(HOPS) / sizeof(HOPS[0]));
    s_ch = HOPS[s_hopIdx];
    esp_wifi_set_channel(s_ch, WIFI_SECOND_CHAN_NONE);
  }
}

uint32_t hud_engage_frames()  { return s_frames; }
uint32_t hud_engage_deauth()  { return s_deauth; }
uint32_t hud_engage_last_ms() { return s_lastMs; }
int      hud_engage_channel() { return s_ch; }
