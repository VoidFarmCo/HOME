#include "DeviceInfo.h"

#include "Stealth.h"

#include <NimBLEDevice.h>

namespace DeviceInfo {

namespace {

/* Device Information Service and the six string characteristics worth
 * showing. The others in 0x180A are System ID, the IEEE regulatory
 * certification blob and PnP ID, none of which read usefully on a small
 * screen and none of which answer "which build is this". */
const NimBLEUUID kSvcDeviceInfo((uint16_t)0x180A);

struct Field {
  uint16_t uuid;
  String Result::*slot;
};

const Field kFields[] = {
  {0x2A29, &Result::manufacturer},
  {0x2A24, &Result::model},
  {0x2A25, &Result::serial},
  {0x2A26, &Result::firmware},
  {0x2A27, &Result::hardware},
  {0x2A28, &Result::software},
};
constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

/* Strings here are whatever the device chose to put in them. Trim to
 * something a line of the display can hold, and drop anything not printable
 * so a device with a rubbish descriptor cannot scribble on the UI. */
String sanitise(const std::string& v) {
  String out;
  const size_t n = v.size() < 40 ? v.size() : 40;
  for (size_t i = 0; i < n; i++) {
    const char c = v[i];
    if (c == '\0') {
      break;
    }
    out += (c >= 0x20 && c < 0x7F) ? c : '.';
  }
  out.trim();
  return out;
}

}  // namespace

const char* statusText(Status s) {
  switch (s) {
    case Status::Ok:            return "read ok";
    case Status::NoClient:      return "no BLE client available";
    case Status::ConnectFailed: return "device did not accept a connection";
    case Status::NoService:     return "no Device Information Service";
    case Status::Refused:       return "refused: stealth mode";
  }
  return "?";
}

void read(const uint8_t addr[6], bool isPublic, Result& out) {
  out = Result();

  /* A GATT connection transmits, so this is one of the things stealth
   * turns off. Checked here rather than at the caller so a second caller
   * cannot forget. */
  if (Stealth::on()) {
    out.status = Status::Refused;
    return;
  }

  NimBLEAddress target(const_cast<uint8_t*>(addr),
                       isPublic ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM);

  NimBLEClient* client = NimBLEDevice::createClient();
  if (client == nullptr) {
    out.status = Status::NoClient;
    return;
  }
  client->setConnectTimeout(5);

  if (!client->connect(target, true)) {
    out.status = Status::ConnectFailed;
    NimBLEDevice::deleteClient(client);
    return;
  }

  NimBLERemoteService* svc = client->getService(kSvcDeviceInfo);
  if (svc == nullptr) {
    /* Connected, and the device simply does not publish this. Worth
     * distinguishing from a refused connection: one says "it will not talk
     * to you", the other says "it talked and had nothing to say". */
    out.status = Status::NoService;
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    return;
  }

  for (size_t i = 0; i < kFieldCount; i++) {
    NimBLERemoteCharacteristic* chr =
        svc->getCharacteristic(NimBLEUUID(kFields[i].uuid));
    if (chr == nullptr || !chr->canRead()) {
      continue;
    }
    const String v = sanitise(chr->readValue());
    if (v.length() > 0) {
      out.*(kFields[i].slot) = v;
      out.found++;
    }
  }

  out.status = Status::Ok;
  client->disconnect();
  NimBLEDevice::deleteClient(client);
}

}  // namespace DeviceInfo
