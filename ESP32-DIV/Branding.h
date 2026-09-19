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

/* Pueo's own version. The sketch still carries ESP32DIV_VERSION, which is
 * upstream's release this was forked from -- showing that on the splash
 * would claim to be an ESP32-DIV build it no longer is. Both appear in the
 * serial banner, which is the honest way round. */
#define PUEO_VERSION     "0.1.0"

#define PUEO_NAME        "Pueo"
#define PUEO_TAGLINE     "multi-radio field tool"

/* Shown under the name on the splash, and in the serial banner. */
#define PUEO_UPSTREAM    "based on ESP32-DIV by CiferTech"
#define PUEO_UPSTREAM_URL "github.com/cifertech/ESP32-DIV"

/* ── Boot logo ───────────────────────────────────────────────────────────────
 * Define PUEO_LOGO_BITMAP to a 150x150 1-bpp array to draw a logo above the
 * name. Generate one with:
 *
 *   py -3 tools/make_bitmap.py art/pueo_owl_src.jpg --name pueo_logo  *       --size 180x180 --crop --invert
 *
 * and paste the output into icon.h.
 *
 * The size ceiling comes from the layout, not from taste. displayLogo() puts
 * the logo at y = 140 - H/2, so the text below it has to fit in what is left
 * of a 320px panel. With the name line that text block is 82px and H caps at
 * 196; with PUEO_LOGO_HAS_WORDMARK it is 46px and H caps at about 240, where
 * the 240px panel width becomes the real limit. 200 leaves 24px of slack.
 *
 * The ten-frame loading animation (bitmap_icon_skull_loading_1..10, 100x120)
 * is also upstream artwork and is still in use. Replace it the same way when
 * you have frames of your own. */
#define PUEO_LOGO_BITMAP bitmap_pueo_logo

/* Set when the artwork already contains the wordmark, so displayLogo() drops
 * its separate name line instead of printing "Pueo" twice. Dropping that line
 * also frees 26px, which is what lets the logo go to 200 -- see the layout
 * note above. */
#define PUEO_LOGO_HAS_WORDMARK 1

#define PUEO_LOGO_W 200
#define PUEO_LOGO_H 200
