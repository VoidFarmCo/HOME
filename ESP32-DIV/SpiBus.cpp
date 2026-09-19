#include "SpiBus.h"

#include <SPI.h>
#include <SD.h>
#include "esp32-hal-spi.h"
#include "driver/gpio.h"

#include "shared.h"
#include "Touchscreen.h"

namespace SpiBus {

namespace {

/* Touch lives on the radio peripheral wherever Touchscreen.cpp builds
 * touchscreenSPI as SPIClass(VSPI) -- the same bus number the global SPI
 * object uses on classic ESP32. That is the CYD and DIV V1 case. */
#if !TOUCH_SHARES_TFT_SPI && (defined(BOARD_CYD) || defined(BOARD_ESP32_DIV_V1))
#define SPIBUS_TOUCH_ON_RADIO_BUS 1
#else
#define SPIBUS_TOUCH_ON_RADIO_BUS 0
#endif

struct Profile {
  const char* name;
  int8_t cs;
  int8_t sck;
  int8_t miso;
  int8_t mosi;
  uint32_t hz;      // 0 = driver sets its own via beginTransaction
  uint8_t mode;
  uint8_t bitOrder;
  bool hardware;    // false: driver bit-bangs, detach the peripheral instead
};

constexpr int8_t kNoPin = -1;

/* Clock notes:
 *   Touch  driver wraps every read in beginTransaction(2 MHz), so ours is moot
 *   Sd     SD.begin() is given its own frequency; this is the idle setting
 *   Cc1101 the ELECHOUSE driver issues bare SPI.transfer() with no transaction
 *          at all, so it runs at whatever the bus was left at. That is the one
 *          clock we genuinely own. 4 MHz sits under the CC1101's 6.5 MHz
 *          burst-access ceiling with margin.
 *   Nrf24  Nrf24Raw issues bare SPI.transfer() like the CC1101 driver does,
 *          so since RF24 was dropped this is the clock the part actually
 *          runs at rather than an idle setting. 10 MHz is the nRF24L01+'s
 *          documented SPI ceiling; it is a limit rather than a margin.
 */
const Profile kProfiles[] = {
  /* None   */ {"none",   kNoPin, kNoPin, kNoPin, kNoPin, 0, SPI_MODE0, MSBFIRST, true},
#if SPIBUS_TOUCH_ON_RADIO_BUS
  /* Touch  */ {"touch",  XPT2046_CS, XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI,
                2000000, SPI_MODE0, MSBFIRST, true},
#else
  /* Touch  */ {"touch",  kNoPin, kNoPin, kNoPin, kNoPin, 0, SPI_MODE0, MSBFIRST, true},
#endif
  /* Sd     */ {"sd",     SD_CS, SD_SCLK, SD_MISO, SD_MOSI,
                4000000, SPI_MODE0, MSBFIRST, true},
  /* Cc1101 */ {"cc1101", CC1101_CS, CC1101_SCK, CC1101_MISO, CC1101_MOSI,
                4000000, SPI_MODE0, MSBFIRST, true},
  /* Nrf24  */ {"nrf24",  CSN_PIN_1, NRF24_SPI_SCK, NRF24_SPI_MISO, NRF24_SPI_MOSI,
                10000000, SPI_MODE0, MSBFIRST, true},
  /* Pn532  */ {"pn532",  PN532_SS, PN532_SCK, PN532_MISO, PN532_MOSI,
                0, SPI_MODE0, LSBFIRST, false},
};
static_assert(sizeof(kProfiles) / sizeof(kProfiles[0]) == (size_t)Dev::Count,
              "SpiBus: profile table does not match Dev");

/* Every chip select on the shared bus, so a claim can park the others. */
const int8_t kAllChipSelects[] = {
  SD_CS, CC1101_CS, PN532_SS, CSN_PIN_1,
#if CSN_PIN_2 != CSN_PIN_1
  CSN_PIN_2,
#endif
#if CSN_PIN_3 != CSN_PIN_1 && CSN_PIN_3 != CSN_PIN_2
  CSN_PIN_3,
#endif
#if SPIBUS_TOUCH_ON_RADIO_BUS
  XPT2046_CS,
#endif
};

Dev s_owner = Dev::None;
Dev s_pinned = Dev::None;   // whose pins the GPIO matrix currently carries
Stats s_stats = {0, 0, 0, 0};

const Profile& prof(Dev d) { return kProfiles[(uint8_t)d]; }

void parkCs(int8_t pin) {
  if (pin < 0) {
    return;
  }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
}

/* The spi_t the shared peripheral is running on under the global SPI object,
 * or null when SPI has not started it (or end() stopped it).
 *
 * Deliberately not falling back to touchscreenSPI.bus(): after SPI.end() that
 * pointer is still non-null but the peripheral behind it has been stopped, so
 * using it would attach pins to a dead bus. SPIClass never clears another
 * instance's _spi, so a stale handle is the normal state here, not an edge
 * case. */
spi_t* busHandle() {
  return SPI.bus();
}

/* Point the matrix at `p`'s pins without resetting the peripheral. */
void repin(const Profile& p) {
  spi_t* bus = busHandle();
  if (!bus) {
    // Peripheral not running under SPI. If touch started it and something
    // later stopped it, its pins are still routed -- drop them before we
    // restart, or the old MISO source stays wired up.
#if SPIBUS_TOUCH_ON_RADIO_BUS
    spi_t* stale = touchscreenSPI.bus();
    const Profile& prev = prof(s_pinned);
    if (stale && s_pinned != Dev::None) {
      if (prev.sck >= 0) {
        spiDetachSCK(stale, prev.sck);
      }
      if (prev.miso >= 0) {
        spiDetachMISO(stale, prev.miso);
      }
      if (prev.mosi >= 0) {
        spiDetachMOSI(stale, prev.mosi);
      }
    }
#endif
    SPI.begin(p.sck, p.miso, p.mosi, kNoPin);
    s_stats.repins++;
    return;
  }
  const Profile& old = prof(s_pinned);
  if (old.sck >= 0 && old.sck != p.sck) {
    spiDetachSCK(bus, old.sck);
  }
  if (old.miso >= 0 && old.miso != p.miso) {
    spiDetachMISO(bus, old.miso);
  }
  if (old.mosi >= 0 && old.mosi != p.mosi) {
    spiDetachMOSI(bus, old.mosi);
  }
  if (p.sck >= 0) {
    spiAttachSCK(bus, p.sck);
  }
  if (p.miso >= 0) {
    spiAttachMISO(bus, p.miso);
  }
  if (p.mosi >= 0) {
    spiAttachMOSI(bus, p.mosi);
  }
  s_stats.repins++;
}

/* Hand the pads back to plain GPIO so a bit-bang driver can drive them. */
void detachForBitbang(const Profile& p) {
  spi_t* bus = busHandle();
  const Profile& old = prof(s_pinned);
  if (bus) {
    if (old.sck >= 0) {
      spiDetachSCK(bus, old.sck);
    }
    if (old.miso >= 0) {
      spiDetachMISO(bus, old.miso);
    }
    if (old.mosi >= 0) {
      spiDetachMOSI(bus, old.mosi);
    }
  }
  if (p.sck >= 0) {
    gpio_reset_pin((gpio_num_t)p.sck);
  }
  if (p.miso >= 0) {
    gpio_reset_pin((gpio_num_t)p.miso);
  }
  if (p.mosi >= 0) {
    gpio_reset_pin((gpio_num_t)p.mosi);
  }
  s_stats.repins++;
}

}  // namespace

const char* name(Dev d) {
  return ((uint8_t)d < (uint8_t)Dev::Count) ? prof(d).name : "?";
}

bool touchSharesRadioBus() {
  return SPIBUS_TOUCH_ON_RADIO_BUS != 0;
}

Dev owner() { return s_owner; }

void deselectAll() {
  for (size_t i = 0; i < sizeof(kAllChipSelects) / sizeof(kAllChipSelects[0]); i++) {
    parkCs(kAllChipSelects[i]);
  }
}

void claim(Dev d) {
  if (d == Dev::None || (uint8_t)d >= (uint8_t)Dev::Count) {
    return;
  }
  if (s_owner == d && s_pinned == d) {
    return;  // already ours, nothing to do
  }
  if (s_owner != Dev::None && s_owner != d) {
    // Not fatal -- the previous owner simply never released. Record it so the
    // unbalanced path can be found, then take the bus anyway, which is what
    // the pre-arbitration code did implicitly on every handoff.
    s_stats.conflicts++;
  }

  const Profile& p = prof(d);

  // Park everyone first so nothing is listening while the matrix moves.
  deselectAll();

  if (s_pinned != d) {
    if (p.hardware) {
      repin(p);
    } else {
      detachForBitbang(p);
    }
    s_pinned = d;
  }

  if (p.hardware && busHandle()) {
    // Applied explicitly every time. Inheriting these is how the CC1101 ended
    // up running at whichever speed the previously visited feature left set.
    SPI.setDataMode(p.mode);
    SPI.setBitOrder(p.bitOrder);
    if (p.hz) {
      SPI.setFrequency(p.hz);
    }
  }

  s_owner = d;
  s_stats.claims++;
}

void release(Dev d) {
  if (s_owner != d) {
    s_stats.strayReleases++;
  }
  const Profile& p = prof(d);
  parkCs(p.cs);
  if (s_owner == d) {
    s_owner = Dev::None;
  }
}

void park() {
  deselectAll();

  SD.end();

  spi_t* bus = busHandle();
  if (bus) {
    const Profile& old = prof(s_pinned);
    if (old.sck >= 0) {
      spiDetachSCK(bus, old.sck);
    }
    if (old.miso >= 0) {
      spiDetachMISO(bus, old.miso);
    }
    if (old.mosi >= 0) {
      spiDetachMOSI(bus, old.mosi);
    }
  }

  // Pads the PN532 bit-bang may have left configured as plain GPIO.
  gpio_reset_pin((gpio_num_t)PN532_SCK);
  gpio_reset_pin((gpio_num_t)PN532_MISO);
  gpio_reset_pin((gpio_num_t)PN532_MOSI);

  deselectAll();

  s_owner = Dev::None;
  s_pinned = Dev::None;
}

const Stats& stats() { return s_stats; }

void resetStats() { s_stats = Stats{0, 0, 0, 0}; }

void logStats(const char* tag) {
  Serial.printf("[spibus] %s owner=%s pinned=%s claims=%u repins=%u "
                "conflicts=%u stray=%u\n",
                tag ? tag : "", name(s_owner), name(s_pinned),
                (unsigned)s_stats.claims, (unsigned)s_stats.repins,
                (unsigned)s_stats.conflicts, (unsigned)s_stats.strayReleases);
}

}  // namespace SpiBus
