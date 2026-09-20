// =====================================================
//  PUEO enclosure - base and lid, both in this file
//  85 x 170 x 20, zoned for RF/NFC/power separation
//  every module below is editable - see MODULES list
//
//  THIS FILE CONTAINS BOTH PARTS. Which one renders is
//  decided by PART, near the bottom. Opening the file
//  in the GUI shows only the base, which is why the lid
//  looks missing.
//
//    PART = "base"   the base           (default)
//    PART = "lid"    the lid, alone
//    PART = "both"   both, side by side, for looking at
//                    -- do not export this one to print
//
//  From the command line:
//    openscad -o base.stl pueo-enclosure.scad
//    openscad -D 'PART="lid"' -o lid.stl pueo-enclosure.scad
//
//  In the GUI: edit PART below, or open the Customizer
//  (Window > Customizer) and pick it there.
//
//  magikh0e.pl
// =====================================================

/* ---------- envelope ---------- */
W        = 85;      // outer width
L        = 170;     // outer length
H        = 20;      // outer height of the base
WALL     = 2.5;
FLOOR    = 4.0;     // general floor thickness
POCKET   = 2.0;     // how deep board recesses sit
NFC_FLOOR = 1.4;    // thin window under the PN532 coil
FILLET   = 3;       // outer corner radius

/* ---------- fasteners ---------- */
BOSS_R   = 4.0;
BOSS_HOLE = 1.5;    // M3 self-tap pilot
BOSS_INSET = 6.0;

/* ---------- wall penetrations ---------- */
SMA_D     = 6.5;    // SMA bulkhead hole
SMA_X     = [-23, 22];   // 45 mm apart, aligned to each radio
                         // both radios have board-mounted SMAs,
                         // so these MUST line up with the boards
// Both radios carry their own edge-mounted SMA, so the bulkhead height is
// not a free choice: it is wherever the module's connector ends up once the
// module is stacked on the carrier board. Derived rather than asserted.
STANDOFF_H  = 2.5;  // pocket floor to carrier underside; clears TH leads
PCB_T       = 1.6;  // carrier thickness
SOCKET_H    = 0;    // 0 = radios soldered down, 5.0 = low-profile socket
MOD_PCB_T   = 1.0;  // radio module board thickness            [VERIFY]
SMA_AXIS_H  = 2.5;  // SMA axis above the module's own board   [VERIFY]

SMA_Z = STANDOFF_H + PCB_T + SOCKET_H + MOD_PCB_T + SMA_AXIS_H;
echo(str("SMA_Z = ", SMA_Z, " mm above the pocket floor"));

// Room a soldered-down module has above the carrier. Every module has to
// fit inside this; the MT3608 at its disputed 14 mm does not.
CARRIER_HEADROOM = (H - (FLOOR - POCKET)) - STANDOFF_H - PCB_T;
echo(str("room above the carrier = ", CARRIER_HEADROOM, " mm"));

// The TP4056 in hand has a micro-USB jack, not Type-C. The receptacle is
// about 8 x 3; the cutout has to clear the plug's metal shell with slack for
// print tolerance, not the overmoulded boot, which stays outside the wall.
USB_W     = 11;     // cutout width  (jack is ~8)
USB_HT    = 6;      // cutout height (jack is ~3)
USB_Y     = -62;    // RIGHT wall - the jack is on the board's short end

/* ---------- GPS antenna slot (top centre) ---------- */
GPS_W     = 21;     // slot width
GPS_D     = 7;      // how far back from the inner wall; the antenna
                    // board is 20 x 6 and lies flat, so 5 was too shallow
GPS_FLOOR = 1.6;    // thin plastic under the antenna
GPS_OPEN  = 6;      // height of the matching wall opening

/* ---------- vents ---------- */
VENT_W   = 2.2;
VENT_Z0  = -2;
VENT_Z1  =  6;

