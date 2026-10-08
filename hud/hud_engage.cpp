#include "hud_engage.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

static uint32_t s_frames = 0, s_deauth = 0, s_lastMs = 0;  // written in WiFi task, read in UI
static const int HOPS[] = { 1, 6, 11 };
static int      s_hopIdx = 0, s_ch = 1;
static uint32_t s_hopT = 0;

// ---- drone (Open Drone ID) tracking ----
#define DRONE_MAX  8
#define DRONE_AGE  12000
struct DroneRec { uint8_t mac[6]; char id[21]; double lat, lon, oplat, oplon; int8_t rssi; uint32_t last; bool loc, op; };
static DroneRec s_dr[DRONE_MAX];
static int      s_drN = 0;

static DroneRec* drone_get(const uint8_t* mac) {
  for (int i = 0; i < s_drN; i++) if (!memcmp(s_dr[i].mac, mac, 6)) return &s_dr[i];
  if (s_drN < DRONE_MAX) { DroneRec* r = &s_dr[s_drN++]; memset(r, 0, sizeof(*r)); memcpy(r->mac, mac, 6); return r; }
  return &s_dr[0];                        // full: reuse slot 0 (oldest-ish)
}

// Parse an ODID message pack (or single 25-byte message) into a drone record.
static void parse_odid(const uint8_t* od, int odlen, const uint8_t* mac, int8_t rssi) {
  if (odlen < 1) return;
  const uint8_t* msgs = od; int n = 1;
  if (((od[0] >> 4) & 0x0F) == 0xF) {     // message pack: [0xF..][size=25][count][msgs]
    if (odlen < 3) return;
    n = od[2]; msgs = od + 3;
  }
  DroneRec* r = drone_get(mac);
  r->rssi = rssi; r->last = millis();
  for (int m = 0; m < n; m++) {
    const uint8_t* M = msgs + m * 25;
    if ((int)((M - od) + 25) > odlen) break;
    int type = (M[0] >> 4) & 0x0F;
    if (type == 0) {                      // Basic ID: UAS ID at offset 2..21
      memcpy(r->id, M + 2, 20); r->id[20] = 0;
    } else if (type == 1) {               // Location: lat@5, lon@9 (int32 LE *1e7)
      int32_t la, lo; memcpy(&la, M + 5, 4); memcpy(&lo, M + 9, 4);
      r->lat = la * 1e-7; r->lon = lo * 1e-7; r->loc = true;
    } else if (type == 4) {               // System: operator lat@2, lon@6
      int32_t la, lo; memcpy(&la, M + 2, 4); memcpy(&lo, M + 6, 4);
      r->oplat = la * 1e-7; r->oplon = lo * 1e-7; r->op = true;
    }
  }
}

// Scan a beacon's vendor IEs for the ODID element (OUI FA-0B-BC, type 0x0D).
static void parse_beacon(const uint8_t* fr, int len, const uint8_t* mac, int8_t rssi) {
  int i = 36;                             // IEs start after the 24-byte hdr + 12 fixed params
  while (i + 2 <= len) {
    uint8_t id = fr[i], l = fr[i + 1];
    const uint8_t* d = fr + i + 2;
    if (i + 2 + l > len) break;
    if (id == 0xDD && l >= 6 && d[0] == 0xFA && d[1] == 0x0B && d[2] == 0xBC && d[3] == 0x0D)
      parse_odid(d + 5, l - 5, mac, rssi);   // d[4] = send counter, pack/msg follows
    i += 2 + l;
  }
}

static void rxcb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* fr = p->payload;
  int len = p->rx_ctrl.sig_len;
  s_frames++;
  uint8_t subtype = (fr[0] >> 4) & 0x0F;
  if (subtype == 0x0C || subtype == 0x0A) { s_deauth++; s_lastMs = millis(); }  // deauth/disassoc
  else if (subtype == 0x08 && len > 38)   parse_beacon(fr, len, fr + 10, p->rx_ctrl.rssi);  // beacon
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

void hud_engage_leave() { esp_wifi_set_promiscuous(false); }

void hud_engage_tick(uint32_t now) {
  // age out stale drones
  uint32_t t = millis();
  for (int i = 0; i < s_drN; ) {
    if (t - s_dr[i].last > DRONE_AGE) { s_dr[i] = s_dr[--s_drN]; }
    else i++;
  }
  if (now - s_hopT > 300) {
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
int      hud_engage_drone_count() { return s_drN; }
bool     hud_engage_drone(int i, DroneInfo* out) {
  if (i < 0 || i >= s_drN) return false;
  const DroneRec& r = s_dr[i];
  memcpy(out->id, r.id, sizeof(out->id));
  out->lat = r.lat; out->lon = r.lon; out->oplat = r.oplat; out->oplon = r.oplon;
  out->rssi = r.rssi; out->loc = r.loc; out->op = r.op;
  return true;
}
