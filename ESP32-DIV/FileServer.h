#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * File Transfer — get the captures off the card without pulling the card.
 *
 * System > File Transfer. The device raises its own WPA2 access point, serves
 * the SD card read only over HTTP, and shows the SSID, the password and the
 * URL on screen. Join it from a phone or a laptop, open the page, tap a file.
 *
 * ── Why not Bluetooth ───────────────────────────────────────────────────────
 *
 * This started as "send the files over Bluetooth", which is the obvious ask
 * and does not survive contact with the stack. There is no standard BLE
 * profile for file transfer: the thing phones actually use is OBEX over
 * Classic Bluetooth, which needs Bluedroid, and this firmware is built on
 * NimBLE because Bluedroid will not fit beside the rest of it. A custom GATT
 * service would transfer files to nothing that exists, because the other end
 * would have to be an app somebody writes.
 *
 * HTTP over a SoftAP needs no app on the other end. Every phone already has
 * the client.
 *
 * ── It transmits ────────────────────────────────────────────────────────────
 *
 * A SoftAP beacons, so this refuses to start under Stealth Mode like any
 * other transmitter. It is in check_stealth.py's list for that reason. It
 * does not inject raw frames, so it is not in TX_NAMESPACES.
 *
 * ── Read only, and not open ─────────────────────────────────────────────────
 *
 * Nothing here writes, deletes or renames: SD File Manager is next door and
 * does that with the panel in your hand rather than over a radio. The AP
 * carries a generated 8 digit password shown on screen, because an open AP
 * serving a wardrive log and a packet capture to anyone in range is a worse
 * feature than no feature.
 *
 * The password is new every time the screen is opened. There is nothing to
 * remember and nothing stored, and a password that outlived the session would
 * be one more thing that leaks.
 * ────────────────────────────────────────────────────────────────────────── */

#include <Arduino.h>

namespace FileServer {

void setup();
void loop();
void exit();

}  // namespace FileServer
