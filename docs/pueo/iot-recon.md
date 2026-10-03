# IoT Recon (ARP Scanner)

The ARP Scanner joins a network and lists the live hosts on it. IoT Recon then
deep-dives those hosts the way HaleHound's does: probe common service ports,
fingerprint HTTP and RTSP, and test default HTTP Basic-Auth credentials. It runs
over the connection the ARP Scanner already made, so it is plain TCP with no
radio-mode change.

It is a phase of the ARP Scanner, not a separate menu entry. The WiFi grid is
full at twelve tiles, and recon needs the join and the host list the ARP Scanner
already has.

## Using it

1. Open **WiFi, ARP Scanner**. Pick an AP and **Join** it (enter the password);
   it sweeps the subnet and lists the hosts.
2. Press **Recon** (the left slot) to scan every discovered host. The body turns
   into a rolling log: each host's IP, then its open ports with the service
   name, HTTP `Server:` banners, RTSP response lines, and any default
   credential that worked.
3. Press **Stop** to return to the host list, or **Exit** to leave.

Rescan (re-run the ARP sweep) is still on the top undo icon, so moving Recon to
the left slot costs nothing.

## What it probes

Per host, a curated port set: FTP, SSH, Telnet, DNS, HTTP (80/88/8000/8080),
HTTPS, SMB, RTSP (554/8554), Modbus, MQTT, and the Dahua/XMEye camera ports
(34567/37777). On an open HTTP port it grabs the `Server:` header; if that port
answers **401** (auth required) it tries a built-in list of default credentials
and reports one only on an authenticated **200**. On an open RTSP port it logs
the first response line.

Findings roll on screen (last 15 lines) and append to **`/captures/iot_recon.txt`**
on the SD card when one is present.

## How it stays responsive

The scan is cooperative: `reconStep()` probes **one port per loop tick** and
returns, so the UI redraws and **Stop** is always live. The alternative, looping
every port of every host in one call, would freeze the device for the length of
a full scan. All recon state (the log, the report file handle, the position) is
one heap struct allocated when recon starts and freed when it stops or the
feature exits, because this board's DRAM has no room for it in `.bss`.

Recon needs the joined connection: it refuses to start unless connected, and
aborts back to the host list if the connection drops mid-scan.

## What a check holds

`tools/check_iot_recon.py` pins the ways this misleads or wedges:

- default creds are tried only against a host that returned 401,
- a credential is reported working only on an authenticated 200,
- the Basic-Auth base64 encoder matches the standard library (so logins are real),
- the scan advances one port per tick (UI and Stop stay live),
- recon needs a joined connection, to start and to continue,
- the heap state is freed on stop and on leaving the feature.

Each was broken on purpose to confirm the check fails, per `CONTRIBUTING.md`.
