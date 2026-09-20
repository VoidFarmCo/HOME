// =====================================================
//  PUEO - debossed logo fit test
//
//  The owl is line art, not a silhouette. Measured off
//  the artwork, the thinnest 5% of strokes are 8 px of a
//  474 px height, so at the 30 mm the lid allows they
//  land at 0.51 mm - one extrusion wide on a 0.4 nozzle.
//  Whether that reads, or fills in, or tears, is a
//  question about a specific printer, filament and layer
//  height, and no amount of staring at a render answers
//  it. Print this instead of a 60 g lid.
//
//  WHICH WAY UP, because it decides the whole test.
//  The lid is a tray: outer face, four walls, hollow
//  underneath. Printed outer-face-UP its ceiling is an
//  80 x 165 bridge, so it has to print outer-face-DOWN,
//  and the logo is therefore against the BED. Bed-side
//  grooves are a different animal from top-side ones -
//  glass-smooth floor, crisp walls, and the material
//  above them bridges a 0.5 mm gap without noticing.
//
//  So this tile carries the SAME owl on both faces:
//
//    underside  the real case - what the lid will get
//    top face   the comparison, for a printer where
//               top-side detail turns out to be better
//
//  They sit back to back in the same place, so the tile
//  is only as wide as the ladder needs.
//
//  Print it FLAT AND AS-IS. Do not flip it, do not let
//  the slicer auto-orient it. Everything meant for the
//  underside is already mirrored in the model, so it
//  reads correctly when you turn the tile over.
//
//  The ladder is on the underside with the real owl. It
//  is the part that generalises: eight grooves at the
//  same 0.6 mm depth and rising width, so the number
//  under the narrowest one that still reads is this
//  printer's floor for a debossed line.
//
//  From the command line:
//    openscad -o logo-fit-test.stl logo-fit-test.scad
//
//  HOW TO READ IT
//  1. Turn the tile over. The face with the ladder is
//     the one that printed against the bed, which is the
//     face the lid's logo will be on.
//  2. Look at that owl at arm's length. If the wifi arcs
//     and the eye rings have closed up into blobs, 30 mm
//     is too small for this artwork on this printer and
//     the answer is a simplified mark, not a deeper cut.
//  3. Compare it with the owl on the other face. If the
//     top-side one is clearly better, the lid wants a
//     different print orientation, which means supports
//     and is a real trade.
//  4. Read the ladder. The narrowest groove that still
//     has a visible floor is the limit. Anything in the
//     artwork thinner than that is decoration that will
//     not survive, and LOGO_H should go up until the
//     artwork's 0.51 mm strokes clear it.
//
//  magikh0e.pl
// =====================================================

use <pueo-enclosure.scad>

/* ---------- tile ---------- */
TILE_W  = 76;
TILE_L  = 56;
TILE_T  = 3.0;    // near the lid's own 2.5 mm skin
TILE_R  = 3;      // corner radius

/* ---------- what is under test ---------- */
TEST_H     = 30;    // the size the lid allows
TEST_DEPTH = 0.6;   // the shipped deboss depth

// Both owls sit at the same place in X and Y, on opposite faces, so the
// tile only has to be wide enough for the ladder.
OWL_Y   = 9;        // centre of both owls

/* ---------- groove ladder ---------- */
// Widths in mm. The point of the narrow end is to fail:
// 0.3 is below a 0.4 nozzle and should not appear at all.
LADDER    = [0.3, 0.4, 0.5, 0.6, 0.8, 1.0, 1.2, 1.6];
LADDER_Y  = -17;    // centre of the slots
LADDER_LEN = 10;    // how tall each groove is
LADDER_PITCH = 8.5;
LABEL_Y   = -25;
LABEL_SZ  = 3.2;

$fn = 48;

module rounded(w, l, r) {
    offset(r=r) offset(delta=-r) square([w, l], center=true);
}

// Everything cut into ONE face, authored as if seen from outside it.
module face_owl() {
    translate([0, OWL_Y]) owl2d(TEST_H);
}

module face_ladder() {
    n = len(LADDER);
    x0 = -LADDER_PITCH * (n - 1) / 2;
    for (i = [0:n-1]) {
        x = x0 + i * LADDER_PITCH;
        translate([x, LADDER_Y])
            square([LADDER[i], LADDER_LEN], center=true);
        translate([x, LABEL_Y])
            text(str(LADDER[i]), size=LABEL_SZ, halign="center",
                 valign="center", font="Liberation Sans");
    }
}

module tile() {
    difference() {
        linear_extrude(TILE_T) rounded(TILE_W, TILE_L, TILE_R);

        // ---- underside: the real case, against the bed ----
        // Mirrored so it reads the right way round when the tile is
        // turned over. The mirror wraps the whole group, so the ladder
        // still runs narrow-to-wide left to right from that side.
        translate([0, 0, -1])
            linear_extrude(TEST_DEPTH + 1)
                mirror([1, 0, 0]) {
                    face_owl();
                    face_ladder();
                }

        // ---- top face: the comparison ----
        translate([0, 0, TILE_T - TEST_DEPTH])
            linear_extrude(TEST_DEPTH + 1)
                face_owl();
    }
}

tile();
