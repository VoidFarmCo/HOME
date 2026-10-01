#include "FileServer.h"

#include "SettingsStore.h"
#include "Stealth.h"
#include "config.h"
#include "shared.h"
#include "utils.h"

#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

namespace FileServer {
namespace {

/* Its own instance, and on the heap.
 *
 * Its own, because wifi.cpp has two WebServers already and both belong to
 * the captive portal, whose routes are an attack: /login.html harvests
 * credentials. Hanging a file listing off the same object would put the
 * harvester one path away from a screen whose whole job is to be handed to
 * somebody you trust.
 *
 * On the heap, because a third static one does not fit. A WebServer carries
 * a WiFiServer and half a dozen Strings, and as a file-scope object it took
 * .dram0.bss 128 bytes past the segment and the link failed. DRAM is the
 * scarce thing in this image; the heap at the moment this screen opens is
 * not. It exists for exactly as long as the screen does. */
WebServer* s_server = nullptr;

/* Channel 6 rather than 1. Nothing here is trying to be unobtrusive, and 1
 * is where every other SoftAP in this firmware lands, so a phone with a
 * stale association has less to be confused by. */
constexpr int kApChannel = 6;

/* Two. One is the point, and the second is the laptop you switch to when the
 * phone turns out not to want to save a .pcap. */
constexpr int kMaxClients = 2;

/* Long enough to be worth typing once, short enough to type. Eight digits is
 * WPA2's minimum length and 10^8 guesses against an AP that exists for the
 * length of one screen. */
constexpr int kPassDigits = 8;

/* A listing is built in RAM a chunk at a time, but the entry loop still has
 * to stop somewhere: SD.open on a directory of thousands would otherwise
 * hold the UI for as long as it took to walk it. */
constexpr int kMaxEntries = 300;

constexpr int kMaxPathLen = 128;

constexpr uint32_t kRedrawMs = 250;

char     s_ssid[24]  = {0};
char     s_pass[kPassDigits + 1] = {0};
char     s_url[24]   = {0};
bool     s_running   = false;
bool     s_apUp      = false;
const char* s_fail    = nullptr;
const char* s_failWhy1 = nullptr;
const char* s_failWhy2 = nullptr;
int      s_clients   = -1;
uint32_t s_files     = 0;
uint64_t s_bytes     = 0;
bool     s_dirty     = true;
uint32_t s_lastDraw  = 0;

int contentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY() : PUEO_SCREEN_H;
}

/* ── Paths off the wire ───────────────────────────────────────────────────
 *
 * The only paths this serves are ones it printed itself, which is exactly
 * the reasoning that produces a directory traversal: the client sends
 * whatever it likes. "/logs/../../etc" means nothing on a FAT volume, but
 * SD's own path handling is not this code's to trust, so anything with a
 * ".." in it is refused rather than normalised. A path that has to be
 * cleaned up is a path somebody built by hand.
 */
bool pathOk(const String& p) {
  if (p.length() == 0 || p.length() > kMaxPathLen) {
    return false;
  }
  if (p[0] != '/') {
    return false;
  }
  if (p.indexOf("..") >= 0) {
    return false;
  }
  /* A control character cannot appear in a FAT name and can appear in a
   * header, which is the other end of the same mistake. */
  for (size_t i = 0; i < p.length(); i++) {
    if ((uint8_t)p[i] < 0x20 || (uint8_t)p[i] == 0x7F) {
      return false;
    }
  }
  return true;
}

String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '&') {
      out += "&amp;";
    } else if (c == '<') {
      out += "&lt;";
    } else if (c == '>') {
      out += "&gt;";
    } else if (c == '"') {
      out += "&quot;";
    } else {
      out += c;
    }
  }
  return out;
}

/* The href is a query value, so the escaping it needs is not the escaping
 * the link text needs. A file called "a&b .csv" is one bug in each
 * direction if you use one function for both: the text breaks the markup,
 * the href breaks at the ampersand and loses everything after it. */
