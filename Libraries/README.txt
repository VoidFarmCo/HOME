These three files are consumed by tools/build.sh setup. Nothing here needs
installing by hand.

  platform.txt           upstream's patched core platform.txt, copied over
                         the stock one. build.sh strips -w and -zmuldefs
                         from it; see docs/pueo/warnings.md and zmuldefs.md
  TFT_eSPI-master.zip    upstream customised TFT_eSPI, so it cannot come
                         from Library Manager
  User_Setup cyd.h       the display config for this board, copied over
                         TFT_eSPI/User_Setup.h

Removed from this folder, all of it upstream's and none of it used here:
the SmartRC CC1101 zip, which is superseded by the vendored copy in
libs/SmartRC-CC1101-Driver-Lib (see its VENDORED.md); the User_Setup
variants for the v1 and v2 boards; and the duplicate "v1 Libraries" set for
the original ESP32-DIV hardware. All recoverable from the upstream remote.
