#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Branding — everything the boot screen and serial banner say.
 *
 * Upstream kept these strings as XOR-obfuscated byte arrays (OBF_PN, OBF_DN
 * and friends in shared.h) so they would not turn up in `strings` on a built
 * binary. That is CiferTech's choice about their build and it is a reasonable
 * one; there is no reason to carry it here. Pueo's own name is in plain text,
 * and so is the credit back to the project it is built on.
 *
 * ESP32-DIV is MIT licensed. The licence requires the copyright notice be
 * kept, which LICENSE does. The attribution below is not required by the
 * licence -- it is here because the code came from somewhere and saying so
 * costs nothing.
 * ──────────────────────────────────────────────────────────────────────────── */

#define PUEO_NAME        "Pueo"
#define PUEO_TAGLINE     "multi-radio field tool"

/* Shown under the name on the splash, and in the serial banner. */
#define PUEO_UPSTREAM    "based on ESP32-DIV by CiferTech"
#define PUEO_UPSTREAM_URL "github.com/cifertech/ESP32-DIV"

/* ── Boot logo ───────────────────────────────────────────────────────────────
 * Define PUEO_LOGO_BITMAP to a 150x150 1-bpp array to draw a logo above the
 * name. Generate one with:
 *
 *   py -3 tools/make_bitmap.py logo.png --name pueo_logo --size 150x150
 *
 * and paste the output into icon.h.
 *
 * Deliberately not defaulting to bitmap_icon_cifer: that is CiferTech's logo,
 * and drawing it under the name "Pueo" would misattribute rather than
 * rebrand. Until there is artwork the splash is text only.
 *
 * The ten-frame loading animation (bitmap_icon_skull_loading_1..10, 100x120)
 * is also upstream artwork and is still in use. Replace it the same way when
 * you have frames of your own. */
// #define PUEO_LOGO_BITMAP bitmap_pueo_logo

#define PUEO_LOGO_W 150
#define PUEO_LOGO_H 150
