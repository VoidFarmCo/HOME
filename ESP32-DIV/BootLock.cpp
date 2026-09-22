#include "BootLock.h"

#include <Preferences.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "KeyboardUI.h"
#include "shared.h"
#include "utils.h"

namespace BootLock {
namespace {

/* NVS lives on the ESP32's flash and survives a settings.json that never
 * loads, a card that is not in the slot, and a card that belongs to somebody
 * else. It does not survive an erase_flash, which is the point made in the
 * header: this is not a hardware lock. */
constexpr char kNamespace[] = "pueo-lock";
constexpr char kSaltKey[]   = "salt";
constexpr char kHashKey[]   = "hash";

constexpr size_t kSaltLen = 16;
constexpr size_t kHashLen = 32;

/* Stretching, not a single hash.
 *
 * The password is going to be short -- it is typed on a 22-pixel on-screen
 * keyboard -- so a plain SHA-256 of it falls to a wordlist in about the time
 * it takes to write the wordlist. Iterating makes each guess cost the same
 * as one unlock.
 *
 * 20000 rounds is about a fifth of a second on a 240 MHz ESP32, which is
 * unnoticeable once at boot and is 5 guesses a second to somebody with the
 * flash contents. That is not a wall. It is the difference between a
 * wordlist run taking minutes and taking weeks, on a device whose real
 * answer to a determined attacker is "reflash it". */
constexpr int kRounds = 20000;

void derive(const uint8_t* salt, const String& pw, uint8_t out[kHashLen]) {
  uint8_t buf[kHashLen];
  mbedtls_sha256_context c;

  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts_ret(&c, 0);
  mbedtls_sha256_update_ret(&c, salt, kSaltLen);
  mbedtls_sha256_update_ret(&c, (const uint8_t*)pw.c_str(), pw.length());
  mbedtls_sha256_finish_ret(&c, buf);
  mbedtls_sha256_free(&c);

  for (int i = 0; i < kRounds; i++) {
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts_ret(&c, 0);
    mbedtls_sha256_update_ret(&c, salt, kSaltLen);
    mbedtls_sha256_update_ret(&c, buf, kHashLen);
    mbedtls_sha256_finish_ret(&c, buf);
    mbedtls_sha256_free(&c);
  }

  memcpy(out, buf, kHashLen);
  memset(buf, 0, sizeof(buf));
}

/* Compares every byte whichever way it goes. A loop that returns on the
 * first mismatch tells anyone who can time it how much of the hash they
 * guessed, which turns a search of the whole space into a search one byte
 * at a time. */
bool sameHash(const uint8_t* a, const uint8_t* b) {
  uint8_t diff = 0;
  for (size_t i = 0; i < kHashLen; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

/* The keyboard, set up the one way this file ever wants it. */
String ask(const char* line1, const char* line2, const char* okLabel,
           bool requireText) {
  OnScreenKeyboardConfig cfg;
  cfg.titleLine1      = line1;
  cfg.titleLine2      = line2;
  osKeyboardUseStandardLayout(cfg);
  cfg.maxLen          = 32;
  cfg.shuffleNames    = nullptr;
  cfg.shuffleCount    = 0;
  /* Every other caller in the tree uses 195. The keyboard is laid out for a
   * 240x320 panel throughout -- see KeyboardUI.cpp -- and matching it is
   * better than this one screen sitting somewhere different. */
  cfg.buttonsY        = 195;
  cfg.backLabel       = "Clear";
  cfg.middleLabel     = "";
  cfg.okLabel         = okLabel;
  cfg.enableShuffle   = false;
  cfg.requireNonEmpty = requireText;
  cfg.emptyErrorMsg   = "Enter a password";
  cfg.maskInput       = true;

  OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, "");
  if (!r.accepted) return String();
  return r.text;
}

void notice(const char* msg, uint16_t colour) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(colour, TFT_BLACK);
  tft.drawString(msg, PUEO_SCREEN_W / 2, PUEO_SCREEN_H / 2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
}

}  // namespace

bool isSet() {
  Preferences p;
  if (!p.begin(kNamespace, true)) return false;
  const bool have = p.getBytesLength(kSaltKey) == kSaltLen
                 && p.getBytesLength(kHashKey) == kHashLen;
  p.end();
  return have;
}

bool verify(const String& pw) {
  Preferences p;
  if (!p.begin(kNamespace, true)) return false;

  uint8_t salt[kSaltLen];
  uint8_t stored[kHashLen];
  const bool have = p.getBytes(kSaltKey, salt, kSaltLen) == kSaltLen
                 && p.getBytes(kHashKey, stored, kHashLen) == kHashLen;
  p.end();
  if (!have) return false;

  uint8_t got[kHashLen];
  derive(salt, pw, got);
  const bool okay = sameHash(got, stored);
  memset(got, 0, sizeof(got));
  return okay;
}

bool set(const String& pw) {
  Preferences p;
  if (!p.begin(kNamespace, false)) return false;

  if (pw.length() == 0) {
    p.remove(kSaltKey);
    p.remove(kHashKey);
    p.end();
    return true;
  }

  /* A new salt every time it is set, so setting the same password twice
   * does not produce the same stored bytes. */
  uint8_t salt[kSaltLen];
  esp_fill_random(salt, sizeof(salt));

  uint8_t hash[kHashLen];
  derive(salt, pw, hash);

  const bool okay = p.putBytes(kSaltKey, salt, sizeof(salt)) == sizeof(salt)
                 && p.putBytes(kHashKey, hash, sizeof(hash)) == sizeof(hash);
  p.end();
  memset(hash, 0, sizeof(hash));
  return okay;
}

void require() {
  if (!isSet()) return;

  /* The delay after a wrong answer doubles, to a ceiling. Someone sitting
   * with the device and a list gets slower; someone who mistyped once waits
   * a second. There is no lockout and no attempt counter kept across a
   * reboot -- a counter that a power cycle clears is theatre, and one that
   * survives would brick the device for its owner. */
  uint32_t penaltyMs = 1000;

  for (;;) {
    const String pw = ask("Locked", "Enter the boot password", "Unlock", true);

    /* Cancel is not a way out. There is nothing behind this screen until
     * the password is right, so Clear just starts the entry again. */
    if (pw.length() != 0 && verify(pw)) {
      notice("Unlocked", UI_OK);
      delay(400);
      return;
    }

    if (pw.length() != 0) {
      notice("Wrong password", UI_WARN);
      delay(penaltyMs);
      if (penaltyMs < 8000) penaltyMs *= 2;
    }
  }
}

void manage() {
  if (isSet()) {
    const String current = ask("Boot Lock", "Enter the current password",
                               "Next", true);
    if (current.length() == 0) return;          // cancelled
    if (!verify(current)) {
      notice("Wrong password", UI_WARN);
      delay(1200);
      return;
    }
  }

  const String next = ask("Boot Lock",
                          isSet() ? "New password, or Clear to remove"
                                  : "Set a boot password",
                          "Next", false);
  if (next.length() == 0) {
    /* Empty means remove when there was one, and means "changed my mind"
     * when there was not. Both land on the same call. */
    if (isSet()) {
      set(String());
      notice("Boot lock removed", UI_OK);
      delay(900);
    }
    return;
  }

  const String again = ask("Boot Lock", "Type it again", "Save", true);
  if (again != next) {
    notice("They did not match", UI_WARN);
    delay(1200);
    return;
  }

  if (set(next)) {
    notice("Boot lock set", UI_OK);
  } else {
    notice("Could not save", UI_WARN);
  }
  delay(900);
}

}  // namespace BootLock