String urlEncode(const String& s) {
  static const char kHex[] = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    const uint8_t c = (uint8_t)s[i];
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                      c == '.' || c == '~' || c == '/';
    if (safe) {
      out += (char)c;
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

String humanSize(uint64_t n) {
  char buf[24];
  if (n < 1024ULL) {
    snprintf(buf, sizeof(buf), "%u B", (unsigned)n);
  } else if (n < 1024ULL * 1024ULL) {
    snprintf(buf, sizeof(buf), "%.1f kB", (double)n / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%.1f MB", (double)n / (1024.0 * 1024.0));
  }
  return String(buf);
}

const char* mimeFor(const String& name) {
  String n = name;
  n.toLowerCase();
  if (n.endsWith(".jsonl")) return "application/x-ndjson";
  if (n.endsWith(".json"))  return "application/json";
  if (n.endsWith(".csv"))   return "text/csv";
  if (n.endsWith(".txt") || n.endsWith(".log")) return "text/plain";
  if (n.endsWith(".pcap"))  return "application/vnd.tcpdump.pcap";
  if (n.endsWith(".bin"))   return "application/octet-stream";
  return "application/octet-stream";
}

String parentOf(const String& path) {
  if (path == "/" || path.length() == 0) {
    return "/";
  }
  int cut = path.lastIndexOf('/');
  if (cut <= 0) {
    return "/";
  }
  return path.substring(0, cut);
}

String joinPath(const String& dir, const String& name) {
  if (dir.endsWith("/")) {
    return dir + name;
  }
  return dir + "/" + name;
}

/* ── The pages ────────────────────────────────────────────────────────────
 *
 * Chunked, not a String built whole and handed to send(). A listing is
 * unbounded in a way the heap is not, and the one directory anybody opens
 * here is the one with every capture they have ever taken in it.
 */
const char kStyle[] =
    "<style>body{background:#111;color:#eee;font:16px system-ui,sans-serif;"
    "margin:0;padding:12px}a{color:#ffa500;text-decoration:none}"
    "h1{font-size:18px;margin:4px 0 12px}table{border-collapse:collapse;"
    "width:100%}td{padding:8px 6px;border-bottom:1px solid #333}"
    "td.s{color:#888;text-align:right;white-space:nowrap}"
    ".d{color:#6cf}.n{color:#888;font-size:13px;margin-top:16px}</style>";

void sendHead(const String& title) {
  s_server->sendContent("<!doctype html><html><head><meta charset=utf-8>"
                       "<meta name=viewport content=\"width=device-width,"
                       "initial-scale=1\"><title>");
  s_server->sendContent(htmlEscape(title));
  s_server->sendContent("</title>");
  s_server->sendContent(kStyle);
  s_server->sendContent("</head><body>");
}

void sendError(int code, const char* msg) {
  s_server->send(code, "text/plain", msg);
}

void handleList() {
  String dirPath = s_server->hasArg("d") ? s_server->arg("d") : String("/");
  if (dirPath.length() == 0) {
    dirPath = "/";
  }
  if (!pathOk(dirPath)) {
    sendError(400, "bad path");
    return;
  }

  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    sendError(404, "no such directory");
    return;
  }

  s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server->send(200, "text/html", "");
  sendHead(dirPath);

  s_server->sendContent("<h1>");
  s_server->sendContent(htmlEscape(dirPath));
  s_server->sendContent("</h1><table>");

  if (dirPath != "/") {
    s_server->sendContent("<tr><td colspan=2><a class=d href=\"/?d=");
    s_server->sendContent(urlEncode(parentOf(dirPath)));
    s_server->sendContent("\">../</a></td></tr>");
  }

  int shown = 0;
  bool truncated = false;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    if (shown >= kMaxEntries) {
      truncated = true;
      e.close();
      break;
    }
    /* name() is the full path on some core versions and the leaf on others,
     * so take the leaf either way rather than finding out on a board. */
    String full = String(e.name());
    int cut = full.lastIndexOf('/');
    const String leaf = (cut >= 0) ? full.substring(cut + 1) : full;
    const bool isDir = e.isDirectory();
    const uint32_t size = isDir ? 0 : (uint32_t)e.size();
    e.close();

    if (leaf.length() == 0) {
      continue;
    }

    const String target = joinPath(dirPath, leaf);
    s_server->sendContent("<tr><td><a");
    if (isDir) {
      s_server->sendContent(" class=d href=\"/?d=");
    } else {
      s_server->sendContent(" href=\"/dl?f=");
    }
    s_server->sendContent(urlEncode(target));
    s_server->sendContent("\">");
    s_server->sendContent(htmlEscape(leaf));
    s_server->sendContent(isDir ? "/</a></td><td class=s></td></tr>"
                               : "</a></td><td class=s>");
    if (!isDir) {
      s_server->sendContent(humanSize(size));
      s_server->sendContent("</td></tr>");
    }
    shown++;
  }
  dir.close();

  s_server->sendContent("</table>");
  if (truncated) {
    s_server->sendContent("<p class=n>First ");
    s_server->sendContent(String(kMaxEntries));
    s_server->sendContent(" entries only.</p>");
  }
  s_server->sendContent("<p class=n>Read only. Nothing here can write to the "
                       "card.</p></body></html>");
  s_server->sendContent("");
}

void handleDownload() {
  if (!s_server->hasArg("f")) {
    sendError(400, "no file");
    return;
  }
  const String path = s_server->arg("f");
  if (!pathOk(path)) {
    sendError(400, "bad path");
    return;
  }

  File f = SD.open(path, FILE_READ);
  if (!f) {
    sendError(404, "no such file");
    return;
  }
  if (f.isDirectory()) {
    f.close();
    sendError(400, "that is a directory");
    return;
  }

  const uint32_t size = (uint32_t)f.size();
  int cut = path.lastIndexOf('/');
  const String leaf = (cut >= 0) ? path.substring(cut + 1) : path;

  /* attachment, not inline. A browser shown text/csv renders it, and what
   * somebody came here for is the file on their disk. The filename is one
   * this code just read out of a directory, and pathOk has already refused
   * the control characters that would let it break out of the header. */
  s_server->sendHeader("Content-Disposition",
                      "attachment; filename=\"" + leaf + "\"");
  s_server->streamFile(f, mimeFor(leaf));
  f.close();

  s_files++;
  s_bytes += size;
  s_dirty = true;
}

void handleNotFound() {
  /* Everything is reachable from the listing, so a miss is a typed URL or a
   * phone probing for a captive portal. Send it to the listing rather than
   * a dead end. */
  s_server->sendHeader("Location", "/", true);
  s_server->send(302, "text/plain", "");
}

/* ── Screen ───────────────────────────────────────────────────────────── */

constexpr int kLabelX = 14;
constexpr int kValueX = 150;

void drawRow(int y, const char* label, const char* value, uint16_t colour) {
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(label, kLabelX, y);
  tft.setTextColor(colour, TFT_BLACK);
  tft.drawString(value, kValueX, y);
}

void draw() {
  const int top = 22;
  tft.fillRect(0, top, PUEO_SCREEN_W, contentBottom() - top, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(PUEO_BODY_SIZE);

  if (!s_apUp) {
    /* Two reasons the screen can say nothing happened, and they want
     * different answers from whoever is reading it: put the card in, or find
     * out why the radio would not come up. One message for both would send
     * somebody looking at the wrong thing. */
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.drawString(s_fail != nullptr ? s_fail : "Nothing to serve",
                   kLabelX, top + 8);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(s_failWhy1 != nullptr ? s_failWhy1 : "", kLabelX, top + 40);
    tft.drawString(s_failWhy2 != nullptr ? s_failWhy2 : "", kLabelX, top + 62);
    return;
  }

  constexpr int kLine = 26;
  int y = top + 6;

  drawRow(y, "Network", s_ssid, TFT_WHITE);
  y += kLine;
  drawRow(y, "Password", s_pass, ORANGE);
  y += kLine;
  drawRow(y, "Open", s_url, TFT_WHITE);
  y += kLine + 10;

  char buf[32];
  snprintf(buf, sizeof(buf), "%d of %d", s_clients < 0 ? 0 : s_clients,
           kMaxClients);
  drawRow(y, "Joined", buf, s_clients > 0 ? ORANGE : TFT_DARKGREY);
  y += kLine;

  if (s_files == 0) {
    drawRow(y, "Sent", "nothing yet", TFT_DARKGREY);
  } else {
    snprintf(buf, sizeof(buf), "%lu file%s, %s", (unsigned long)s_files,
             s_files == 1 ? "" : "s", humanSize(s_bytes).c_str());
    drawRow(y, "Sent", buf, TFT_WHITE);
  }
  y += kLine + 10;

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Read only. Exit shuts the AP down.", kLabelX, y);
}

void makeCredentials() {
  uint8_t mac[6] = {0};
  WiFi.softAPmacAddress(mac);
  snprintf(s_ssid, sizeof(s_ssid), "Pueo-%02X%02X", mac[4], mac[5]);

  /* Digits only. This gets typed on a phone keyboard by someone reading it
   * off a 3.5" panel, and a character set that needs a shift key for every
   * third character is a password that gets retyped three times. */
  for (int i = 0; i < kPassDigits; i++) {
    s_pass[i] = (char)('0' + (esp_random() % 10));
  }
  s_pass[kPassDigits] = '\0';
}

}  // namespace

void setup() {
  if (Stealth::refuse("File Transfer")) return;

  s_running  = true;
  s_apUp     = false;
  s_clients  = -1;
  s_fail     = nullptr;
  s_failWhy1 = nullptr;
  s_failWhy2 = nullptr;
  s_files    = 0;
  s_bytes    = 0;
  s_dirty    = true;
  s_lastDraw = 0;

  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  setTouchNavLabels(nullptr, nullptr, "Exit", nullptr, nullptr);

  tft.fillScreen(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  redrawTouchButtonBar();

  /* Mount it first, the same way SD File Manager does. isSDCardAvailable()
   * reports the state of the mount, not the state of the card, and a feature
   * that handed the bus to the CC1101 or the nRF24 leaves it unmounted. Going
   * straight to the flag would tell somebody their card was missing because
   * of what they opened before this. */
  sdRetryMount();
  if (!isSDCardAvailable()) {
    /* The radio stays down. Raising an access point to serve an empty volume
     * would be a beacon with nothing behind it, which is worse than the
     * error message. */
    s_fail     = "No SD card";
    s_failWhy1 = "There is nothing to serve, so";
    s_failWhy2 = "the access point stayed down.";
    draw();
    return;
  }

  /* AP mode before the password is generated, so the hardware RNG is running
   * with RF on. esp_random is documented as true random only then, and this
   * is the one number here that has to be unguessable. */
  WiFi.mode(WIFI_AP);
  delay(50);
  makeCredentials();

  if (!WiFi.softAP(s_ssid, s_pass, kApChannel, 0, kMaxClients)) {
    s_fail     = "The access point did not start";
    s_failWhy1 = "The card is fine. The radio";
    s_failWhy2 = "refused. Try leaving and re-entering.";
    draw();
    return;
  }
  snprintf(s_url, sizeof(s_url), "http://%s",
           WiFi.softAPIP().toString().c_str());

  s_server = new WebServer(80);
  if (s_server == nullptr) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    s_fail     = "Out of memory";
    s_failWhy1 = "The web server would not fit.";
    s_failWhy2 = "Reboot and open this first.";
    draw();
    return;
  }
  s_server->on("/", HTTP_GET, handleList);
  s_server->on("/dl", HTTP_GET, handleDownload);
  s_server->onNotFound(handleNotFound);
  s_server->begin();

  s_apUp = true;
  draw();
}

void loop() {
  if (!s_running) {
    return;
  }

  if (isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    waitForButtonRelease(BTN_SELECT);
    return;
  }

  if (s_apUp) {
    /* streamFile blocks for the length of one file, so the screen does not
     * update mid-transfer. That is the right trade: the alternative is a
     * server that gives up a download to repaint a counter. */
    s_server->handleClient();

    const int n = WiFi.softAPgetStationNum();
    if (n != s_clients) {
      s_clients = n;
      s_dirty = true;
    }
  }

  const uint32_t now = millis();
  if (s_dirty && (uint32_t)(now - s_lastDraw) >= kRedrawMs) {
    s_lastDraw = now;
    s_dirty = false;
    draw();
  }

  delay(4);
}

void exit() {
  /* Reached whether or not setup got as far as raising anything, including
   * when Stealth refused before it ran at all, so every step here has to be
   * safe on a feature that never started. */
  s_running = false;

  if (s_apUp) {
    s_server->close();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    s_apUp = false;
  }

  /* The server goes back to the heap it came from. Keeping it would hold a
   * listening socket and several kilobytes for every screen that is not this
   * one, which is the cost the static instance was charging. */
  delete s_server;
  s_server = nullptr;

  /* The password does not outlive the screen. Nothing reads it after this,
   * and a credential left in a static is a credential in a core dump. */
  memset(s_pass, 0, sizeof(s_pass));

  requestStatusBarRedraw();
}

}  // namespace FileServer
