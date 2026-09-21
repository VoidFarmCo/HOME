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

/* 3.5" numbers are from QDtech's E32R35T outline drawing, V1.0 2024-08-14,
 * not from calipers. The PCB is 55.50 x 101.50 x 5.80 mm, corners R3.50,
 * four mounting holes on a 47.90 x 94.50 pattern at 3.20 mm, and 5.09 mm of
 * SMD standing off the back. Unmarked tolerance on that drawing is +/-0.2.
 *
 * The window was 55.5 x 101.5 -- the board's exact outline, with the comment
 * claiming half a millimetre of clearance that was not in the arithmetic. An
 * FDM part printed to a board's exact size does not accept the board. 0.5 on
 * each dimension is the clearance the comment always meant; if your printer
 * runs tight, this is the number to open up rather than the one to scale.
 *
 * The 2.8" values are left alone: they came from measurement rather than a
 * drawing, and there is no E32R35T-equivalent outline for that board here to
 * check them against. */
BEZEL_W   = (PANEL == "3.5") ? 56.0 : 56.0;   // board + 0.5 clearance
BEZEL_L   = (PANEL == "3.5") ? 102.0 : 92.5;

/* Corner radius of the window. The 3.5" board's own corners are R3.50, so a
 * 2 mm window leaves four crescents of lid covering the PCB corners.
 *
 * Only the 2.8" cuts a board-sized window now; see the block below for what
 * the 3.5" does instead. */
BEZEL_R   = (PANEL == "3.5") ? 3.5 : 2;

/* ---------- 3.5" screen: aperture, rebate, and four M3 posts ----------
 *
 * Every number here is off QDtech's E32R35T outline drawing, V1.0
 * 2024-08-14, and the drawing's vertical chains close on the 101.50 overall
 * to the hundredth, which is why these are stated rather than measured.
 *
 *   PCB           55.50 x 101.50 x 5.80, corners R3.50
 *   LCD BL        55.50 x  84.96   the module outline, centred on the board
 *   RTP AA        49.96 x  77.24   what the aperture exposes
 *   LCD AA        48.96 x  73.44   the image, 0.50 inside the aperture
 *   holes         47.90 x  94.50   four at 3.20, centred on the board
 *   stack         1.20 RTP + 2.50 LCD + 0.50 tape = 4.20 above the PCB
 *
 * The window used to be the board's whole outline, so the PCB dropped
 * through the lid and nothing covered its edges. Now the lid face covers
 * the board and only the screen shows: an aperture at the touch panel's
 * active area, and behind it a rebate the module's shoulder sits up into.
 *
 * SCREEN_LIP is the lid material left in front of the module -- what the
 * module actually bears against, and what stops it falling out the front.
 * The glass therefore sits SCREEN_LIP below the outer face. At 1.0 mm that
 * reads as flush and protects the glass edge from a desk. Set it to 0 for
 * literally flush and the lid stops retaining the screen at all, which is
 * the trade rather than an oversight.
 *
 * The visible area is not concentric with the board: LCD AA sits 2.35 mm
 * toward the top. The old BEZEL_Y fudge was 2, arrived at by eye, which is
 * 0.35 off the drawing -- keep the derived number.
 */
SCREEN_APER_W  = 49.96;   // RTP AA; clears the LCD AA by 0.50 a side
SCREEN_APER_L  = 77.24;
SCREEN_APER_DY = 2.35;    // LCD AA centre above the board centre
SCREEN_APER_R  = 1.5;     // aperture corner radius, cosmetic

SCREEN_MOD_W   = 55.50 + 0.40;   // LCD BL + print clearance
SCREEN_MOD_L   = 84.96 + 0.40;
SCREEN_MOD_R   = 1.0;

SCREEN_LIP     = 1.0;     // lid left in front of the module
SCREEN_STACK   = 4.20;    // RTP + LCD + tape, above the PCB's top face
SCREEN_PCB_T   = 1.60;

