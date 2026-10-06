#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * H.O.M.E UI kit — the firmware's own visual identity.
 *
 * H.O.M.E is our firmware, not a Pueo reskin: the screens the user lives in (the
 * profile picker, the playbook home, the plain-language status screen, the status
 * bar) share ONE identity defined here, so a restyle is one file and not fifty,
 * and so no screen reaches for a raw colour literal. check_ui_kit.py holds that
 * line.
 *
 * This header owns the Profile concept because the profile is first an identity
 * (which home screen, wording and accent you see) before it is a stored setting;
 * SettingsStore.h includes this for the enum. Kept dependency-free on purpose.
 * ──────────────────────────────────────────────────────────────────────────── */
#include <stdint.h>

/* The two audiences H.O.M.E serves on one device. The profile curates the home
 * screen, the wording and the accent. Persisted in AppSettings (SettingsStore.h). */
enum class Profile : uint8_t { Home = 0, Combat = 1 };

/* Profiles are collapsed for now (owner 2026-10-06): one unified device, every
 * tile and tool shown, no HOME/COMBAT toggle or first-boot picker. The profile
 * enum/persistence/picker code all stay in place -- flip this to 1 to bring the
 * two-profile UI back when HOME and COMBAT are made meaningfully different. */
#define HOME_PROFILES_ENABLED 0

/* Default accent per profile, as an index into kAccentPresets (SettingsStore.cpp).
 * HOME keeps the brand purple; COMBAT is red. These are the defaults applied when
 * a profile is first chosen -- the user can still change accentColor afterwards,
 * so the profile sets the look without locking it. The indices are cross-checked
 * against the preset table by check_ui_kit.py so a reordering there cannot leave
 * these pointing at the wrong colour. */
constexpr uint8_t HOME_UI_ACCENT_HOME   = 4;  /* Purple 0x79DD, the H.O.M.E brand */
constexpr uint8_t HOME_UI_ACCENT_COMBAT = 2;  /* Red 0xF800 */

inline uint8_t homeUiDefaultAccent(Profile p) {
  return (p == Profile::Combat) ? HOME_UI_ACCENT_COMBAT : HOME_UI_ACCENT_HOME;
}

/* Screen title / brand wording per profile. The combat name is the product's
 * own -- "HEADS OF MY ENEMIES" -- and the home name is plain. */
inline const char* homeUiProfileTitle(Profile p) {
  return (p == Profile::Combat) ? "HEADS OF MY ENEMIES" : "HOME";
}

/* Short tag for tight spots (status bar, a picker tile) where the full combat
 * title will not fit. */
inline const char* homeUiProfileTag(Profile p) {
  return (p == Profile::Combat) ? "COMBAT" : "HOME";
}

/* H.O.M.E screen metrics, shared by the picker, the playbook home and the status
 * screen so the look is set in one place. Tile placement still starts at the
 * menu's Y_START; these cover the H.O.M.E-specific framing the new screens draw. */
constexpr int HOME_UI_TILE_RADIUS = 6;
constexpr int HOME_UI_TILE_GAP    = 8;
constexpr int HOME_UI_PAD         = 10;
