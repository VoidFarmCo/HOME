#include "Nrf24Raw.h"

#include "SpiBus.h"
#include "shared.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

namespace Nrf24Raw {

namespace {

/* Registers and commands, from the nRF24L01+ datasheet section 9. Written
 * out here rather than included from a driver, which is the whole point of
 * this file. */
constexpr uint8_t REG_CONFIG     = 0x00;
constexpr uint8_t REG_EN_AA      = 0x01;
constexpr uint8_t REG_SETUP_RETR = 0x04;
constexpr uint8_t REG_RF_CH      = 0x05;
constexpr uint8_t REG_RF_SETUP   = 0x06;
constexpr uint8_t REG_TX_ADDR    = 0x10;

constexpr uint8_t CMD_W_REGISTER   = 0x20;
constexpr uint8_t CMD_FLUSH_TX     = 0xE1;
constexpr uint8_t CMD_W_TX_PAYLOAD = 0xA0;
constexpr uint8_t CMD_REUSE_TX_PL  = 0xE3;

/* CONFIG */
constexpr uint8_t CFG_PWR_UP  = 0x02;

/* RF_SETUP. CONT_WAVE and PLL_LOCK together are what make the output an
 * unmodulated carrier rather than a transmission. RF_DR_HIGH with RF_DR_LOW
 * clear is 2 Mbps; RF_PWR 0b11 is 0 dBm, the part's maximum. */
constexpr uint8_t RF_CONT_WAVE = 0x80;
constexpr uint8_t RF_PLL_LOCK  = 0x10;
constexpr uint8_t RF_DR_HIGH   = 0x08;
constexpr uint8_t RF_PWR_MAX   = 0x06;

/* The datasheet's power-up settling time, and the synthesiser's settling
 * time after a channel change. Both are worth honouring: skipping the second
 * is how a hopping carrier ends up spending its time in transit. */
constexpr uint32_t kPowerUpUs = 1500;
constexpr uint32_t kSettleUs  = 130;

bool    s_present = false;
uint8_t s_channel = 0xFF;

inline void csnLow()  { digitalWrite(CSN_PIN_1, LOW); }
inline void csnHigh() { digitalWrite(CSN_PIN_1, HIGH); }
inline void ceLow()   { digitalWrite(CE_PIN_1, LOW); }
inline void ceHigh()  { digitalWrite(CE_PIN_1, HIGH); }

uint8_t readReg(uint8_t r) {
  csnLow();
  SPI.transfer(r & 0x1F);
  const uint8_t v = SPI.transfer(0);
  csnHigh();
  return v;
}

void writeReg(uint8_t r, uint8_t v) {
  csnLow();
  SPI.transfer((r & 0x1F) | CMD_W_REGISTER);
  SPI.transfer(v);
  csnHigh();
}

void writeRegMulti(uint8_t r, const uint8_t* d, uint8_t n) {
  csnLow();
  SPI.transfer((r & 0x1F) | CMD_W_REGISTER);
  for (uint8_t i = 0; i < n; i++) {
    SPI.transfer(d[i]);
  }
  csnHigh();
}

void command(uint8_t c) {
  csnLow();
  SPI.transfer(c);
  csnHigh();
}

void commandWithData(uint8_t c, const uint8_t* d, uint8_t n) {
  csnLow();
  SPI.transfer(c);
  for (uint8_t i = 0; i < n; i++) {
    SPI.transfer(d[i]);
  }
  csnHigh();
}

}  // namespace

bool begin() {
  pinMode(CE_PIN_1, OUTPUT);
  pinMode(CSN_PIN_1, OUTPUT);
  ceLow();
  csnHigh();

  SpiBus::claim(SpiBus::Dev::Nrf24);
  delay(5);

  /* Presence check by write and read-back. A absent module floats the MISO
   * line and reads a constant, so a register that returns what was just put
   * in it is a chip rather than a pull-up. RF_CH is safe to scribble on:
   * every path here sets it before transmitting anything. */
  writeReg(REG_RF_CH, 0x4C);
  s_present = (readReg(REG_RF_CH) == 0x4C);

  if (s_present) {
    writeReg(REG_CONFIG, 0x00);     // powered down, CRC off, primary TX
  }
  s_channel = 0xFF;
  return s_present;
}

bool present() {
  return s_present;
}

void startConstCarrier(uint8_t ch) {
  if (!s_present) {
    return;
  }
  SpiBus::claim(SpiBus::Dev::Nrf24);

  ceLow();
  writeReg(REG_EN_AA, 0x00);        // no auto-acknowledge
  writeReg(REG_SETUP_RETR, 0x00);   // no retransmission
  writeReg(REG_CONFIG, CFG_PWR_UP); // powered, primary TX, CRC off
  delayMicroseconds(kPowerUpUs);

  /* The plus variant will not hold a carrier on an empty FIFO: it wants a
   * payload to keep re-sending. Address and payload are all-ones because
   * neither is meant to be received by anything. */
  uint8_t ones[32];
  memset(ones, 0xFF, sizeof(ones));
  writeRegMulti(REG_TX_ADDR, ones, 5);
  command(CMD_FLUSH_TX);
  commandWithData(CMD_W_TX_PAYLOAD, ones, sizeof(ones));

  writeReg(REG_RF_SETUP, RF_CONT_WAVE | RF_PLL_LOCK | RF_DR_HIGH | RF_PWR_MAX);
  writeReg(REG_RF_CH, ch);
  s_channel = ch;

  ceHigh();
  delay(1);
  command(CMD_REUSE_TX_PL);
}

void hopTo(uint8_t ch) {
  if (!s_present || ch == s_channel) {
    return;
  }
  SpiBus::claim(SpiBus::Dev::Nrf24);

  /* CE down while the synthesiser is retuned. Writing RF_CH under a live
   * carrier is not something the datasheet undertakes to define; this is the
   * documented order and it costs 130 us. */
  ceLow();
  writeReg(REG_RF_CH, ch);
  ceHigh();
  delayMicroseconds(kSettleUs);
  s_channel = ch;
}

void stop() {
  if (!s_present) {
    return;
  }
  SpiBus::claim(SpiBus::Dev::Nrf24);
  ceLow();
  writeReg(REG_RF_SETUP, RF_DR_HIGH | RF_PWR_MAX);   // carrier bits cleared
  command(CMD_FLUSH_TX);
  s_channel = 0xFF;
}

void powerDown() {
  if (!s_present) {
    return;
  }
  stop();
  writeReg(REG_CONFIG, 0x00);
  s_channel = 0xFF;
}

uint8_t channel() {
  return s_channel;
}

}  // namespace Nrf24Raw