/* Mount posts. The board's four holes are 3.20, which is an M3 clearance
 * hole, so the screw passes through the PCB from behind and threads into
 * the post. 2.50 is the pilot for a self-tapping M3 in PLA or PETG -- not
 * BOSS_HOLE, which is 3.00 and is sized for the base's machine screws;
 * 3.00 here would strip on the first drive.
 *
 * The posts land in the 8.27 mm of bare PCB above and below the module,
 * which is the only place on this board where the PCB's top face is
 * exposed -- the module is exactly as wide as the board. */
SCREEN_HOLE_DX = 47.90 / 2;
SCREEN_HOLE_DY = 94.50 / 2;
SCREEN_POST_R  = 3.0;     // 23.95 + 3.0 = 26.95, inside the 27.75 half-width
SCREEN_POST_PILOT = 1.25; // 2.50 dia

/* [verify] The RGB LED sits in the strip above the screen. Its position is
 * NOT on the outline drawing -- these two numbers are eyeballed off a photo
 * and are the only figures in this file that are not from a source.
 *
 * Measure yours before printing a lid you intend to keep: put a rule on the
 * board's top edge and its left edge, and set LED_X as the offset from the
 * board's centreline (negative is left) and LED_Y as the offset from the
 * board's centre (positive is up). Then delete this paragraph. */
SCREEN_LED_X   = -14.0;
SCREEN_LED_Y   =  46.4;   // ~4.3 down from the top edge, mid-strip
SCREEN_LED_D   =  3.2;    // a light pipe, or just a hole

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
            // 3.5" screen posts, hanging off the plate down to the PCB's
            // top face. Height falls out of the stack: the module's front
            // bears on the lip, so the PCB sits SCREEN_STACK below it.
            if (PANEL == "3.5")
                for (dx = [-1, 1], dy = [-1, 1])
                    translate([dx * SCREEN_HOLE_DX,
                               BEZEL_Y + dy * SCREEN_HOLE_DY,
                               LID_H - SCREEN_LIP - SCREEN_STACK])
                        cylinder(h = SCREEN_STACK + SCREEN_LIP - 2.5,
                                 r = SCREEN_POST_R);
        }
        // The window. On the 2.8" this is still the board's outline; on the
        // 3.5" it is the screen's aperture, with the rebate cut below it.
        if (PANEL == "3.5") {
            // aperture, through the lip
            translate([0, BEZEL_Y + SCREEN_APER_DY, LID_H - 2.5 - 1])
                linear_extrude(2.5 + 2)
                    rrect(SCREEN_APER_W, SCREEN_APER_L, SCREEN_APER_R);
            // rebate for the module, from the plate's underside up to the lip
            translate([0, BEZEL_Y, LID_H - 2.5])
                linear_extrude(2.5 - SCREEN_LIP + 0.01)
                    rrect(SCREEN_MOD_W, SCREEN_MOD_L, SCREEN_MOD_R);
        } else {
            translate([0, BEZEL_Y, -1])
                linear_extrude(LID_H+2) rrect(BEZEL_W, BEZEL_L, BEZEL_R);
        }
        // [verify] RGB LED, in the strip above the screen
        if (PANEL == "3.5")
            translate([SCREEN_LED_X, BEZEL_Y + SCREEN_LED_Y, LID_H - 2.5 - 1])
                cylinder(h = 2.5 + 2, d = SCREEN_LED_D);
        // screw clearance holes, countersunk from outside
        for (p = BOSS_POS)
            translate([p[0], p[1], 0]) {
                translate([0,0,-1]) cylinder(h=LID_H+2, r=1.7);
                translate([0,0,LID_H-2.2]) cylinder(h=2.4, r1=1.7, r2=3.2);
            }
        // pilot holes down the screen posts
        if (PANEL == "3.5")
            for (dx = [-1, 1], dy = [-1, 1])
                translate([dx * SCREEN_HOLE_DX,
                           BEZEL_Y + dy * SCREEN_HOLE_DY,
                           LID_H - SCREEN_LIP - SCREEN_STACK - 1])
                    cylinder(h = SCREEN_STACK + 2, r = SCREEN_POST_PILOT);
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
