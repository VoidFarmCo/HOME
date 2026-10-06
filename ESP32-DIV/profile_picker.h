#pragma once
/* First-boot profile picker — asks, once, which audience this H.O.M.E is for.
 *
 * Shown from setup() when settings().profileChosen is false (a fresh card, or a
 * settings.json from before profiles existed). Writes the choice, its default
 * accent, and the chosen flag before the menu ever appears, so it asks exactly
 * once. Drawn in the H.O.M.E UI kit -- our screen, not an inherited one. */
namespace ProfilePicker {
void run();
}
