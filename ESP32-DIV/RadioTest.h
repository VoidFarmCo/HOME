#pragma once
/* System > Radio Test: probe each module on the shared SPI bus and report
 * present / not found, with its pins. The HaleHound "Radio Test", for bringing
 * up the hat. No stored state: it re-probes on demand, so nothing lives in the
 * board's (full) .bss. */
namespace RadioTest {
void setup();
void loop();
}
