#include "Mcp23017.h"
#include <Arduino.h>
#include <Wire.h>

namespace Mcp23017 {
namespace {

/* Register map, IOCON.BANK = 0 (the power-on default). */
constexpr uint8_t IODIRA = 0x00, IODIRB = 0x01;   // direction: 1 = input, 0 = output
constexpr uint8_t GPPUA  = 0x0C, GPPUB  = 0x0D;   // pull-ups
constexpr uint8_t GPIOA  = 0x12, GPIOB  = 0x13;   // read pin state
constexpr uint8_t OLATA  = 0x14, OLATB  = 0x15;   // output latch (drive outputs here)

uint8_t s_addr = 0x20;
/* Shadow copies so a single pin change is one write, not a read-modify-write
 * race against the bus. Index 0 = port A, 1 = port B. Seeded to the chip's
 * reset state in begin(). */
uint8_t s_iodir[2] = {0xFF, 0xFF};   // reset default: all inputs
uint8_t s_gppu[2]  = {0x00, 0x00};
uint8_t s_olat[2]  = {0x00, 0x00};

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.endTransmission(false);                 // repeated start, keep the bus
  Wire.requestFrom((int)s_addr, 1);
  return Wire.available() ? (uint8_t)Wire.read() : 0;
}

}  // namespace

bool begin(int sda, int scl, uint8_t addr) {
  s_addr = addr;
  Wire.begin(sda, scl);
  s_iodir[0] = s_iodir[1] = 0xFF;
  s_gppu[0]  = s_gppu[1]  = 0x00;
  s_olat[0]  = s_olat[1]  = 0x00;
  writeReg(IODIRA, s_iodir[0]);  writeReg(IODIRB, s_iodir[1]);
  writeReg(GPPUA,  s_gppu[0]);   writeReg(GPPUB,  s_gppu[1]);
  writeReg(OLATA,  s_olat[0]);   writeReg(OLATB,  s_olat[1]);
  return present();
}

bool present() {
  Wire.beginTransmission(s_addr);
  return Wire.endTransmission() == 0;          // 0 = the chip ACKed its address
}

void pinMode(uint8_t pin, uint8_t mode) {
  const uint8_t port = (pin >= 8) ? 1 : 0;
  const uint8_t bit  = (uint8_t)(1 << (pin & 7));
  if (mode == OUTPUT) {
    s_iodir[port] &= (uint8_t)~bit;            // 0 = output
    s_gppu[port]  &= (uint8_t)~bit;
  } else {                                     // INPUT or INPUT_PULLUP
    s_iodir[port] |= bit;                      // 1 = input
    if (mode == INPUT_PULLUP) s_gppu[port] |= bit;
    else                      s_gppu[port] &= (uint8_t)~bit;
  }
  writeReg(port ? IODIRB : IODIRA, s_iodir[port]);
  writeReg(port ? GPPUB  : GPPUA,  s_gppu[port]);
}

void digitalWrite(uint8_t pin, uint8_t val) {
  const uint8_t port = (pin >= 8) ? 1 : 0;
  const uint8_t bit  = (uint8_t)(1 << (pin & 7));
  if (val) s_olat[port] |= bit;
  else     s_olat[port] &= (uint8_t)~bit;
  writeReg(port ? OLATB : OLATA, s_olat[port]);
}

int digitalRead(uint8_t pin) {
  const uint8_t port = (pin >= 8) ? 1 : 0;
  const uint8_t bit  = (uint8_t)(1 << (pin & 7));
  const uint8_t v = readReg(port ? GPIOB : GPIOA);
  return (v & bit) ? 1 : 0;
}

/* Expander-aware helpers. `owns(pin)` -> the MCP23017 channel pin - PIN_BASE;
 * otherwise a plain ESP32 GPIO via the Arduino globals (::pinMode etc.). */
void pinModeAny(int pin, uint8_t mode) {
  if (owns(pin)) pinMode((uint8_t)(pin - PIN_BASE), mode);
  else           ::pinMode((uint8_t)pin, mode);
}

void writeAny(int pin, uint8_t val) {
  if (owns(pin)) digitalWrite((uint8_t)(pin - PIN_BASE), val);
  else           ::digitalWrite((uint8_t)pin, val);
}

int readAny(int pin) {
  return owns(pin) ? digitalRead((uint8_t)(pin - PIN_BASE))
                   : ::digitalRead((uint8_t)pin);
}

}  // namespace Mcp23017

/* Global-linkage wrappers for the vendored ELECHOUSE CC1101 lib, which is
 * compiled separately and cannot include this header. It declares these extern
 * and routes its chip-select through them (see the lib's ccCsWrite/ccCsMode). */
void homeExpWrite(int pin, unsigned char val) { Mcp23017::writeAny(pin, val); }
void homeExpMode(int pin, unsigned char mode)  { Mcp23017::pinModeAny(pin, mode); }