/* =====================================================
   MODULE LAYOUT
   [ x, y, width, depth, name ]
   x,y = centre.  Origin is the middle of the case.
   Footprints taken from the original case pockets.
   ===================================================== */
MODULES = [
  // --- power zone, bottom ---
  [ -19, -65,  36, 17, "MT3608 boost"   ],
  [  26.6, -62, 27, 17, "TP4056 charger" ],  // right edge flush to the right wall
  [ -29, -48,  20, 12, "MP2307 buck"    ],   // independent rail for the PA radios
                                             // part is 17.9 x 12, pocket left oversized

  // --- battery ---
  [ -16, -23,  45, 34, "LiPo pack"      ],

  // --- NFC ---
  [   0,  18,  43, 41, "PN532 V3"       ],   // <-- gets the thin floor

  // --- radios: vertical, board SMA against the top wall ---
  // Pockets deliberately larger than the parts: CC1101 is 15 x 38 in a
  // 15 x 40, NRF24 is 15.5 x 41 in a 16 x 41. pockets() adds 0.6 more.
  [ -23, 61.5, 15, 40, "HW-863 CC1101"  ],
  [  22, 61,   16, 41, "NRF24 PA+LNA"   ],

  // --- GPS, in the corridor between the radios ---
  // Moved up from (24,-21): the antenna is on a 90 mm u.FL pigtail and
  // could not reach the top-wall slot from down there.
  [   0,  52,  16, 13, "ATGM336H GPS"   ]
];

// Looked up by name so reordering MODULES cannot silently point the thin
// floor at the wrong part.
NFC_INDEX = [for (i = [0:len(MODULES)-1]) if (MODULES[i][4] == "PN532 V3") i][0];

$fn = 48;

/* ---------- helpers ---------- */

module rrect(w, d, r) {
    hull() for (sx=[-1,1], sy=[-1,1])
        translate([sx*(w/2-r), sy*(d/2-r)]) circle(r=r);
}

module shell() {
    difference() {
        linear_extrude(H) rrect(W, L, FILLET);
        // interior cavity
        translate([0,0,FLOOR])
            linear_extrude(H) rrect(W-2*WALL, L-2*WALL, max(FILLET-WALL,0.5));
    }
}

BOSS_POS = [[-36.5,-79],[36.5,-79],[-36.5,5],[36.5,5],[-36.5,70],[36.5,70]];
module bosses() {
    for (p = BOSS_POS)
        translate([p[0], p[1], 0])
            difference() {
                cylinder(h=H, r=BOSS_R);
                translate([0,0,FLOOR-1]) cylinder(h=H, r=BOSS_HOLE);
            }
}

module pockets() {
    for (i = [0:len(MODULES)-1]) {
        m = MODULES[i];
        depth = (i == NFC_INDEX) ? (FLOOR - NFC_FLOOR) : POCKET;
        translate([m[0], m[1], FLOOR - depth + 50/2])
            cube([m[2]+0.6, m[3]+0.6, 50], center=true);
    }
}

// vertical stadium slot through a wall with normal +/-Y
module slot_Y(xpos, ypos) {
    translate([xpos, ypos, 0]) rotate([-90,0,0])
        linear_extrude(height=12, center=true)
            hull() {
                translate([0, FLOOR+VENT_Z0+VENT_W/2]) circle(r=VENT_W/2);
                translate([0, FLOOR+VENT_Z1-VENT_W/2]) circle(r=VENT_W/2);
            }
}

// vertical stadium slot through a wall with normal +/-X
module slot_X(xpos, ypos) {
    translate([xpos, ypos, 0]) rotate([0,90,0])
        linear_extrude(height=12, center=true)
            hull() {
                translate([-(FLOOR+VENT_Z0+VENT_W/2), 0]) circle(r=VENT_W/2);
                translate([-(FLOOR+VENT_Z1-VENT_W/2), 0]) circle(r=VENT_W/2);
            }
}

