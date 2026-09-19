// =====================================================
//  PUEO - NRF24 pocket fit test
//
//  The NRF24 PA+LNA is 15.5 x 41 in a pocket cut 16 x 41.
//  pockets() adds 0.6, so the hole is really 16.6 x 41.6:
//  1.1 mm of slack across the width, 0.6 mm along the
//  length. Nominal-to-nominal the length has none at all,
//  which is why this is the one pocket worth proving on
//  the printer before the base geometry is committed.
//
//  Two parts, chosen by PART near the bottom:
//
//    PART = "ladder"   (default) five pockets at rising
//                      clearance, labelled in mm of TOTAL
//                      gap over the module. Print this
//                      first - it is flat and quick, and
//                      it answers the question on its own.
//
//    PART = "insitu"   a slice of the real base around the
//                      NRF24 pocket: top wall, SMA hole,
//                      corner boss. Not a re-drawing - it
//                      is an intersection of base() itself,
//                      so it cannot drift from the part.
//                      Print this second, to check that
//                      the module's edge SMA lines up with
//                      the bulkhead hole.
//
//  From the command line:
//    openscad -o nrf24-ladder.stl nrf24-fit-test.scad
//    openscad -D 'PART="insitu"' -o nrf24-insitu.stl nrf24-fit-test.scad
//
//  HOW TO READ THE LADDER
//  Drop the module into each pocket in turn, starting at
//  0.2. The number on the smallest pocket it seats into
//  flat, without forcing, is the clearance this printer
//  needs. Feed that back into pockets() in the enclosure.
//  If even 1.0 is tight, the pocket is too small and the
//  MODULES entry has to grow, not just the kerf.
//
//  magikh0e.pl
// =====================================================

use <pueo-enclosure.scad>

/* ---------- the part under test ---------- */
// Measured module outline. If your NRF24 differs, change
// these two numbers and reprint - everything follows them.
MOD_W = 15.5;
MOD_L = 41.0;

/* ---------- ladder ---------- */
GAPS   = [0.2, 0.4, 0.6, 0.8, 1.0];   // total, i.e. half per side
LABELS = ["0.2","0.4","0.6","0.8","1.0"];

PLATE_T  = 4.0;    // matches FLOOR in the enclosure
DEPTH    = 2.0;    // matches POCKET
RIB      = 3.0;    // wall between pockets
BORDER   = 3.0;
STRIP    = 10.0;   // front margin carrying the labels

GMAX  = max(GAPS);
PITCH = MOD_W + GMAX + RIB;

PLATE_W = len(GAPS)*PITCH - RIB + 2*BORDER;
PLATE_D = MOD_L + GMAX + BORDER + STRIP;

// Pockets 2 mm deep hold a tight module firmly enough that
// it will not come out with fingernails. Each one gets a
// window through the floor to push it back out.
EJECT_W = 10;
EJECT_L = 20;

function cellx(i) = BORDER + i*PITCH + (MOD_W + GMAX)/2;
CELLY = STRIP + (MOD_L + GMAX)/2;

$fn = 48;

module ladder() {
    difference() {
        cube([PLATE_W, PLATE_D, PLATE_T]);

        for (i = [0:len(GAPS)-1]) {
            // the pocket itself
            translate([cellx(i) - (MOD_W + GAPS[i])/2,
                       CELLY   - (MOD_L + GAPS[i])/2,
                       PLATE_T - DEPTH])
                cube([MOD_W + GAPS[i], MOD_L + GAPS[i], DEPTH + 1]);
            // eject window, straight through
            translate([cellx(i) - EJECT_W/2, CELLY - EJECT_L/2, -1])
                cube([EJECT_W, EJECT_L, PLATE_T + 2]);
        }
    }

    // labels, raised so they survive the first layer
    for (i = [0:len(GAPS)-1])
        translate([cellx(i), STRIP/2, PLATE_T])
            linear_extrude(0.6)
                text(LABELS[i], size=5, halign="center", valign="center");
}

/* ---------- in-situ slice ---------- */
// The window cut out of the real base. Right wall is at
// W/2 = 42.5, top wall at L/2 = 85. Z stops above the SMA
// hole so the module can be dropped in from the top.
SLICE = [[6, 34, 0], [42.5, 85, 15]];

module insitu() {
    // moved to the origin so it lands on a build plate as cut
    translate([-SLICE[0][0], -SLICE[0][1], 0])
    intersection() {
        base();
        translate(SLICE[0])
            cube([SLICE[1][0]-SLICE[0][0],
                  SLICE[1][1]-SLICE[0][1],
                  SLICE[1][2]-SLICE[0][2]]);
    }
}

PART = "ladder";

if (PART == "insitu") insitu(); else ladder();
