<p align="center">
  <img src="../img/pueo-guide.webp" width="100%"
       alt="The Pueo mark: a stylised horned owl in green on near-black, with
            a WiFi arc, a Bluetooth rune and a satellite across its chest and
            the word PUEO beneath, in a sweep of concentric signal arcs.">
</p>

# Using Pueo

This is the operating manual. It assumes you have a working unit: firmware
flashed, modules wired, and a screen that responds when you touch it. If you
are still building one, [build-guide.md](build-guide.md) is the other
document and it ends where this one starts.

> **Everything here is for equipment you own or have written permission to
> test.** Several of these tools transmit, jam, or impersonate, and doing
> that to other people's equipment is illegal in most places. Nothing in
> this guide changes that.

---

## Contents

- [The screen](#the-screen)
- [Getting around](#getting-around)
- [The menu](#the-menu)
- [What needs what](#what-needs-what)
- [The SD card](#the-sd-card)
- [Settings](#settings)
- [Stealth mode](#stealth-mode)
- [When something says it is not there](#when-something-says-it-is-not-there)

---

## The screen

Power on and you land at the main menu. Across the top is the status bar,
and it stays there in almost every feature.

| | |
|---|---|
| battery | percentage, from the cell on BAT1. Reads 0% with no cell fitted |
| version | the firmware you are running, for example `Pueo 0.4.13` |
| Bluetooth | lit when the BLE radio is up |
| signal | WiFi activity |
| satellite | lit when the GPS has a fix |
| temperature | the ESP32's internal sensor, not the room |
| SD | one icon for a card present, a different one for none |

The SD icon is the one worth glancing at before you start anything that
captures. It is the difference between a session you can read afterwards and
one you cannot.

---

## Getting around

**The screen is the input.** Touch a tile to open it. Inside a feature, a
row of buttons along the bottom does the work, and the labels change with
the feature. Common ones are `Back`, `Exit`, `Save`, `Next` and `Prev`.

**`Back` and `Exit` both leave**, and they are not always in the same place,
so read the row rather than aiming from memory.

**Menus longer than one screen are paged.** WiFi and Bluetooth both have two
pages, with the page button in the bottom row next to the back button. If a
feature you expected is missing from a menu, look at the second page before
concluding anything.

---

## The menu

Eight tiles, two columns of four, in column order:

```
WiFi        Bluetooth
NRF24       SubGHz
Detect      RFID/NFC
GPS         System
```

### WiFi

Page one: **Packet Monitor**, **Beacon Spammer**, **WiFi Deauther**,
**Probe Request Flood**, **Deauth Detector**, **WiFi Scanner**.

Page two: **Captive Portal**, **Hidden SSID Revealer**, **WPS Scanner**,
**ARP Scanner**, **Karma Attack**, **AP Tracker**.

**AP Tracker** is the one that behaves differently from the rest: pick an
access point from a scan and it locks to that channel and shows a signal
gauge that rises as you get closer. It is for walking a signal down rather
than reading a list.

**Beacon Spammer** broadcasts a list of network names. By default that is a
list built into the firmware, which is the same in every copy. Put your own
in `/ssids.txt` on the card and it uses those instead, and the screen tells
you which list it loaded when it starts.

### Bluetooth

Page one: **BLE Jammer**, **BLE Spoofer**, **Sour Apple**, **AirTag
Spoofer**, **AirTag Sniffer**, **Sniffer**.

Page two: **BLE Scanner**, **BLE Rubber Ducky**, **Skimmer Detect**,
**Hunt**, **Fast Pair**.

**Hunt** is the Bluetooth equivalent of AP Tracker: choose a device and walk
the gauge. **Skimmer Detect** looks for the BLE signatures card skimmers are
known to advertise.

**BLE Rubber Ducky** needs an ESP32-S3 and will tell you so on this
hardware. It is in the menu because the firmware is shared.

### NRF24

**Scanner**, **Proto Kill**, **ESB Sniffer**, **ESB Replay**, **MouseJack
Scan**, **MouseJack Inject**.

**Scanner** is a 2.4 GHz band sweep. It shows which channels are busy across
the whole band, which is the fastest way to see what is around you before
picking a tool. It is passive.

### SubGHz

**Replay Attack**, **SubGHz Jammer**, **De Bruijn / Brute**, **Jamming
Detector**, **Saved Profile**.

**Jamming Detector** is passive and listens for interference. The other four
use the CC1101.

### Detect

**Surveillance** and **Drone Detector**. Both are passive. Drone Detector
watches for the Remote ID broadcasts drones are required to send.

### RFID/NFC

**Card Reader**, **Card Clone**, **Erase**, **Dump**, **Decode Access**,
**Jam Reader**, **Tag Disrupt**, **Disrupt Emulate**.

All of these need the PN532, and all of them want the card within a couple
of centimetres of the coil. Read range on these modules is short and the
enclosure is built to preserve what there is, so hold the tag against the
back of the case rather than waving it nearby.

### GPS

**Wardriver** and **Satellite Scanner**.

**Wardriver** logs what it sees with positions, in a format you can upload
to WiGLE. **Satellite Scanner** shows the constellation and signal
strengths, which is how you tell a fix is coming before it arrives.

### System

**Serial Monitor**, **Update Firmware**, **Touch Calibrate**, **SD File
Manager**, **Settings**, **About**.

**Touch Calibrate** is worth running once on a new unit. **SD File Manager**
browses the card on the device, which saves pulling it out to check whether
a capture landed.

---

## What needs what

Nothing stops you opening a feature whose hardware is not fitted. Most will
open and then tell you the module is missing.

| Feature group | Needs |
|---|---|
| WiFi, Bluetooth, Detect | nothing extra, the ESP32 does it |
| NRF24 group | NRF24L01+PA+LNA |
| SubGHz group | CC1101 |
| RFID/NFC group | PN532 |
| GPS group | ATGM336H |
| anything that captures | an SD card, or the capture is lost |

---

## The SD card

FAT32, inserted in the slot in the side of the lid. The card can go in and
out without opening the case.

**What Pueo reads:**

| Path | What it is |
|---|---|
| `/ssids.txt` | your own network names for Beacon Spammer, one per line |
| `/config/settings.json` | your settings, written by the device |
| `/config/wigle.txt` | WiGLE upload credentials, if you use that |
| `/ducky/` | DuckyScript files |
| `/subghz/` | saved sub-GHz captures and profiles |

**What Pueo writes:**

| Path | What lands there |
|---|---|
| `/logs/` | feature logs |
| `/captures/` | packet captures |
| `/wd_wigle_upload.csv` | wardriving output, WiGLE format |
| `/jamdet.csv` | jamming detector events |
| `/esb/` | ESB sniffer captures |
| `/captive_portal/captured.csv` | captive portal results |

For `/ssids.txt`: one name per line, blank lines and lines starting with `#`
ignored, 32 characters maximum per name, first 64 used. There is a starter
list in [ssids.txt](ssids.txt) you can copy to the card.

---

## Settings

Under **System → Settings**.

| | |
|---|---|
| Brightness | screen backlight |
| Theme | dark or light |
| Accent colour | the highlight colour |
| Auto WiFi scan | scan on entering WiFi features |
| Auto BLE scan | scan on entering Bluetooth features |
| Stealth mode | see below |
| SD logging | master switch, plus one per feature group |

**SD logging has a master and per-feature switches**, and a feature logs
only when both say yes. Turning the master off leaves the card alone
entirely. It does not affect files you ask for by name, such as saving a
capture or a profile: those happen because you pressed the button.

Settings are saved to the card at `/config/settings.json`. Without a card
they last until reboot.

---

## Stealth mode

**Receive only.** Every tool whose job is to transmit refuses to start, and
scans that would transmit while looking passive are made genuinely passive.

It is off by default. When it is on and you open something it blocks, you
get a full-screen notice naming the feature rather than a silent failure.

What it refuses:

```
ARP Scanner        AirTag Spoofer      BLE Jammer          BLE Spoofer
Beacon Spammer     Captive Portal      De Bruijn / Brute   ESB Replay
Hidden SSID Rev.   Karma Attack        MouseJack Inject    Probe Req Flood
Proto Kill         Replay Attack       Saved Profile       Sour Apple
SubGHz Jammer      WPS Scanner         WiFi Deauther
```

Nineteen of them. Everything else stays available, which is most of the
passive side: the scanners, the detectors, Surveillance, the drone
detector, wardriving, NFC reading.

Use it when you want to be certain the device is only listening.

---

## When something says it is not there

**"No CC1101"** means the sub-GHz radio did not answer. The firmware reads
the chip's own part and version registers rather than assuming, so this is a
real answer and not a guess. Check the wiring and the chip select.

**A feature that opens and does nothing** is usually a module that is not
fitted. The status bar will not tell you: it shows the ESP32's own radios,
not the bolted-on ones.

**BLE Rubber Ducky refusing** is expected. It needs an ESP32-S3 and this is
an ESP32.

**Touch landing in the wrong place** wants **System → Touch Calibrate**.

**Captures not appearing** is worth checking in three places, in order: the
SD icon in the status bar, whether SD logging is on in Settings, and whether
the feature has its own switch turned off.

---

## A note on the 2.8 inch panel

Pueo builds for two boards. The 3.5 inch ESP32-3248S035R is the one this has
been run on. The 2.8 inch ESP32-2432S028R compiles and is released every
version, but nobody has booted one. If you are using it and something in
this guide does not match what you see, that is worth reporting rather than
assuming you have it wrong.
