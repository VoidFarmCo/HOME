#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * SpiBus — single owner for the shared SPI bus.
 *
 * On the CYD the TFT has HSPI to itself and *everything else* shares VSPI:
 * the XPT2046 touch controller, the SD card, the CC1101, the NRF24, and the
 * PN532 (which bit-bangs on the same pads). The ESP32 has no third bus to
 * escape to.
 *
 * The resource that actually conflicts is the GPIO matrix, not the clock.
 * SCK and MOSI are peripheral *outputs* and can fan out to several pads, but
 * MISO is an *input*: exactly one GPIO can drive it. Touch reads on GPIO 39
 * and everything else reads on GPIO 19, so whoever attached last owns the
 * bus, and the loser silently reads the wrong pin. Nothing about that is
 * visible in a build log.
 *
 * So ownership is explicit here. claim() re-points the matrix, parks every
 * other chip select, and applies the settings the incoming device needs
 * rather than inheriting whatever the last one left behind.
 *
 * Re-pointing uses the spiAttach and spiDetach helpers directly rather than
 * SPIClass::end()/begin(). end() calls spiStopBus(), which resets the whole
 * peripheral -- and because spiStartBus() hands every SPIClass on a given bus
 * number the same spi_t*, that reset lands on the other users too. begin() is
 * no help either: it early-returns whenever _spi is already non-null, so a
 * second begin() with different pins does nothing at all.
 *
 * See docs/pueo/spi-bus.md for how the current behaviour was traced.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <Arduino.h>
#include <stdint.h>

namespace SpiBus {

enum class Dev : uint8_t {
  None = 0,
  Touch,    // XPT2046, driver manages its own clock via beginTransaction
  Sd,
  Cc1101,   // ELECHOUSE driver uses raw SPI.transfer(), so WE own its clock
  Nrf24,    // Nrf24Raw uses bare SPI.transfer(), so WE own its clock
  Count
};

/** Human name, for diagnostics. */
const char* name(Dev d);

/** True when touch and the radios are on the same peripheral (CYD, DIV V1). */
bool touchSharesRadioBus();

/**
 * Take the bus for `d`.
 *
 * Parks every other device's chip select, re-points SCK/MISO/MOSI at `d`'s
 * pins when the previous owner used different ones, and applies `d`'s clock,
 * mode and bit order. Cheap and idempotent when `d` already holds it.
 *
 * Pn532 is the odd one: its driver bit-bangs, so the peripheral is detached
 * from those pads instead of being pointed at them.
 */
void claim(Dev d);

/** Drop ownership and park `d`'s chip select. Safe to call unbalanced. */
void release(Dev d);

/** Who currently holds the bus. */
Dev owner();

/** Drive every known chip select high. */
void deselectAll();

/**
 * Full teardown: unmount SD, stop the peripheral, reset every pad, park every
 * chip select. This is the heavy hammer the legacy reclaim path needs; prefer
 * claim() for ordinary handoffs.
 */
void park();

/** Counters, for the serial diagnostics and for tests once hardware exists. */
struct Stats {
  uint32_t claims;         // claim() calls that changed owner
  uint32_t repins;         // times the GPIO matrix was actually re-pointed
  uint32_t conflicts;      // claim() while another device still held it
  uint32_t strayReleases;  // release(d) where d was not the owner
};
const Stats& stats();
void resetStats();

/** Dump owner + counters to Serial. No-op if the console is down. */
void logStats(const char* tag);

}  // namespace SpiBus
