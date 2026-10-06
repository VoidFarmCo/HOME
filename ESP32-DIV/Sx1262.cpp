#include "Sx1262.h"
#include <Arduino.h>
#include <SPI.h>
#include "shared.h"       // board pins (LORA_CS/RESET/BUSY via BoardConfig -> board_home.h)
#include "Mcp23017.h"     // writeAny/readAny: CS/RESET on the expander, BUSY direct

namespace Sx1262 {

#if defined(LORA_CS) && defined(LORA_RESET) && defined(LORA_BUSY)

namespace {

/* SX1262 command opcodes (datasheet rev 2.1, table 11-1). */
constexpr uint8_t OP_SET_STANDBY        = 0x80;
constexpr uint8_t OP_SET_PACKET_TYPE    = 0x8A;
constexpr uint8_t OP_SET_RF_FREQUENCY   = 0x86;
constexpr uint8_t OP_SET_MODULATION     = 0x8B;
constexpr uint8_t OP_SET_PACKET_PARAMS  = 0x8C;
constexpr uint8_t OP_SET_BUFFER_BASE    = 0x8F;
constexpr uint8_t OP_WRITE_BUFFER       = 0x0E;
constexpr uint8_t OP_READ_BUFFER        = 0x1E;
constexpr uint8_t OP_SET_TX             = 0x83;
constexpr uint8_t OP_SET_RX             = 0x82;
constexpr uint8_t OP_GET_IRQ_STATUS     = 0x12;
constexpr uint8_t OP_CLR_IRQ_STATUS     = 0x02;
constexpr uint8_t OP_SET_DIO_IRQ        = 0x08;
constexpr uint8_t OP_GET_RX_BUF_STATUS  = 0x13;
constexpr uint8_t OP_SET_TX_PARAMS      = 0x8E;
constexpr uint8_t OP_SET_PA_CONFIG      = 0x95;
constexpr uint8_t OP_SET_DIO2_RF_SW     = 0x9D;
constexpr uint8_t OP_SET_REGULATOR      = 0x96;
constexpr uint8_t OP_CALIBRATE          = 0x89;
constexpr uint8_t OP_WRITE_REGISTER     = 0x0D;
constexpr uint8_t OP_GET_STATUS         = 0xC0;

/* IRQ bits. */
constexpr uint16_t IRQ_TX_DONE = 0x0001;
constexpr uint16_t IRQ_RX_DONE = 0x0002;
constexpr uint16_t IRQ_CRC_ERR = 0x0040;
constexpr uint16_t IRQ_TIMEOUT = 0x0200;

/* Private H.O.M.E network sync word (not the public/LoRaWAN 0x3444). */
constexpr uint16_t SYNCWORD        = 0x1424;
constexpr uint16_t REG_SYNCWORD_MSB = 0x0740;

bool s_present = false;

inline void csLow()    { Mcp23017::writeAny(LORA_CS, LOW); }
inline void csHigh()   { Mcp23017::writeAny(LORA_CS, HIGH); }
inline bool busyHigh() { return Mcp23017::readAny(LORA_BUSY) != 0; }

SPISettings spiCfg(2000000, MSBFIRST, SPI_MODE0);

/* BUSY must read low before a command. Bounded so an absent/stuck module cannot
 * wedge the firmware. */
bool waitBusy(uint32_t timeoutMs = 20) {
  const uint32_t start = millis();
  while (busyHigh()) {
    if (millis() - start > timeoutMs) return false;
    delayMicroseconds(50);
  }
  return true;
}

/* opcode + params, nothing read back. */
void cmd(uint8_t op, const uint8_t* p, uint8_t n) {
  waitBusy();
  SPI.beginTransaction(spiCfg);
  csLow();
  SPI.transfer(op);
  for (uint8_t i = 0; i < n; i++) SPI.transfer(p[i]);
  csHigh();
  SPI.endTransaction();
}

/* opcode, skip `skip` status bytes, then read `n` bytes into out. */
void readCmd(uint8_t op, uint8_t skip, uint8_t* out, uint8_t n) {
  waitBusy();
  SPI.beginTransaction(spiCfg);
  csLow();
  SPI.transfer(op);
  for (uint8_t i = 0; i < skip; i++) SPI.transfer(0);
  for (uint8_t i = 0; i < n; i++) out[i] = SPI.transfer(0);
  csHigh();
  SPI.endTransaction();
}

void writeRegister(uint16_t addr, uint8_t val) {
  const uint8_t p[3] = { (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), val };
  cmd(OP_WRITE_REGISTER, p, 3);
}

void writeBuffer(uint8_t offset, const uint8_t* data, uint8_t len) {
  waitBusy();
  SPI.beginTransaction(spiCfg);
  csLow();
  SPI.transfer(OP_WRITE_BUFFER);
  SPI.transfer(offset);
  for (uint8_t i = 0; i < len; i++) SPI.transfer(data[i]);
  csHigh();
  SPI.endTransaction();
}

void readBuffer(uint8_t offset, uint8_t* data, uint8_t len) {
  waitBusy();
  SPI.beginTransaction(spiCfg);
  csLow();
  SPI.transfer(OP_READ_BUFFER);
  SPI.transfer(offset);
  SPI.transfer(0);                 // one NOP status byte
  for (uint8_t i = 0; i < len; i++) data[i] = SPI.transfer(0);
  csHigh();
  SPI.endTransaction();
}

uint16_t irqStatus() {
  uint8_t b[2];
  readCmd(OP_GET_IRQ_STATUS, 1, b, 2);   // 1 status byte first
  return (uint16_t)((b[0] << 8) | b[1]);
}

void clearIrq(uint16_t mask) {
  const uint8_t p[2] = { (uint8_t)(mask >> 8), (uint8_t)(mask & 0xFF) };
  cmd(OP_CLR_IRQ_STATUS, p, 2);
}

void setPacketLength(uint8_t len) {
  const uint8_t p[6] = { 0x00, 0x08,   // preamble length 8
                         0x00,          // variable-length (explicit) header
                         len,           // payload length
                         0x01,          // CRC on
                         0x00 };        // standard IQ
  cmd(OP_SET_PACKET_PARAMS, p, 6);
}

void resetChip() {
  Mcp23017::pinModeAny(LORA_CS, OUTPUT);
  Mcp23017::pinModeAny(LORA_RESET, OUTPUT);
  pinMode(LORA_BUSY, INPUT);
  csHigh();
  Mcp23017::writeAny(LORA_RESET, LOW);
  delay(2);
  Mcp23017::writeAny(LORA_RESET, HIGH);
  delay(5);
  waitBusy(50);
}

}  // namespace

bool begin(uint32_t freqHz) {
  resetChip();

  uint8_t p[8];

  p[0] = 0x00; cmd(OP_SET_STANDBY, p, 1);          // STDBY_RC
  p[0] = 0x01; cmd(OP_SET_REGULATOR, p, 1);        // DC-DC
  p[0] = 0x01; cmd(OP_SET_PACKET_TYPE, p, 1);      // LoRa
  p[0] = 0x7F; cmd(OP_CALIBRATE, p, 1);            // calibrate all blocks
  delay(5); waitBusy();

  // RF frequency: freq * 2^25 / 32 MHz.
  const uint32_t frf = (uint32_t)(((double)freqHz / 32000000.0) * 33554432.0);
  p[0] = (uint8_t)(frf >> 24); p[1] = (uint8_t)(frf >> 16);
  p[2] = (uint8_t)(frf >> 8);  p[3] = (uint8_t)(frf);
  cmd(OP_SET_RF_FREQUENCY, p, 4);

  // PA for the SX1262 at +22 dBm.
  p[0] = 0x04; p[1] = 0x07; p[2] = 0x00; p[3] = 0x01;
  cmd(OP_SET_PA_CONFIG, p, 4);
  p[0] = 22; p[1] = 0x04;                          // +22 dBm, 200 us ramp
  cmd(OP_SET_TX_PARAMS, p, 2);

  p[0] = 0x00; p[1] = 0x00; cmd(OP_SET_BUFFER_BASE, p, 2);

  // Modulation: SF7, BW 125 kHz, CR 4/5, low-data-rate off. Fine for short text.
  p[0] = 0x07; p[1] = 0x04; p[2] = 0x01; p[3] = 0x00;
  cmd(OP_SET_MODULATION, p, 4);

  setPacketLength(255);

  p[0] = 0x01; cmd(OP_SET_DIO2_RF_SW, p, 1);       // DIO2 drives the RF switch

  writeRegister(REG_SYNCWORD_MSB,     (uint8_t)(SYNCWORD >> 8));
  writeRegister(REG_SYNCWORD_MSB + 1, (uint8_t)(SYNCWORD & 0xFF));

  // Route TxDone/RxDone/Timeout to the IRQ status (polled, no DIO pin).
  const uint16_t mask = IRQ_TX_DONE | IRQ_RX_DONE | IRQ_TIMEOUT;
  p[0] = (uint8_t)(mask >> 8); p[1] = (uint8_t)(mask & 0xFF);
  p[2] = p[0]; p[3] = p[1];                        // DIO1 = same mask
  p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0;
  cmd(OP_SET_DIO_IRQ, p, 8);

  return present();
}

bool present() {
  uint8_t st = 0;
  readCmd(OP_GET_STATUS, 0, &st, 1);
  // A wired chip returns a status byte with real mode/command bits; an absent
  // module leaves MISO floating -> 0x00 or 0xFF.
  s_present = (st != 0x00 && st != 0xFF);
  return s_present;
}

bool send(const uint8_t* data, uint8_t len) {
  if (!s_present && !present()) return false;
  uint8_t p[3];
  p[0] = 0x00; cmd(OP_SET_STANDBY, p, 1);
  writeBuffer(0, data, len);
  setPacketLength(len);
  clearIrq(0xFFFF);
  p[0] = 0x00; p[1] = 0x00; p[2] = 0x00;           // SetTx timeout 0 = single shot
  cmd(OP_SET_TX, p, 3);

  const uint32_t start = millis();
  while (!(irqStatus() & IRQ_TX_DONE)) {
    if (millis() - start > 4000) { clearIrq(0xFFFF); return false; }
    delay(2);
  }
  clearIrq(0xFFFF);
  return true;
}

void startReceive() {
  uint8_t p[3];
  p[0] = 0x00; cmd(OP_SET_STANDBY, p, 1);
  clearIrq(0xFFFF);
  setPacketLength(255);
  p[0] = 0xFF; p[1] = 0xFF; p[2] = 0xFF;           // SetRx 0xFFFFFF = continuous
  cmd(OP_SET_RX, p, 3);
}

uint8_t receive(uint8_t* buf, uint8_t maxLen, int16_t* rssiOut) {
  const uint16_t irq = irqStatus();
  if (!(irq & IRQ_RX_DONE)) return 0;
  if (irq & IRQ_CRC_ERR) { clearIrq(0xFFFF); startReceive(); return 0; }

  uint8_t st[2];
  readCmd(OP_GET_RX_BUF_STATUS, 1, st, 2);         // payloadLen, startPtr
  uint8_t len = st[0];
  const uint8_t start = st[1];
  if (len > maxLen) len = maxLen;
  readBuffer(start, buf, len);

  if (rssiOut) {
    uint8_t r[1];
    readCmd(0x14 /* GetPacketStatus */, 1, r, 1);  // RssiPkt = -r/2 dBm
    *rssiOut = (int16_t)(-(r[0] / 2));
  }
  clearIrq(0xFFFF);
  startReceive();                                   // re-arm
  return len;
}

#else   /* board without LoRa pins: stubs so the sketch still links */

bool begin(uint32_t) { return false; }
bool present() { return false; }
bool send(const uint8_t*, uint8_t) { return false; }
void startReceive() {}
uint8_t receive(uint8_t*, uint8_t, int16_t*) { return 0; }

#endif

}  // namespace Sx1262