module gps_slot() {
    // recess for the flat active antenna, hard against the top wall
    translate([0, L/2 - WALL - GPS_D/2 + 0.01, FLOOR])
        cube([GPS_W, GPS_D, 40], center=false, $fn=4);
}

module penetrations() {
    // GPS antenna recess + its opening through the wall
    translate([-GPS_W/2, L/2 - WALL - GPS_D, GPS_FLOOR])
        cube([GPS_W, GPS_D + WALL + 1, 40]);
    translate([-GPS_W/2, L/2 - WALL - 1, GPS_FLOOR])
        cube([GPS_W, WALL + 2, GPS_OPEN]);

    // SMA bulkheads, top wall
    for (x = SMA_X)
        translate([x, L/2, FLOOR - POCKET + SMA_Z])
            rotate([90,0,0]) cylinder(h=4*WALL, r=SMA_D/2, center=true);

    // micro-USB, RIGHT wall. Z is anchored to the pocket floor, which is
    // only right while the modules sit on the floor -- see CARRIER note.
    translate([W/2, USB_Y, FLOOR - POCKET - 0.5 + USB_HT/2])
        cube([4*WALL, USB_W, USB_HT], center=true);

    // vents - bottom wall, clear of the USB cutout
    for (x = [-34,-28,-22,-16,-10,-4]) slot_Y(x, -L/2);
    // vents - left wall alongside the power zone
    for (y = [-76,-70,-64,-58,-52]) slot_X(-W/2, y);
}

/* ---------- assembly ---------- */
// "base", "lid", or "both". See the header. "both" is for viewing only:
// it lays the two parts out side by side, which is not a printable STL.
PART = "base";

module base() {
    difference() {
        union() { shell(); bosses(); }
        pockets();
        penetrations();
    }
}

/* =====================================================
   LID  -  render with:  openscad -D PART=\"lid\" ...
   Opening sized for the existing CYD bezel (55.5 x 92)
   ===================================================== */
LID_H     = 12;

/* Which CYD the lid is cut for. Both run the same firmware a define
 * apart, and both are 55 mm across the board -- the 3.5" is simply 9 mm
 * longer, so only the window length and what sits under it change.
 *
 *   2.8"  ESP32-2432S028R  ILI9341  240x320  board 55.5 x 92
 *   3.5"  ESP32-3248S035R  ST7796   320x480  board 55.0 x 101   [VERIFY]
 *
 * The 3.5" figures come from the vendor's dimensioned drawing (55 x 101
 * stated) with the glass measured against it at 54.8 x 84.4, sitting
 * 8.6 mm below the top edge and 8.0 mm above the bottom. Confirm with
 * calipers before printing a lid you intend to keep.
 *
 * Render with:  openscad -D PART=\"lid\" -D PANEL=\"3.5\" ...
 */
PANEL     = "2.8";

BEZEL_W   = (PANEL == "3.5") ? 55.5 : 56.0;   // board + 0.5 clearance
BEZEL_L   = (PANEL == "3.5") ? 101.5 : 92.5;
/* The longer window eats into the chin. Nudging it 2 mm toward the top,
 * where nothing lives, keeps a printable margin around the owl. */
BEZEL_Y   = (PANEL == "3.5") ? 2 : 0;         // shift the screen up/down the face

/* ---------- owl, debossed into the lid face ----------
 *
 * pueo-owl.svg is traced from the logo by tools/trace_logo.py, which checks
 * its own output by rasterising it back and comparing: 0.02% of the artwork
 * differs at the tolerance it ships with. Re-run the tracer rather than
 * editing the SVG.
 *
 * Debossed rather than embossed, and that is about the artwork. This is line
 * art, not a silhouette: measured across the bitmap, the thinnest 5% of
 * strokes are 8 px of a 474 px height, so at LOGO_H they come out at
 * LOGO_H * 8/474 mm -- 0.51 mm at 30, 0.34 mm at 20. A raised 0.5 mm rib
 * 0.6 mm tall is a fragile single extrusion that can shear off the face. The
 * same feature as a groove is a narrow channel in otherwise solid top
 * layers, which is the forgiving direction to fail in: it may close up a
 * little, but it cannot break off.
 *
 * Going below about 25 mm puts the fine strokes under a 0.4 mm nozzle
 * entirely, at which point the wifi arcs and the eye rings stop existing.
 * If a smaller mark is wanted, the answer is different artwork, not a
 * smaller copy of this one.
 *
 * LOGO_Y sits it in the chin, between the screen window and the bottom
 * wall. That band runs -82.5 to -46.25, and the screw pillars either side
 * are at x +/-36.5, which a ~29 mm wide logo on the centreline never reaches.
 */
