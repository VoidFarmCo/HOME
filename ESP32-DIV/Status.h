#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Status — H.O.M.E's plain-language "what happened / why / what to do" screen.
 *
 * Replaces silent or cryptic failures (a blank list, a serial-only message, a
 * feature that just does nothing) with a full-screen panel in plain words. Drawn
 * with the UI kit (homeAccent / UI_BG / UI_TEXT) so it looks like ours, and it
 * blocks until the user taps or presses a key, then returns so the caller can
 * repaint its own screen.
 * ──────────────────────────────────────────────────────────────────────────── */

namespace Status {

/* what = the headline ("No SD card"); why = one or two plain sentences; fix =
 * an optional "Try: ..." line telling the user what to do. */
void explain(const char* what, const char* why, const char* fix = nullptr);

}  // namespace Status
