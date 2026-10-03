# Radio Test (System)

**System, Radio Test.** Probes each module on the shared SPI bus and reports
**present** or **not found**, with its pins. The HaleHound "Radio Test", for
bringing up the hat: flash a bare board and it reads "not found" for everything
but the SD, then each module comes up as you wire it.

Probes (each uses the driver already in the tree, so a probe matches how the
feature itself talks to the part):

| Module | Check | Pins |
|---|---|---|
| SD card | `isSDCardAvailable()` | CS5 |
| CC1101 | `getCC1101()` (reads the VERSION register) | CS21, GDO 22/35 |
| NRF24 | `Nrf24Raw::begin()` then `present()` | CE16, CSN25 |
| PN532 | `RfidNfc::begin()` (firmware version reads back) | SS17 |

**Rescan** (left slot) re-runs the probes; **Exit** returns to System.

There is no stored result and no menu check of its own: the screen is read-only,
it re-probes on demand (nothing in the board's full `.bss`), and the menu-integrity
checks (`check_menu_dispatch`, `check_menu_tables`, `check_grid_capacity`) cover
the wiring.

The matching PCB work is `D:\Projects\HOME-RF-Hat` (Pueo's inherited carrier-board
design); Radio Test is the bring-up tool for it.