LOGO       = true;   // false leaves the face blank
LOGO_H     = 30;     // artwork height in mm; see the note above before shrinking
LOGO_DEPTH = 0.6;    // cut into a 2.5 mm skin, so 1.9 mm is left under it
LOGO_Y     = (PANEL == "3.5") ? -65.5 : -64;   // centre of the chin band
LOGO_SVG   = "pueo-owl.svg";
LOGO_PX_W  = 462;    // the SVG's own viewBox, so scaling stays honest
LOGO_PX_H  = 474;

/* The artwork, centred on the origin, scaled to `height` millimetres.
 *
 * resize() rather than a computed scale factor, because the size the SVG
 * arrives at is not the size it says. The file is 462 x 474 px and carries
 * width="462px", and OpenSCAD converts px to mm at a fixed 96 dpi, so the
 * import is really 122.2 x 125.4 mm. Scaling by height/474 on top of that
 * gives a logo 3.78x too small -- 7.9 mm where 30 was asked for. Passing
 * dpi=25.4 to import() does not help: the explicit px dimensions win.
 *
 * resize([0, height], auto=true) sidesteps all of it by measuring the
 * bounding box that actually turned up and scaling that to fit, with 0
 * meaning "keep this axis proportional". Whatever units the SVG claims, the
 * owl comes out `height` tall.
 *
 * Worth knowing how this was caught, because a picture did not do it. A
 * 7.9 mm owl sitting in the chin looks much like a 30 mm one at lid scale,
 * and the render looked right. What found it was arithmetic: 108,773 ink px
 * at 30 mm tall and 0.6 mm deep has to remove about 261 mm^3, and measuring
 * the lid's volume before and after showed 18. Measure it again if this is
 * ever changed.
 */
module owl2d(height) {
    resize([0, height], auto = true)
        import(LOGO_SVG, center = true);
}

module lid() {
    difference() {
        union() {
            // shell
            difference() {
                linear_extrude(LID_H) rrect(W, L, FILLET);
                translate([0,0,-1])
                    linear_extrude(LID_H-2.5+1)
                        rrect(W-2*WALL, L-2*WALL, max(FILLET-WALL,0.5));
            }
            // screw pillars matching the base bosses
            for (p = BOSS_POS)
                translate([p[0], p[1], 0]) cylinder(h=LID_H, r=BOSS_R);
        }
        // CYD window
        translate([0, BEZEL_Y, -1])
            linear_extrude(LID_H+2) rrect(BEZEL_W, BEZEL_L, 2);
        // screw clearance holes, countersunk from outside
        for (p = BOSS_POS)
            translate([p[0], p[1], 0]) {
                translate([0,0,-1]) cylinder(h=LID_H+2, r=1.7);
                translate([0,0,LID_H-2.2]) cylinder(h=2.4, r1=1.7, r2=3.2);
            }
        // owl, cut into the outer face
        if (LOGO)
            translate([0, LOGO_Y, LID_H - LOGO_DEPTH])
                linear_extrude(LOGO_DEPTH + 1)
                    owl2d(LOGO_H);
    }
}


if (PART == "both") {
    base();
    // clear of the base, plus a 10 mm gap
    translate([W + 10, 0, 0]) lid();
} else if (PART == "lid") {
    lid();
} else {
    base();
}
