#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * BootLock — a password asked for at boot, before the menu appears.
 *
 * ── What this is for, and what it is not ────────────────────────────────────
 *
 * It stops someone who picks the device up from reading what is on it. That
 * is the threat it is built for and the only one it meets.
 *
 * It does not stop anyone who is willing to open a USB cable. The firmware
 * can be reflashed in twenty seconds, and esptool can read the flash out
 * without asking this code anything. There is no secure boot, no flash
 * encryption and no eFuse burned on a CYD, so the lock is a thing the
 * running firmware chooses to honour rather than something the hardware
 * enforces. Anybody who knows that gets past it.
 *
 * Saying so here because a lock that is described as more than it is gets
 * trusted with more than it can hold.
 *
 * ── Where the password lives ────────────────────────────────────────────────
 *
 * In NVS, on the ESP32's own flash, and never on the SD card. Two reasons,
 * and the second is the one that decided it:
 *
 *   - the card comes out, and anything on it can be read on any laptop
 *   - settings.json is not read at boot on this board at all (see the
 *     BOARD_HAS_ESP32S3 branch in ESP32-DIV.ino), so a lock that lived
 *     there would never be loaded in time to lock anything
 *
 * What is stored is a salted, stretched SHA-256 of the password, not the
 * password. That does not make the flash safe to hand out -- see above --
 * but it does mean a flash dump does not immediately hand over a string
 * somebody has probably reused somewhere that matters more than this.
 * ────────────────────────────────────────────────────────────────────────── */

#include <Arduino.h>

namespace BootLock {

/* Is a password set? Cheap; reads a length out of NVS. */
bool isSet();

/* Constant-time check of `pw` against the stored hash. */
bool verify(const String& pw);

/* Store `pw`, or clear the lock when it is empty. Writes NVS immediately:
 * this is not part of AppSettings and does not wait for Save, because a
 * password you thought you set and did not is the wrong way round. */
bool set(const String& pw);

/* Ask, until the answer is right. Returns at once when no password is set.
 * Requires the display and the touchscreen to be up. */
void require();

/* The Settings row: verify the current password, then set, change or clear.
 * Repaints nothing -- the caller owns the screen and redraws after. */
void manage();

}  // namespace BootLock
