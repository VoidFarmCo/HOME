// =====================================================
//  PUEO enclosure - base and lid, both in this file
//  84 x 142 x 20, or 85 x 170 with the external power stack
//  zoned for RF/NFC/power separation
//  every module below is editable - see MODULES list
//
//  THIS FILE CONTAINS BOTH PARTS. Which one renders is
//  decided by PART, in the BUILD OPTIONS block directly
//  below. Opening the file in the GUI shows only the
//  base, which is why the lid looks missing.
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
//  Every knob that changes what comes out is in the one
//  block below. The reasoning behind each stays further
//  down, beside the geometry it decides.
//
//  magikh0e.pl
// =====================================================

/* ══════════════════ BUILD OPTIONS ══════════════════
 * The five that change what you get. Everything else in this file is
 * geometry that follows from them.
 *
 *   PART       "base" | "lid" | "both"   which part renders
 *   PANEL      "3.5"  | "2.8"            which CYD it is cut for
 *   EXT_CHARGER  false  | true             a TP4056 rides inside
 *   INSERTS    true   | false            M3 heat-set, or self-tapped
 *   LOGO       false  | true             the owl in the lid
 *
 * "both" is for looking at, not for printing: it lays the two parts side
 * by side in one solid.
 *
 * A base and a lid have to come from the same PANEL. The bezel moves with
 * the panel and the microSD slot is placed from the bezel, so the base
 * differs per panel as well, not just the lid.
 *
 * From the command line, -D overrides any of these:
 *   openscad -D 'PART="lid"' -D 'PANEL="2.8"' -o lid-28.stl pueo-enclosure.scad
 */
PART      = "base";
PANEL     = "3.5";
EXT_CHARGER = false;
INSERTS   = true;
LOGO      = false;
/* ═══════════════════════════════════════════════════ */

/* ---------- envelope ---------- */
/* ---- does your CYD charge its own battery? ----------------------------
 *
 * EXT_CHARGER = false   the board does it. 84 x 142.
 * EXT_CHARGER = true    it does not, so a TP4056 and an MT3608 ride inside
 *                       and the case grows to 85 x 170.
 *
 * The name is about the CHARGER, not about where the power comes from.
 * Running the thing off a USB lead is the ordinary case and needs no flag:
 * the lid opening is cut either way. This one asks whether a charging
 * circuit has to be carried in the case because the board lacks one.
 *
 * On the reference 3.5" it does not. Adding a TP4056 there buys nothing,
 * because the FM5324GA described below already is one.
 *
 * The reference board -- Sunton ESP32-3248S035R -- carries an FM5324GA
 * beside its BAT1 connector, with a 2.2 uH inductor next to it. That part
 * is a single-chip single-cell Li-ion charger AND synchronous 5 V boost,
 * with automatic load detection and no-load shutdown. It is a TP4056 and
 * an MT3608 in one package, already soldered down, already wired to a
 * battery connector. Buying both again to sit in the case beside it is
 * buying the same two functions twice.
 *
 * They are what made the case wide. Side by side the two modules spanned
 * 79.1 mm of the 79.1 mm of content, in a case whose next widest zone is
 * the radios at 60.5. Deleting them takes 85 x 170 to 78 x 142 -- 23% of
 * the volume -- and fixes a height problem by deletion, because
 * CARRIER_HEADROOM is 13.9 mm and the MT3608 is 14.
 *
 * [VERIFY] which boards have it. Confirmed on the 3.5" ESP32-3248S035R in
 * hand, by reading the part number off the chip. Sunton's own connector
 * list gives the 2.8" ESP32-2432S028R a battery connector too, so it very
 * likely has the same arrangement -- but nobody here has looked at what is
 * behind it. lcdwiki's boards are unknown. If yours has no BAT1, or has one
 * with no charger behind it, set EXT_CHARGER = true.
 *
 * [VERIFY] with EXT_CHARGER = false the +3V3_RF buck has to take 5 V from
 * somewhere, and the candidate is the 5V pin on P1. Nobody has confirmed
 * that pin is an output when the board is running from BAT1 rather than
 * from USB. Measure it before you rely on it.
 *
 * What does NOT become optional is the MP2307 buck. That is not about
 * making 5 V, it is about keeping the PA radios' current steps off
 * whatever feeds the display, and it is needed either way.
 */
// EXT_CHARGER is set in BUILD OPTIONS at the top.

/* Width is set by the corner bosses, not by the modules. The radios reach
 * x = -30.5 and 30, so a boss clearing CC1101 must sit at or left of -34.5
 * and one clearing NRF24 at or right of 34.0; symmetric that is +/-34.5,
 * and containing a 4 mm radius needs 77.0 of outer width. 78 is the next
 * even number -- at 76 the boss overhangs the shell by half a millimetre.
 * The modules alone would fit in 68; the extra 10 mm buys a screw in each
 * corner, which matters because the panel is resistive and gets pressed. */
/* Width is set by the corner bosses, and they are set by the radios.
 *
 * The radios sit at x -23 (15 wide) and 22 (16 wide) and run y 27..68, so
 * they occupy the top of the case across nearly its whole width. A boss
 * clears them only outboard: at BOSS_R 4.5 that is x <= -35.0 on the left
 * and x >= 34.5 on the right. BOSS_X 36 takes 1.0 mm of margin on the
 * tighter side, and containing the radius with 1.5 mm of skin outside it
 * needs 84 of width.
 *
 * BOSS_Y 66 rather than 64: at 64 the boss lands exactly on the inner face
 * of the end wall and the tangency makes the base non-manifold. 66 pushes
 * it 2 mm into the wall so the two merge, which is what you want anyway --
 * a boss tied into a wall is stiffer than one standing next to it.
 *
 * Both numbers were swept against OpenSCAD rather than reasoned about. The
 * arithmetic says 80 x 142 is enough and it is not: at 80 the boss stands
 * 0.5 mm proud of the outside, which renders clean and prints as a bump.
 *
 * The modules alone would fit in 68. Corner screws and brass cost 16 mm of
 * width -- most of the saving left is the length, 170 -> 142.
 */
BOSS_X   = 36;      // corner boss centres, small case
BOSS_Y   = 66;
W        = EXT_CHARGER ? 85 : 84;
L        = EXT_CHARGER ? 170 : 142;
H        = 20;      // outer height of the base
WALL     = 2.5;
FLOOR    = 4.0;     // general floor thickness
POCKET   = 2.0;     // how deep board recesses sit
NFC_FLOOR = 1.4;    // thin window under the PN532 coil
FILLET   = 3;       // outer corner radius

/* ---------- fasteners ----------
 *
 * INSERTS = true    brass heat-set inserts in the base's bosses, M3
 *                   machine screws through the lid into them
 * INSERTS = false   the screw cuts its own thread in the plastic
 *
 * Inserts are worth it here because this lid comes off. A thread cut
 * straight into PLA survives a handful of cycles and then strips, and the
 * first thing you do with a field tool is open it again to fix something.
 *
 * Only the base changes. The lid already has an M3 clearance hole (r=1.7,
 * so 3.4) and a countersink, which is what you want either way.
 *
 * Sizing, for the common M3 insert -- about 4.6 mm across the knurl and
 * 5.0 long:
 *
 *   hole    4.2 diameter. The knurl bites the last few tenths as the brass
 *           melts in; 4.6 would spin, 3.8 would push a bulge out of the
 *           boss wall.
 *   boss    9.0 across, so 2.4 of plastic around the insert. At the old
 *           8.0 it would have been 1.9, which splits in PLA often enough
 *           to matter.
 *   depth   the bore runs from FLOOR-1 to the top of the boss, about 17,
 *           so insert length is not a constraint.
 *
 * [VERIFY] measure your own inserts. 4.6 x 5.0 is the usual cheap kit but
 * they vary, and the hole wants to match the knurl, not the thread.
 */
// INSERTS is set in BUILD OPTIONS at the top.
BOSS_R    = INSERTS ? 4.5 : 4.0;   // radius: 9.0 or 8.0 across
BOSS_HOLE = INSERTS ? 2.1 : 1.5;   // radius: 4.2 for an insert, 3.0 for a screw
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

// Charging is the CYD's own USB now that the TP4056 is gone, so this cutout
// is for the board's socket rather than a charger module's. The CYD has both
// micro-USB and USB-C on this revision; the opening is sized for the larger.
/* Two different sockets, on two different parts.
 *
 * With EXT_CHARGER the jack is the TP4056's own micro-USB. That module sits in
 * the base, on its short end against the right wall, so the opening is a
 * rectangle through the base's long side and these three numbers place it.
 *
 * Without EXT_CHARGER there is no TP4056 and charging goes through the CYD's
 * own socket. The CYD is mounted in the LID, with its long axis down the
 * length of the case, so its sockets are at the short END. USB_Y was a
 * placeholder of 0 for this case and the opening it fed was on the base's
 * long side: the wrong wall of the wrong part, which is why it never lined
 * up with anything.
 *
 * The two are not alternatives. The lid opening is cut either way, because
 * the CYD's socket is on the board either way and it is what you flash
 * through. EXT_CHARGER only decides whether the BASE also gets a hole for a
 * charger sitting next to it.
 */
USB_W     = 11;     // EXT_CHARGER only: the TP4056 jack is ~8 wide
USB_HT    = 6;
USB_Y     = -62;    // and where it sits along the base's right wall

/* The lid opening, measured off a working print rather than derived.
 *
 * A 3MF with the cut as a negative cylinder gave lid extents of 84 x 142 x
 * 12, which is W x L x LID_H exactly, so the transform that produced these
 * is sound.
 *
 * USB_D is the hole that worked, not the socket. At 14 it is wider than the
 * lid is thick, so it takes the full wall height and there is room to shrink
 * it once somebody measures the socket itself. Left generous deliberately:
 * a cable that will not seat is worse than an opening that shows daylight,
 * and this is the end you plug into. */
USB_LID_X = -0.5;   // very nearly centred on the width
USB_LID_Z = 1.0;    // above the lid's mating face, near the board's underside
USB_LID_D = 14.0;

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
// Two layouts, because the power zone is a different size in each. Pockets
// are deliberately larger than the parts: CC1101 is 15 x 38 in a 15 x 40,
// NRF24 is 15.5 x 41 in a 16 x 41, and pockets() adds 0.6 more.
//
// The radios sit 45 mm apart in both, x = -23 and 22. That separation is
// what this whole zoned layout exists for and it is not a free variable.

MODULES_ONBOARD = [
  // The charger and the 5 V boost are on the CYD. What is left down here is
  // the independent rail for the PA radios.
  [   0, -60,  20, 12, "MP2307 buck"    ],   // part is 17.9 x 12
  // Centred, and its leads go to the CYD's own BAT1.
  [   0, -35,  45, 34, "LiPo pack"      ],
  [   0,   4,  43, 41, "PN532 V3"       ],   // <-- gets the thin floor
  [ -23,  48,  15, 40, "HW-863 CC1101"  ],
  [  22,  47.5,16, 41, "NRF24 PA+LNA"   ],
  [   0,  40,  16, 13, "ATGM336H GPS"   ]    // corridor between the radios
];

MODULES_EXTERNAL = [
  // Three in the power zone, and they are what makes this case 85 wide.
  [ -19, -65,  36, 17, "MT3608 boost"   ],
  [  26.6, -62, 27, 17, "TP4056 charger" ],  // right edge flush to the wall
  [ -29, -48,  20, 12, "MP2307 buck"    ],
  [ -16, -23,  45, 34, "LiPo pack"      ],
  [   0,  18,  43, 41, "PN532 V3"       ],
  [ -23,  61.5,15, 40, "HW-863 CC1101"  ],
  [  22,  61,  16, 41, "NRF24 PA+LNA"   ],
  // Moved up from (24,-21): the antenna is on a 90 mm u.FL pigtail and
  // could not reach the top-wall slot from down there.
  [   0,  52,  16, 13, "ATGM336H GPS"   ]
];

MODULES = EXT_CHARGER ? MODULES_EXTERNAL : MODULES_ONBOARD;

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

/* Six lid screws. The middle pair is clear of the SD slot; the rest are at
 * the corners.
 *
 * The middle pair sat at y = 5 and put the left one squarely across the
 * 15 mm of gap the card crosses between the wall and the socket mouth. Not
 * across the board or the socket -- it is outboard of both -- but across the
 * card. Reported from a printed lid with a card that would not go in.
 *
 * It moved UP, to 24, and getting there took two goes.
 *
 * The first attempt moved it DOWN instead, to y = -1.5, on the argument that
 * a screw beside the slot stiffens the edge the 16 mm hole weakens, and that
 * the spans come out evener: 77.5 and 71.5 against 103 and 46. Both of those
 * are true and neither survived the part.
 *
 * Down is pinched between the slot and the battery pocket, so it only fits
 * with the radius cut to 3.5 -- and that left the cylinder exactly tangent
 * to the wall face, which is coincident, which is not a 2-manifold, so it
 * needed moving half a millimetre outboard as well. Three adjustments
 * holding each other up. And it still was not clean: the slot's cutting
 * prism runs 4*WALL long, so it bit a 3 x 0.86 x 4 mm notch out of the
 * corner of the boss it had just been necessary to narrow. A notch that
 * carries nothing, was never asked for, and takes material off the thinnest
 * boss in the model.
 *
 * Up needs none of that. Full radius, original x, 2.9 mm clear of the slot
 * and 5.4 mm clear of the card, nothing tangent to anything, no notch. The
 * uneven spans are a real cost and the right one to pay: 103 mm between
 * screws is a stiffness argument, and the alternative was three interacting
 * fudges around a part that has to print. */
// Five, not six. At 85 mm wide there was room outboard of the radios for a
// boss in each top corner; at 68 the radios reach x = -30.5 and 30 and there
// is not. Both top bosses landed inside a radio pocket -- caught by checking
// the placement against MODULES rather than by looking at the render, where
// a boss cut away by a pocket still looks like a boss.
//
// The replacement is a single one up the middle, in the corridor between the
// radios and above the GPS. One screw across the top instead of two.
// One in each corner on the small case. x=+/-34.5 is the nearest the radios
// allow -- see the note on W -- and y=+/-64 puts them 7 mm inside the end
// walls with a 4 mm radius, so they merge into the corner fillet.
//
// Checked against every pocket AND against the GPS antenna slot, which is a
// wall feature rather than a module. Checking MODULES alone is how an
// earlier centre boss ended up inside that slot: OpenSCAD reported it as a
// non-manifold base and the render looked fine.
//
// The 85 x 170 case keeps its original six: it is long enough that four
// would leave too much unsupported bezel in the middle.
BOSS_POS = EXT_CHARGER
  ? [[-36.5,-79],[36.5,-79],[-36.5,24],[36.5,24],[-36.5,70],[36.5,70]]
  : [[-BOSS_X,-BOSS_Y],[BOSS_X,-BOSS_Y],[-BOSS_X,BOSS_Y],[BOSS_X,BOSS_Y]];
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

    // micro-USB, RIGHT wall, for the TP4056 that is only here with
    // EXT_CHARGER. Z is anchored to the pocket floor, which is only right
    // while the modules sit on the floor -- see CARRIER note.
    //
    // Without EXT_CHARGER the socket you plug into is the CYD's own, and that
    // is in the lid at the far end. See USB_LID_* and lid().
    if (EXT_CHARGER)
        translate([W/2, USB_Y, FLOOR - POCKET - 0.5 + USB_HT/2])
            cube([4*WALL, USB_W, USB_HT], center=true);

    // vents - bottom wall, clear of the USB cutout
    for (x = EXT_CHARGER ? [-34,-28,-22,-16,-10,-4] : [-15,-9,-3,3,9,15])
        slot_Y(x, -L/2);
    // vents - left wall alongside the power zone
    for (y = EXT_CHARGER ? [-76,-70,-64,-58,-52] : [-56,-50,-44,-38,-32])
        slot_X(-W/2, y);
}

/* ---------- assembly ---------- */
// PART is set in BUILD OPTIONS at the top. "both" is for viewing only:
// it lays the two parts out side by side, which is not a printable STL.

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
 *   3.5"  ESP32-3248S035R  ST7796   320x480  board 55.0 x 101   [CONFIRMED]
 *
 * The 3.5" figures come from the vendor's dimensioned drawing (55 x 101
 * stated) with the glass measured against it at 54.8 x 84.4, sitting
 * 8.6 mm below the top edge and 8.0 mm above the bottom.
 *
 * 2026-09-24: the 3.5" lid has been printed and the board fits it. That
 * retires the [VERIFY] these numbers carried, and it settles a question no
 * amount of reading could -- see the note on BEZEL_W below.
 *
 * Default is 3.5 as of 2026-09-23: it is the reference board, the one the
 * firmware and this case are built for, and the only one anything has run
 * on. It was 2.8 until then, which meant every default render -- including
 * the pictures on the site -- was the panel nobody has.
 *
 * It is not only the lid. BEZEL_Y shifts with the panel and the base's
 * microSD slot opening is placed from it, so the BASE differs per panel
 * too. A base built at one setting does not pair with a lid built at the
 * other.
 *
 * Render with:  openscad -D PART=\"lid\" -D PANEL=\"2.8\" ...
 *
 * PANEL is set in BUILD OPTIONS at the top.
 */

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
 * check them against.
 *
 * Two things were wrong with the provenance and neither turned out to
 * matter. QDtech's E32R35T is lcdwiki's board, not Sunton's -- the same
 * wrong-board substitution that put an audio amplifier on GPIO 4 and
 * invented an I2C JST. And Sunton's own datasheet gives the module as
 * 101.5 x 54.9, which would make 55.50 too wide by 0.6.
 *
 * The printed lid accepts the board, so 56.0 is right in the only sense
 * that counts. If 54.9 is the true width the clearance is 1.1 rather than
 * 0.5 -- looser, not tighter, which is why a drawing of the wrong board
 * still produced a part that fits. Do not tighten it on the strength of the
 * datasheet: that document says 240x320 and 320x480 four lines apart. */
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

/* Mount posts. The board's four holes are 3.20 -- that figure is the
 * board's, off the outline sheet, and it is an M3 clearance hole. What
 * actually threads into the post is smaller: M2.5 x 5, confirmed by
 * driving them into a printed lid, where they bite and hold without
 * splitting the post.
 *
 * So the 3.20 is loose around an M2.5 shank. It does not matter here --
 * the post's shoulder locates the board, not the screw -- but it is the
 * reason the two numbers disagree, and the reason this paragraph exists.
 *
 * 2.50 is the pilot -- not BOSS_HOLE, which is 3.00 and is sized for the
 * base's machine screws; 3.00 here would strip on the first drive.
 *
 * The posts land in the 8.27 mm of bare PCB above and below the module,
 * which is the only place on this board where the PCB's top face is
 * exposed -- the module is exactly as wide as the board. */
SCREEN_HOLE_DX = 47.90 / 2;
SCREEN_HOLE_DY = 94.50 / 2;
SCREEN_POST_R  = 3.0;     // 23.95 + 3.0 = 26.95, inside the 27.75 half-width
SCREEN_POST_PILOT = 1.25; // 2.50 dia

/* Blind. The first cut ran the pilot the full height and it surfaced on the
 * outer face -- four holes through the front of a lid whose whole point is
 * that only the screen shows.
 *
 * 2026-09-24, on a printed lid: the hole pattern lines up and the screws do
 * not come through. Both halves confirmed by fit, which is the only check
 * either of them was ever going to get -- 47.90 x 94.50 came from a drawing
 * of lcdwiki's board, and the vendor datasheet that agrees with it is the
 * one claiming two different resolutions four lines apart.
 *
 * Measured up from the PCB's top face, so the number is the thread
 * engagement rather than a coordinate. 4.0 leaves 1.2 mm of solid lid over
 * the hole, which is six layers at 0.2 and is the figure to raise if your
 * printer leaves the last one thin.
 *
 * With the PCB at 1.60 that wants a 5 mm screw: 3.4 mm of engagement and
 * 0.6 spare at the bottom. A 6 mm bottoms out before it clamps. */
SCREEN_SCREW_DEPTH = 4.0;

/* The RGB LED sits in the strip above the screen. Its position is NOT on
 * the outline drawing -- these two numbers were eyeballed off a photo, and
 * were the only figures in this file that did not come from a source.
 *
 * A printed lid puts the hole over the LED, so they are close enough to
 * keep -- that was a 3.5" lid printed 2026-09-20, the day these went in.
 * Still true of the current one: the 84 x 142 rework shrank the case around
 * the screen plate without moving it, and every input to this hole
 * (LED_X/Y/D, BEZEL_Y, LID_H, SCREEN_LIP, and the translate itself) is
 * unchanged since. Checked against db70919 rather than assumed.
 *
 * Still the softest numbers here: if yours misses, put a rule on the
 * board's top and left edges and set LED_X as the offset from the board's
 * centreline (negative is left) and LED_Y from its centre (positive up).
 *
 * The hole stays although Pueo does not drive an RGB LED. Those pins are
 * repurposed -- on the 3.5", GPIO 16 is NRF24 CE and 17 is PN532 SS -- so
 * once the radios are wired this hole shows blue and green flickering on
 * every SPI transaction. Kept on purpose as of 2026-09-24: it is a free
 * activity light. It is not a stealth leak in the sense check_stealth.py
 * means, which walks transmit paths, but it is light coming out of a case,
 * so decide for yourself before printing a lid for a quiet room.
 *
 * Do not "fix" it as a stray hole over a dead LED. It was looked at. */
SCREEN_LED_X   = -14.0;
SCREEN_LED_Y   =  46.4;   // ~4.3 down from the top edge, mid-strip
SCREEN_LED_D   =  3.2;    // a light pipe, or just a hole

/* ---------- micro-SD access, 3.5" lid ----------
 *
 * The card goes in the socket on the BACK of the display board, which the
 * lid holds face-down. Without an opening the only way to the slot is four
 * screws and lifting the screen out -- reported from a printed case, not
 * predicted. A card is not a thing you fit once: it comes out to be read.
 *
 * The socket sits hard against one long edge of the PCB -- about 3 mm in --
 * and ejects sideways, so this is a slot through the lid's side wall rather
 * than a hole in the face. It is still cut oversize: the card is 11 mm wide
 * and 1 mm thick and the opening is 16 x 4, which leaves room for print
 * tolerance and for a fingernail, and the socket itself is 16 mm along the
 * board so nothing is gained by matching the card instead.
 *
 * Both numbers come off lcdwiki's own outline drawing, E32R35T_Size.pdf,
 * rather than off a photograph or a ruler.
 *
 * SD_Y, from the chain of dimensions down the right of its Back view. The
 * leader from the socket runs to the top of 17.87, which chains through
 * 14.54 at the UART header and 25.48 at the BAT header to the bottom edge
 * of the PCB:
 *
 *     17.87 + 14.54 + 25.48 = 57.89 mm from the socket to the bottom edge
 *     101.50 - 57.89        = 43.61 mm from the TOP edge
 *     101.50/2 - 43.61      =  7.14 mm above the board's centre
 *
 * SD_SIDE, from which side of that same view the socket sits on. It is the
 * right-hand side there, with the UART and BAT headers -- and a Back view
 * mirrors, so it is the LEFT edge looking at the screen, which is negative
 * X here. The I2C, SPI and SPEAKER headers are on the other side in both
 * the drawing and a photograph of the board, which is the check that the
 * two views agree about handedness.
 *
 * Measuring the drawing's own pixels puts the socket centre at 42.6 mm from
 * the top against the 43.61 the dimension chain gives, and its mouth about
 * 3 mm in from the edge. The dimensioned number is the one used.       */
SD_SLOT   = (PANEL == "3.5");
SD_SIDE   = -1;       // left edge viewed from the front, per the Back view
SD_Y      = 7.14;     // mm above the board centre, from the dimension chain
SD_W      = 16;       // opening along the board's length
SD_HT     = 4;        // opening through the wall's height

/* Z falls out of the screen stack rather than being chosen. The PCB's back
 * face is the reference: the module's front bears on the lip, so the front
 * face is SCREEN_LIP below the outer face and the back face is one PCB
 * thickness and one screen stack below that. The socket stands about 2 mm
 * proud of it, and the slot straddles that. */
SD_Z      = LID_H - SCREEN_LIP - SCREEN_STACK - SCREEN_PCB_T - 1.0;

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
// LOGO is set in BUILD OPTIONS at the top; true puts the owl in the chin.
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
/* ---------- owl artwork, generated by tools/inline_logo.py ---------- */
/*
 * 32 closed polygons, 2893 points, straight out of
 * docs/pueo/pueo-owl.svg. Do not hand-edit: change the SVG and re-run
 * tools/inline_logo.py. The SVG is still published beside the source
 * for anyone who wants the artwork itself.
 *
 * y is flipped and the outline is centred on its own bounding box,
 * which is what import(center = true) did.
 */
owl_points = [
  [-182,236.5], [-181.5,237], [-155.5,237], [-154.5,236], [-114.5,236], [-113.5,235],
  [-110.5,235], [-109.5,234], [-108.5,234], [-104,229.5], [-104,228.5], [-103,227.5],
  [-103,225.5], [-101,222.5], [-100,217.5], [-98,214.5], [-97,209.5], [-95.5,207],
  [-92.5,207], [-91.5,208], [-89.5,208], [-88.5,209], [-85.5,209], [-84.5,210],
  [-82.5,210], [-81.5,211], [-79.5,211], [-78.5,212], [-75.5,212], [-74.5,213],
  [-71.5,213], [-70.5,214], [-67.5,214], [-66.5,215], [-62.5,215], [-61.5,216],
  [-56.5,216], [-55.5,217], [-50.5,217], [-49.5,218], [-43.5,218], [-42.5,219],
  [-34.5,219], [-33.5,220], [-19.5,220], [-18.5,221], [17.5,221], [18.5,220],
  [32.5,220], [33.5,219], [42.5,219], [43.5,218], [49.5,218], [50.5,217],
  [55.5,217], [56.5,216], [60.5,216], [61.5,215], [65.5,215], [66.5,214],
  [69.5,214], [70.5,213], [73.5,213], [74.5,212], [77.5,212], [78.5,211],
  [80.5,211], [81.5,210], [84.5,210], [85.5,209], [87.5,209], [88.5,208],
  [91.5,208], [92.5,207], [94.5,207], [97,210.5], [97,212.5], [98,213.5],
  [99,218.5], [101,221.5], [101,223.5], [102,224.5], [102,226.5], [104,229.5],
  [104,230.5], [107.5,234], [108.5,234], [109.5,235], [112.5,235], [113.5,236],
  [154.5,236], [155.5,237], [180.5,237], [184,234.5], [184,229.5], [185,228.5],
  [185,214.5], [182.5,212], [181.5,212], [180.5,211], [175.5,211], [172,208.5],
  [172,206.5], [173.5,204], [182.5,204], [185,201.5], [185,187.5], [184,186.5],
  [184,179.5], [183,178.5], [183,177.5], [179.5,175], [175.5,175], [174.5,174],
  [165.5,174], [164.5,173], [158.5,173], [157,171.5], [157,170.5], [160,167.5],
  [160,166.5], [165,160.5], [165,159.5], [167,157.5], [168,154.5], [170,152.5],
  [170,151.5], [173,146.5], [173,144.5], [177,137.5], [177,135.5], [178,134.5],
  [178,132.5], [179,131.5], [179,129.5], [180,128.5], [180,126.5], [181,125.5],
  [181,123.5], [182,122.5], [182,119.5], [183,118.5], [183,115.5], [184,114.5],
  [184,110.5], [185,109.5], [185,103.5], [186,102.5], [186,95.5], [187,94.5],
  [187,81.5], [188,80.5], [188,38.5], [189,37.5], [189,29.5], [190,28.5],
  [190,25.5], [192,22.5], [192,20.5], [193,19.5], [196,12.5], [198,10.5],
  [200,5.5], [202,3.5], [202,2.5], [203,1.5], [203,0.5], [204,-0.5],
  [204,-1.5], [205,-2.5], [205,-3.5], [211,-14.5], [211,-16.5], [213,-19.5],
  [213,-21.5], [215,-24.5], [215,-26.5], [216,-27.5], [216,-29.5], [217,-30.5],
  [217,-32.5], [218,-33.5], [218,-35.5], [219,-36.5], [219,-39.5], [220,-40.5],
  [220,-42.5], [221,-43.5], [221,-46.5], [222,-47.5], [222,-50.5], [223,-51.5],
  [223,-55.5], [224,-56.5], [224,-59.5], [225,-60.5], [225,-65.5], [226,-66.5],
  [226,-70.5], [227,-71.5], [227,-76.5], [228,-77.5], [228,-83.5], [229,-84.5],
  [229,-92.5], [230,-93.5], [230,-104.5], [231,-105.5], [231,-134.5], [230,-135.5],
  [230,-145.5], [229,-146.5], [229,-154.5], [228,-155.5], [228,-161.5], [227,-162.5],
  [227,-168.5], [226,-169.5], [226,-173.5], [225,-174.5], [225,-178.5], [224,-179.5],
  [224,-183.5], [223,-184.5], [223,-188.5], [222,-189.5], [222,-192.5], [221,-193.5],
  [221,-196.5], [220,-197.5], [220,-200.5], [219,-201.5], [219,-204.5], [218,-205.5],
  [218,-207.5], [217,-208.5], [217,-211.5], [216,-212.5], [216,-214.5], [215,-215.5],
  [215,-217.5], [214,-218.5], [214,-221.5], [213,-222.5], [213,-224.5], [211,-227.5],
  [211,-229.5], [210,-230.5], [209,-233.5], [206.5,-236], [189.5,-236], [189,-235.5],
  [189,-231.5], [190,-230.5], [190,-228.5], [191,-227.5], [191,-224.5], [192,-223.5],
  [192,-219.5], [193,-218.5], [193,-214.5], [194,-213.5], [194,-209.5], [195,-208.5],
  [195,-204.5], [196,-203.5], [196,-197.5], [197,-196.5], [197,-189.5], [198,-188.5],
  [198,-180.5], [199,-179.5], [199,-165.5], [200,-164.5], [198.5,-163], [197.5,-163],
  [193.5,-166], [190.5,-167], [188,-170.5], [188,-174.5], [187,-175.5], [187,-185.5],
  [186,-186.5], [186,-193.5], [185,-194.5], [185,-200.5], [184,-201.5], [184,-205.5],
  [183,-206.5], [183,-210.5], [182,-211.5], [182,-215.5], [181,-216.5], [181,-219.5],
  [180,-220.5], [180,-223.5], [179,-224.5], [179,-226.5], [178,-227.5], [178,-230.5],
  [177,-231.5], [176,-234.5], [174.5,-236], [153.5,-236], [152,-234.5], [152,-230.5],
  [153,-229.5], [153,-225.5], [154,-224.5], [154,-220.5], [155,-219.5], [155,-214.5],
  [156,-213.5], [156,-207.5], [157,-206.5], [157,-199.5], [158,-198.5], [158,-190.5],
  [159,-189.5], [159,-177.5], [160,-176.5], [160,-140.5], [161,-139.5], [161,-138.5],
  [162.5,-137], [164.5,-137], [177.5,-150], [178.5,-150], [183.5,-153], [191.5,-153],
  [192.5,-152], [194.5,-152], [195.5,-151], [196.5,-151], [218,-129.5], [218,-128.5],
  [220,-125.5], [220,-122.5], [221,-121.5], [221,-117.5], [220,-116.5], [220,-113.5],
  [219,-112.5], [218,-109.5], [206,-97.5], [206,-96.5], [205,-95.5], [205,-88.5],
  [206,-87.5], [206,-77.5], [205,-76.5], [205,-71.5], [204,-70.5], [204,-66.5],
  [203,-65.5], [202,-60.5], [201,-59.5], [201,-58.5], [200,-57.5], [197,-50.5],
  [195,-48.5], [195,-47.5], [191,-43.5], [191,-42.5], [181.5,-34], [180.5,-34],
  [178.5,-32], [175.5,-31], [173.5,-29], [171.5,-29], [168.5,-27], [166.5,-27],
  [165.5,-26], [163.5,-26], [162.5,-25], [159.5,-25], [158.5,-24], [147.5,-24],
  [146.5,-25], [140.5,-25], [139.5,-24], [138.5,-24], [136,-21.5], [136,-20.5],
  [132,-16.5], [132,-15.5], [126,-9.5], [126,-8.5], [119.5,-2], [118.5,-2],
  [113.5,3], [112.5,3], [111.5,4], [107.5,4], [106.5,3], [97.5,3],
  [96.5,4], [88.5,4], [87.5,5], [82.5,5], [81.5,6], [77.5,6],
  [76.5,7], [70.5,7], [69.5,6], [68.5,6], [67.5,5], [66.5,5],
  [65.5,4], [64.5,4], [63.5,3], [56.5,0], [54.5,-2], [53.5,-2],
  [52.5,-3], [51.5,-3], [50.5,-4], [49.5,-4], [48.5,-5], [47.5,-5],
  [46.5,-6], [45.5,-6], [44.5,-7], [43.5,-7], [42.5,-8], [41.5,-8],
  [40.5,-9], [39.5,-9], [38.5,-10], [31.5,-13], [29.5,-15], [28.5,-15],
  [27.5,-16], [26.5,-16], [25.5,-17], [24.5,-17], [23.5,-18], [22.5,-18],
  [21.5,-19], [20.5,-19], [19.5,-20], [18.5,-20], [17.5,-21], [16.5,-21],
  [15.5,-22], [14.5,-22], [13.5,-23], [12.5,-23], [11.5,-24], [10.5,-24],
  [9.5,-25], [2.5,-28], [1.5,-29], [-1.5,-29], [-2.5,-28], [-4.5,-28],
  [-5.5,-27], [-6.5,-27], [-7.5,-26], [-8.5,-26], [-9.5,-25], [-10.5,-25],
  [-21.5,-19], [-22.5,-19], [-24.5,-17], [-25.5,-17], [-26.5,-16], [-27.5,-16],
  [-28.5,-15], [-29.5,-15], [-30.5,-14], [-31.5,-14], [-32.5,-13], [-33.5,-13],
  [-34.5,-12], [-35.5,-12], [-36.5,-11], [-37.5,-11], [-38.5,-10], [-39.5,-10],
  [-40.5,-9], [-47.5,-6], [-49.5,-4], [-50.5,-4], [-51.5,-3], [-52.5,-3],
  [-53.5,-2], [-54.5,-2], [-55.5,-1], [-56.5,-1], [-57.5,0], [-58.5,0],
  [-59.5,1], [-60.5,1], [-71.5,7], [-77.5,7], [-78.5,6], [-82.5,6],
  [-83.5,5], [-88.5,5], [-89.5,4], [-98.5,4], [-99.5,3], [-107.5,3],
  [-108.5,4], [-111.5,4], [-112.5,3], [-114.5,3], [-132,-14.5], [-132,-15.5],
  [-136,-19.5], [-136,-20.5], [-138.5,-23], [-139.5,-23], [-140.5,-24], [-158.5,-24],
  [-159.5,-25], [-161.5,-25], [-164.5,-27], [-166.5,-27], [-167.5,-28], [-170.5,-29],
  [-172.5,-31], [-175.5,-32], [-178.5,-35], [-179.5,-35], [-182.5,-38], [-183.5,-38],
  [-188.5,-42], [-190.5,-42], [-191.5,-43], [-198.5,-46], [-200.5,-48], [-203.5,-49],
  [-205.5,-51], [-206.5,-51], [-208.5,-53], [-209.5,-53], [-212.5,-56], [-213.5,-56],
  [-216.5,-59], [-217.5,-59], [-220.5,-62], [-222.5,-62], [-225,-59.5], [-225,-58.5],
  [-224,-57.5], [-224,-53.5], [-223,-52.5], [-223,-49.5], [-222,-48.5], [-222,-45.5],
  [-221,-44.5], [-221,-41.5], [-220,-40.5], [-220,-38.5], [-219,-37.5], [-219,-35.5],
  [-218,-34.5], [-218,-31.5], [-217,-30.5], [-217,-28.5], [-215,-25.5], [-214,-20.5],
  [-212,-17.5], [-212,-15.5], [-210,-12.5], [-210,-10.5], [-209,-9.5], [-206,-2.5],
  [-204,-0.5], [-202,4.5], [-200,6.5], [-198,11.5], [-196,13.5], [-196,14.5],
  [-193,19.5], [-193,21.5], [-191,24.5], [-191,26.5], [-190,27.5], [-190,31.5],
  [-189,32.5], [-189,77.5], [-188,78.5], [-188,92.5], [-187,93.5], [-187,100.5],
  [-186,101.5], [-186,106.5], [-185,107.5], [-185,112.5], [-184,113.5], [-184,116.5],
  [-183,117.5], [-183,120.5], [-182,121.5], [-182,124.5], [-181,125.5], [-181,128.5],
  [-180,129.5], [-180,131.5], [-178,134.5], [-178,136.5], [-176,139.5], [-176,141.5],
  [-175,142.5], [-175,143.5], [-174,144.5], [-174,145.5], [-173,146.5], [-170,153.5],
  [-168,155.5], [-167,158.5], [-165,160.5], [-165,161.5], [-162,164.5], [-162,165.5],
  [-158,169.5], [-158,170.5], [-157,171.5], [-159.5,173], [-165.5,173], [-166.5,174],
  [-175.5,174], [-176.5,175], [-180.5,175], [-184,178.5], [-184,182.5], [-185,183.5],
  [-185,192.5], [-186,193.5], [-186,199.5], [-185,200.5], [-185,201.5], [-182.5,204],
  [-174.5,204], [-173,205.5], [-173,209.5], [-173.5,210], [-175.5,210], [-176.5,211],
  [-180.5,211], [-181.5,212], [-182.5,212], [-185,214.5], [-185,216.5], [-186,217.5],
  [-186,219.5], [-185,220.5], [-185,232.5], [-182.5,236], [-143.5,225], [-145,223.5],
  [-145,220.5], [-144,219.5], [-144,217.5], [-143,216.5], [-143,215.5], [-140.5,214],
  [-135.5,214], [-131,211.5], [-131,210.5], [-127,203.5], [-127,201.5], [-124,196.5],
  [-124,194.5], [-123,193.5], [-123,192.5], [-119.5,189], [-117.5,189], [-116.5,188],
  [-110.5,188], [-109.5,187], [-103.5,187], [-102,188.5], [-102,191.5], [-103,192.5],
  [-103,194.5], [-104,195.5], [-105,200.5], [-107,203.5], [-108,208.5], [-110,211.5],
  [-111,216.5], [-113,219.5], [-113,221.5], [-115.5,224], [-116.5,224], [-117.5,225],
  [-142.5,225], [116.5,225], [113,222.5], [113,221.5], [112,220.5], [112,218.5],
  [110,215.5], [110,213.5], [109,212.5], [108,207.5], [106,204.5], [106,202.5],
  [105,201.5], [104,196.5], [103,195.5], [103,194.5], [102,193.5], [102,191.5],
  [101,190.5], [101,188.5], [102.5,187], [108.5,187], [109.5,188], [116.5,188],
  [117.5,189], [119.5,189], [122,191.5], [122,192.5], [124,195.5], [124,197.5],
  [126,200.5], [126,202.5], [127,203.5], [130,210.5], [132.5,213], [134.5,213],
  [135.5,214], [139.5,214], [140.5,215], [141.5,215], [144,218.5], [144,221.5],
  [145,222.5], [143.5,225], [117.5,225], [-132.5,168], [-136.5,165], [-137.5,165],
  [-150,152.5], [-150,151.5], [-155,145.5], [-155,144.5], [-160,135.5], [-160,133.5],
  [-161,132.5], [-161,131.5], [-162,130.5], [-162,128.5], [-163,127.5], [-163,124.5],
  [-164,123.5], [-164,120.5], [-165,119.5], [-165,115.5], [-166,114.5], [-166,105.5],
  [-167,104.5], [-167,96.5], [-166,95.5], [-166,86.5], [-165,85.5], [-165,81.5],
  [-164,80.5], [-164,78.5], [-163,77.5], [-163,74.5], [-162,73.5], [-162,71.5],
  [-160,68.5], [-160,66.5], [-159,65.5], [-159,64.5], [-158,63.5], [-158,62.5],
  [-157,61.5], [-154,54.5], [-152,52.5], [-152,51.5], [-150,49.5], [-150,48.5],
  [-147,45.5], [-147,44.5], [-137.5,35], [-136.5,35], [-133.5,32], [-132.5,32],
  [-130.5,30], [-127.5,29], [-125.5,27], [-123.5,27], [-122.5,26], [-121.5,26],
  [-120.5,25], [-118.5,25], [-117.5,24], [-114.5,24], [-113.5,23], [-108.5,23],
  [-107.5,22], [-97.5,22], [-96.5,23], [-89.5,23], [-88.5,24], [-84.5,24],
  [-83.5,25], [-80.5,25], [-79.5,26], [-76.5,26], [-75.5,27], [-72.5,27],
  [-71.5,28], [-67.5,28], [-66.5,29], [-61.5,29], [-60.5,28], [-55.5,27],
  [-51.5,24], [-50.5,24], [-49.5,23], [-48.5,23], [-47.5,22], [-46.5,22],
  [-45.5,21], [-44.5,21], [-43.5,20], [-42.5,20], [-41.5,19], [-40.5,19],
  [-39.5,18], [-38.5,18], [-37.5,17], [-36.5,17], [-35.5,16], [-34.5,16],
  [-33.5,15], [-32.5,15], [-31.5,14], [-30.5,14], [-29.5,13], [-28.5,13],
  [-27.5,12], [-26.5,12], [-25.5,11], [-24.5,11], [-23.5,10], [-22.5,10],
  [-21.5,9], [-20.5,9], [-19.5,8], [-18.5,8], [-17.5,7], [-16.5,7],
  [-15.5,6], [-14.5,6], [-13.5,5], [-12.5,5], [-11.5,4], [-4.5,1],
  [-3.5,0], [3.5,0], [4.5,1], [5.5,1], [6.5,2], [13.5,5],
  [15.5,7], [16.5,7], [17.5,8], [18.5,8], [19.5,9], [20.5,9],
  [21.5,10], [22.5,10], [23.5,11], [24.5,11], [25.5,12], [26.5,12],
  [27.5,13], [28.5,13], [29.5,14], [30.5,14], [31.5,15], [32.5,15],
  [33.5,16], [34.5,16], [35.5,17], [36.5,17], [37.5,18], [38.5,18],
  [39.5,19], [40.5,19], [41.5,20], [42.5,20], [43.5,21], [44.5,21],
  [45.5,22], [46.5,22], [47.5,23], [48.5,23], [49.5,24], [56.5,27],
  [57.5,28], [59.5,28], [60.5,29], [66.5,29], [67.5,28], [71.5,28],
  [72.5,27], [75.5,27], [76.5,26], [78.5,26], [79.5,25], [82.5,25],
  [83.5,24], [87.5,24], [88.5,23], [95.5,23], [96.5,22], [106.5,22],
  [107.5,23], [113.5,23], [114.5,24], [116.5,24], [117.5,25], [122.5,26],
  [123.5,27], [128.5,29], [133.5,33], [134.5,33], [140.5,39], [141.5,39],
  [142,40.5], [148,46.5], [148,47.5], [154,55.5], [154,56.5], [159,65.5],
  [159,67.5], [161,70.5], [161,72.5], [162,73.5], [162,75.5], [163,76.5],
  [163,79.5], [164,80.5], [164,83.5], [165,84.5], [165,89.5], [166,90.5],
  [166,109.5], [165,110.5], [165,115.5], [164,116.5], [164,120.5], [163,121.5],
  [163,124.5], [162,125.5], [162,128.5], [161,129.5], [161,131.5], [159,134.5],
  [159,136.5], [158,137.5], [156,142.5], [154,144.5], [153,147.5], [151,149.5],
  [151,150.5], [147,154.5], [147,155.5], [139.5,163], [138.5,163], [132.5,168],
  [127.5,168], [126.5,167], [124.5,167], [123.5,166], [118.5,164], [110.5,158],
  [107.5,157], [99.5,151], [96.5,150], [94.5,148], [93.5,148], [91.5,146],
  [90.5,146], [88.5,144], [87,143.5], [87,142.5], [88.5,141], [96.5,141],
  [97.5,140], [101.5,140], [102.5,139], [104.5,139], [105.5,138], [110.5,137],
  [111.5,136], [114.5,135], [116.5,133], [117.5,133], [120,130.5], [120,127.5],
  [118.5,126], [109.5,126], [109,125.5], [112,120.5], [112,118.5], [113,117.5],
  [113,115.5], [114,114.5], [114,110.5], [115,109.5], [115,100.5], [114,99.5],
  [114,95.5], [113,94.5], [113,91.5], [112,90.5], [112,88.5], [111,87.5],
  [110,84.5], [105,78.5], [105,77.5], [102.5,75], [101.5,75], [97.5,71],
  [96.5,71], [94.5,69], [93.5,69], [90.5,67], [85.5,66], [84.5,65],
  [81.5,65], [80.5,64], [67.5,64], [66.5,65], [62.5,65], [61.5,66],
  [59.5,66], [48.5,72], [48,70.5], [51,67.5], [51,66.5], [62.5,55],
  [64,54.5], [64,53.5], [65,52.5], [62.5,50], [61.5,51], [59.5,51],
  [55.5,54], [52.5,55], [49.5,58], [48.5,58], [39,67.5], [39,68.5],
  [35,73.5], [35,74.5], [31,81.5], [31,83.5], [30,84.5], [30,86.5],
  [29,87.5], [29,90.5], [28,91.5], [28,94.5], [27,95.5], [27,101.5],
  [26.5,102], [19.5,102], [18.5,103], [17.5,103], [16,105.5], [21.5,111],
  [22.5,111], [24.5,113], [25.5,113], [28.5,116], [29.5,116], [37.5,122],
  [38.5,122], [41.5,125], [42.5,125], [44.5,127], [45.5,127], [48.5,130],
  [49.5,130], [52.5,133], [53.5,133], [55.5,135], [56.5,135], [60.5,139],
  [61.5,139], [74,151.5], [74,152.5], [81,159.5], [81,160.5], [84,163.5],
  [84,166.5], [82.5,168], [81.5,168], [76.5,165], [74.5,165], [73.5,164],
  [68.5,162], [66.5,160], [61.5,158], [59.5,156], [58.5,156], [56.5,154],
  [55.5,154], [53.5,152], [52.5,152], [50.5,150], [49.5,150], [47.5,148],
  [46.5,148], [43.5,145], [42.5,145], [38.5,141], [37.5,141], [33.5,137],
  [32.5,137], [28.5,133], [27.5,133], [23.5,129], [22.5,129], [15.5,123],
  [14.5,123], [9.5,119], [8.5,119], [5.5,117], [3.5,117], [2.5,116],
  [-2.5,116], [-3.5,117], [-6.5,117], [-7.5,118], [-8.5,118], [-10.5,120],
  [-11.5,120], [-13.5,122], [-14.5,122], [-16.5,124], [-17.5,124], [-19.5,126],
  [-20.5,126], [-24.5,130], [-25.5,130], [-29.5,134], [-30.5,134], [-34.5,138],
  [-35.5,138], [-40.5,143], [-41.5,143], [-48.5,149], [-49.5,149], [-51.5,151],
  [-52.5,151], [-60.5,157], [-63.5,158], [-65.5,160], [-66.5,160], [-77.5,166],
  [-79.5,166], [-80.5,167], [-83.5,168], [-85,166.5], [-85,164.5], [-84,163.5],
  [-84,162.5], [-80,158.5], [-80,157.5], [-72,149.5], [-72,148.5], [-64.5,141],
  [-63.5,141], [-59.5,137], [-58.5,137], [-55.5,134], [-54.5,134], [-47.5,128],
  [-46.5,128], [-44.5,126], [-43.5,126], [-40.5,123], [-39.5,123], [-37.5,121],
  [-36.5,121], [-34.5,119], [-33.5,119], [-30.5,116], [-29.5,116], [-27.5,114],
  [-26.5,114], [-24.5,112], [-23.5,112], [-21.5,110], [-20.5,110], [-17,106.5],
  [-17,104.5], [-19.5,102], [-24.5,102], [-25.5,103], [-27,101.5], [-27,100.5],
  [-28,99.5], [-28,93.5], [-29,92.5], [-29,89.5], [-30,88.5], [-30,86.5],
  [-31,85.5], [-32,80.5], [-33,79.5], [-35,74.5], [-39,69.5], [-39,68.5],
  [-50.5,57], [-51.5,57], [-56.5,53], [-57.5,53], [-60.5,51], [-62.5,51],
  [-63.5,50], [-65,51.5], [-65,53.5], [-53,64.5], [-53,65.5], [-48,71.5],
  [-48.5,72], [-49.5,72], [-51.5,70], [-52.5,70], [-57.5,67], [-59.5,67],
  [-60.5,66], [-62.5,66], [-63.5,65], [-66.5,65], [-67.5,64], [-81.5,64],
  [-82.5,65], [-85.5,65], [-86.5,66], [-88.5,66], [-89.5,67], [-96.5,70],
  [-98.5,72], [-99.5,72], [-107,79.5], [-107,80.5], [-110,83.5], [-110,84.5],
  [-113,89.5], [-113,91.5], [-114,92.5], [-114,95.5], [-115,96.5], [-115,112.5],
  [-114,113.5], [-114,116.5], [-113,117.5], [-113,119.5], [-112,120.5], [-110,125.5],
  [-110.5,126], [-119.5,126], [-121,128.5], [-120,131.5], [-118.5,132], [-116.5,134],
  [-115.5,134], [-113.5,136], [-111.5,136], [-108.5,138], [-106.5,138], [-105.5,139],
  [-102.5,139], [-101.5,140], [-98.5,140], [-97.5,141], [-89.5,141], [-87,142.5],
  [-89.5,145], [-90.5,145], [-95.5,149], [-98.5,150], [-100.5,152], [-101.5,152],
  [-106.5,156], [-109.5,157], [-111.5,159], [-112.5,159], [-117.5,163], [-120.5,164],
  [-122.5,166], [-123.5,166], [-124.5,167], [-126.5,167], [-127.5,168], [-131.5,168],
  [-128,156.5], [-126.5,157], [-125.5,156], [-122.5,155], [-120.5,153], [-119,152.5],
  [-119,150.5], [-129,140.5], [-129,139.5], [-132,136.5], [-132,135.5], [-137,126.5],
  [-137,124.5], [-138,123.5], [-138,121.5], [-139,120.5], [-139,117.5], [-140,116.5],
  [-140,112.5], [-141,111.5], [-141,91.5], [-140,90.5], [-140,86.5], [-139,85.5],
  [-139,82.5], [-138,81.5], [-137,76.5], [-135,73.5], [-135,71.5], [-132,67.5],
  [-131,64.5], [-128,61.5], [-128,60.5], [-117.5,50], [-116.5,50], [-111.5,46],
  [-110.5,46], [-103.5,42], [-101.5,42], [-100.5,41], [-98.5,41], [-97.5,40],
  [-94.5,40], [-93.5,39], [-91.5,39], [-91,36.5], [-92.5,35], [-97.5,35],
  [-98.5,34], [-106.5,34], [-107.5,35], [-112.5,35], [-113.5,36], [-115.5,36],
  [-118.5,38], [-120.5,38], [-122.5,40], [-125.5,41], [-128.5,44], [-129.5,44],
  [-139,53.5], [-139,54.5], [-145,62.5], [-145,63.5], [-149,70.5], [-149,72.5],
  [-150,73.5], [-150,75.5], [-151,76.5], [-151,78.5], [-152,79.5], [-152,81.5],
  [-153,82.5], [-153,86.5], [-154,87.5], [-154,92.5], [-155,93.5], [-155,107.5],
  [-154,108.5], [-154,114.5], [-153,115.5], [-153,119.5], [-152,120.5], [-152,122.5],
  [-151,123.5], [-151,126.5], [-149,129.5], [-149,131.5], [-148,132.5], [-146,137.5],
  [-144,139.5], [-144,140.5], [-142,142.5], [-142,143.5], [-140,145.5], [-140,146.5],
  [-133.5,153], [-132.5,153], [-129.5,156], [-128.5,156], [126,156.5], [127.5,157],
  [129.5,155], [130.5,155], [140,145.5], [140,144.5], [144,139.5], [144,138.5],
  [150,127.5], [150,125.5], [151,124.5], [151,121.5], [152,120.5], [152,117.5],
  [153,116.5], [153,111.5], [154,110.5], [154,90.5], [153,89.5], [153,84.5],
  [152,83.5], [152,80.5], [151,79.5], [150,74.5], [147,69.5], [147,67.5],
  [144,63.5], [143,60.5], [141,58.5], [141,57.5], [139,55.5], [139,54.5],
  [126.5,42], [125.5,42], [123.5,40], [122.5,40], [117.5,37], [115.5,37],
  [114.5,36], [112.5,36], [111.5,35], [107.5,35], [106.5,34], [97.5,34],
  [96.5,35], [91.5,35], [90,36.5], [90.5,39], [92.5,39], [93.5,40],
  [96.5,40], [97.5,41], [102.5,42], [105.5,44], [107.5,44], [109.5,46],
  [110.5,46], [114.5,49], [115.5,49], [125,57.5], [125,58.5], [129,62.5],
  [129,63.5], [131,65.5], [131,66.5], [136,75.5], [136,77.5], [137,78.5],
  [137,80.5], [138,81.5], [138,83.5], [139,84.5], [139,87.5], [140,88.5],
  [140,95.5], [141,96.5], [141,105.5], [140,106.5], [140,113.5], [139,114.5],
  [139,118.5], [138,119.5], [138,121.5], [137,122.5], [136,127.5], [135,128.5],
  [134,131.5], [132,133.5], [131,136.5], [129,138.5], [129,139.5], [125,143.5],
  [125,144.5], [123.5,146], [122.5,146], [118,150.5], [118,151.5], [120.5,154],
  [121.5,154], [123.5,156], [125.5,156], [-95.5,127], [-100,122.5], [-100,121.5],
  [-102,119.5], [-102,117.5], [-104,114.5], [-104,111.5], [-105,110.5], [-105,99.5],
  [-104,98.5], [-104,95.5], [-103,94.5], [-103,92.5], [-102,91.5], [-101,88.5],
  [-99,86.5], [-99,85.5], [-92.5,79], [-91.5,79], [-89.5,77], [-88.5,77],
  [-85.5,75], [-83.5,75], [-82.5,74], [-79.5,74], [-78.5,73], [-68.5,73],
  [-67.5,74], [-64.5,74], [-63.5,75], [-61.5,75], [-60.5,76], [-57.5,77],
  [-55.5,79], [-54.5,79], [-47,86.5], [-46,89.5], [-44,91.5], [-44,93.5],
  [-43,94.5], [-43,96.5], [-42,97.5], [-42,106.5], [-43,107.5], [-43,108.5],
  [-46.5,112], [-47.5,112], [-51.5,115], [-52,112.5], [-51,111.5], [-51,107.5],
  [-50,106.5], [-50,104.5], [-51,103.5], [-51,99.5], [-52,98.5], [-52,96.5],
  [-55,92.5], [-55,91.5], [-58.5,88], [-59.5,88], [-61.5,86], [-62.5,86],
  [-65.5,84], [-69.5,84], [-70.5,83], [-74.5,83], [-75.5,84], [-78.5,84],
  [-79.5,85], [-81.5,85], [-82.5,86], [-83.5,86], [-86.5,89], [-87.5,89],
  [-93,96.5], [-93,98.5], [-94,99.5], [-94,111.5], [-93,112.5], [-91,117.5],
  [-83.5,125], [-82.5,125], [-81,126.5], [-81.5,127], [-94.5,127], [80.5,127],
  [80,126.5], [81.5,125], [82.5,125], [84.5,123], [85.5,123], [89,119.5],
  [89,118.5], [92,114.5], [92,112.5], [93,111.5], [93,108.5], [94,107.5],
  [94,102.5], [93,101.5], [93,98.5], [92,97.5], [90,92.5], [84.5,87],
  [83.5,87], [78.5,84], [74.5,84], [73.5,83], [69.5,83], [68.5,84],
  [65.5,84], [64.5,85], [62.5,85], [61.5,86], [60.5,86], [53,93.5],
  [53,94.5], [51,97.5], [51,99.5], [50,100.5], [50,110.5], [51,111.5],
  [51,113.5], [52,114.5], [51.5,115], [50.5,115], [48.5,113], [47.5,113],
  [45.5,111], [44.5,111], [43,109.5], [43,108.5], [41,105.5], [41,98.5],
  [42,97.5], [42,95.5], [43,94.5], [43,92.5], [44,91.5], [45,88.5],
  [52.5,80], [53.5,80], [58.5,76], [60.5,76], [63.5,74], [66.5,74],
  [67.5,73], [77.5,73], [78.5,74], [81.5,74], [82.5,75], [84.5,75],
  [85.5,76], [88.5,77], [90.5,79], [91.5,79], [99,86.5], [99,87.5],
  [102,91.5], [102,93.5], [103,94.5], [103,96.5], [104,97.5], [104,111.5],
  [103,112.5], [103,115.5], [102,116.5], [100,121.5], [98,123.5], [98,124.5],
  [94.5,127], [81.5,127], [-83.5,120], [-87,117.5], [-87,115.5], [-88,114.5],
  [-88,113.5], [-87,112.5], [-87,110.5], [-84.5,108], [-83.5,108], [-82.5,107],
  [-79.5,107], [-78.5,108], [-77.5,108], [-75,110.5], [-75,112.5], [-74,113.5],
  [-75,114.5], [-75,116.5], [-76.5,119], [-77.5,119], [-78.5,120], [-82.5,120],
  [60.5,120], [57,116.5], [57,110.5], [59.5,108], [60.5,108], [61.5,107],
  [64.5,107], [65.5,108], [66.5,108], [69,110.5], [69,112.5], [70,113.5],
  [70,114.5], [67.5,119], [66.5,119], [65.5,120], [61.5,120], [-3,98.5],
  [-2.5,99], [2.5,99], [4.5,97], [5.5,97], [9,93.5], [9,92.5],
  [12,88.5], [12,86.5], [14,83.5], [14,81.5], [16,78.5], [17,73.5],
  [19,70.5], [19,68.5], [20,67.5], [20,65.5], [21,64.5], [21,62.5],
  [22,61.5], [22,55.5], [21,54.5], [21,52.5], [18,47.5], [18,45.5],
  [14,38.5], [14,36.5], [9,27.5], [9,25.5], [8,24.5], [5,17.5],
  [1.5,15], [-2.5,15], [-3.5,16], [-4.5,16], [-5,17.5], [-7,19.5],
  [-7,21.5], [-12,30.5], [-12,32.5], [-16,39.5], [-16,41.5], [-17,42.5],
  [-17,43.5], [-20,48.5], [-20,50.5], [-23,55.5], [-23,60.5], [-22,61.5],
  [-22,63.5], [-21,64.5], [-21,66.5], [-20,67.5], [-19,72.5], [-18,73.5],
  [-18,74.5], [-17,75.5], [-17,77.5], [-15,80.5], [-15,82.5], [-12,87.5],
  [-12,89.5], [-11,90.5], [-11,91.5], [-8,94.5], [-8,95.5], [-6.5,96],
  [-4.5,98], [-3.5,98], [-112,-10.5], [-111.5,-10], [-107.5,-10], [-106.5,-11],
  [-99.5,-14], [-97.5,-16], [-96.5,-16], [-95.5,-17], [-94.5,-17], [-93.5,-18],
  [-92.5,-18], [-91.5,-19], [-84.5,-22], [-82.5,-24], [-81.5,-24], [-80.5,-25],
  [-79.5,-25], [-78.5,-26], [-77.5,-26], [-76.5,-27], [-75.5,-27], [-74.5,-28],
  [-73.5,-28], [-72.5,-29], [-71.5,-29], [-70.5,-30], [-63.5,-33], [-61.5,-35],
  [-60.5,-35], [-59.5,-36], [-58.5,-36], [-57.5,-37], [-56.5,-37], [-55.5,-38],
  [-54.5,-38], [-53.5,-39], [-52.5,-39], [-51.5,-40], [-44.5,-43], [-42.5,-45],
  [-41.5,-45], [-40.5,-46], [-39.5,-46], [-38.5,-47], [-37.5,-47], [-36.5,-48],
  [-35.5,-48], [-34.5,-49], [-33.5,-49], [-32.5,-50], [-25.5,-53], [-23.5,-55],
  [-16.5,-58], [-14,-60.5], [-14,-62.5], [-36.5,-85], [-38.5,-85], [-39.5,-84],
  [-40.5,-84], [-41.5,-83], [-48.5,-80], [-50.5,-78], [-51.5,-78], [-52.5,-77],
  [-59.5,-74], [-61.5,-72], [-62.5,-72], [-63.5,-71], [-64.5,-71], [-65.5,-70],
  [-72.5,-67], [-74.5,-65], [-75.5,-65], [-76.5,-64], [-83.5,-61], [-85.5,-59],
  [-86.5,-59], [-87.5,-58], [-94.5,-55], [-96.5,-53], [-103.5,-50], [-105.5,-48],
  [-112.5,-45], [-114.5,-43], [-121.5,-40], [-123.5,-38], [-128.5,-36], [-130,-34.5],
  [-130,-31.5], [-128,-29.5], [-128,-28.5], [-126,-26.5], [-126,-25.5], [-123,-22.5],
  [-123,-21.5], [-112.5,-11], [106,-10.5], [106.5,-10], [110.5,-10], [111.5,-11],
  [112.5,-11], [119,-17.5], [119,-19.5], [117.5,-21], [115.5,-21], [114.5,-22],
  [113.5,-22], [110.5,-25], [109.5,-25], [91,-44.5], [91,-45.5], [89,-48.5],
  [89,-56.5], [86.5,-59], [85.5,-59], [84.5,-60], [77.5,-63], [75.5,-65],
  [74.5,-65], [73.5,-66], [72.5,-66], [71.5,-67], [64.5,-70], [62.5,-72],
  [61.5,-72], [60.5,-73], [53.5,-76], [51.5,-78], [50.5,-78], [49.5,-79],
  [48.5,-79], [47.5,-80], [40.5,-83], [38.5,-85], [34.5,-85], [13,-63.5],
  [13,-60.5], [15.5,-58], [16.5,-58], [17.5,-57], [18.5,-57], [19.5,-56],
  [20.5,-56], [21.5,-55], [28.5,-52], [30.5,-50], [31.5,-50], [32.5,-49],
  [33.5,-49], [34.5,-48], [35.5,-48], [36.5,-47], [37.5,-47], [38.5,-46],
  [39.5,-46], [40.5,-45], [41.5,-45], [42.5,-44], [49.5,-41], [51.5,-39],
  [52.5,-39], [53.5,-38], [54.5,-38], [55.5,-37], [56.5,-37], [57.5,-36],
  [58.5,-36], [59.5,-35], [60.5,-35], [61.5,-34], [68.5,-31], [70.5,-29],
  [71.5,-29], [72.5,-28], [73.5,-28], [74.5,-27], [75.5,-27], [76.5,-26],
  [77.5,-26], [78.5,-25], [85.5,-22], [87.5,-20], [90.5,-19], [91.5,-18],
  [92.5,-18], [93.5,-17], [94.5,-17], [95.5,-16], [102.5,-13], [104.5,-11],
  [105.5,-11], [120,-36.5], [120.5,-36], [123.5,-36], [148,-60.5], [148,-63.5],
  [143,-68.5], [147.5,-73], [148.5,-73], [161.5,-60], [166.5,-60], [167.5,-61],
  [168.5,-61], [181,-73.5], [181,-74.5], [182,-75.5], [182,-78.5], [181,-79.5],
  [181,-80.5], [168,-93.5], [172.5,-98], [173.5,-98], [177.5,-94], [178.5,-94],
  [179.5,-93], [180.5,-94], [181.5,-94], [205,-117.5], [205,-118.5], [206,-119.5],
  [205,-120.5], [205,-121.5], [188.5,-138], [185.5,-137], [162,-113.5], [162,-109.5],
  [167,-104.5], [162.5,-100], [151.5,-111], [150.5,-111], [149.5,-112], [146.5,-112],
  [145.5,-111], [144.5,-111], [140.5,-107], [138,-109.5], [138,-111.5], [139,-112.5],
  [139,-113.5], [140,-114.5], [140,-116.5], [141,-117.5], [141,-121.5], [142,-122.5],
  [142,-127.5], [141,-128.5], [141,-131.5], [140,-132.5], [139,-135.5], [137.5,-137],
  [134.5,-137], [123.5,-126], [121.5,-126], [118,-129.5], [118,-132.5], [114.5,-136],
  [109.5,-136], [107,-133.5], [107,-131.5], [106,-130.5], [107,-129.5], [107,-127.5],
  [109.5,-125], [112.5,-125], [117,-120.5], [117,-119.5], [106,-108.5], [106,-107.5],
  [105,-106.5], [106,-105.5], [106,-104.5], [110.5,-102], [111.5,-102], [112.5,-101],
  [123.5,-101], [124.5,-102], [126.5,-102], [131.5,-105], [135,-101.5], [131,-97.5],
  [131,-96.5], [130,-95.5], [130,-92.5], [131,-91.5], [131,-90.5], [142,-79.5],
  [137.5,-75], [136.5,-75], [131.5,-80], [129.5,-80], [104,-54.5], [104,-52.5],
  [119.5,-37], [-161,-50.5], [-160.5,-50], [-146.5,-50], [-145.5,-51], [-139.5,-51],
  [-138.5,-52], [-134.5,-52], [-133.5,-53], [-130.5,-53], [-129.5,-54], [-127.5,-54],
  [-124.5,-56], [-122.5,-56], [-121.5,-57], [-120.5,-57], [-119.5,-58], [-112.5,-61],
  [-110.5,-63], [-109.5,-63], [-107.5,-65], [-106.5,-65], [-103.5,-68], [-102.5,-68],
  [-96,-74.5], [-96,-77.5], [-102.5,-84], [-103.5,-84], [-104.5,-83], [-105.5,-83],
  [-110.5,-78], [-111.5,-78], [-114.5,-75], [-115.5,-75], [-120.5,-71], [-121.5,-71],
  [-124.5,-69], [-126.5,-69], [-129.5,-67], [-134.5,-66], [-135.5,-65], [-138.5,-65],
  [-139.5,-64], [-144.5,-64], [-145.5,-63], [-162.5,-63], [-163.5,-64], [-167.5,-64],
  [-168.5,-65], [-171.5,-65], [-172.5,-66], [-177.5,-67], [-178.5,-68], [-179.5,-68],
  [-180.5,-69], [-182.5,-69], [-183.5,-70], [-186.5,-71], [-188.5,-73], [-191.5,-74],
  [-193.5,-76], [-194.5,-76], [-201.5,-83], [-202.5,-83], [-203.5,-84], [-204.5,-83],
  [-205.5,-83], [-212,-76.5], [-211,-73.5], [-208.5,-71], [-207.5,-71], [-199.5,-64],
  [-198.5,-64], [-196.5,-62], [-191.5,-60], [-189.5,-58], [-187.5,-58], [-182.5,-55],
  [-180.5,-55], [-179.5,-54], [-177.5,-54], [-176.5,-53], [-173.5,-53], [-172.5,-52],
  [-168.5,-52], [-167.5,-51], [-161.5,-51], [-161,-74.5], [-160.5,-74], [-146.5,-74],
  [-145.5,-75], [-140.5,-75], [-139.5,-76], [-137.5,-76], [-136.5,-77], [-131.5,-78],
  [-130.5,-79], [-123.5,-82], [-121.5,-84], [-120.5,-84], [-116.5,-88], [-115.5,-88],
  [-113,-90.5], [-112,-93.5], [-118.5,-100], [-119.5,-100], [-120.5,-101], [-122.5,-99],
  [-123.5,-99], [-128.5,-94], [-131.5,-93], [-133.5,-91], [-134.5,-91], [-135.5,-90],
  [-137.5,-90], [-138.5,-89], [-140.5,-89], [-141.5,-88], [-144.5,-88], [-145.5,-87],
  [-161.5,-87], [-162.5,-88], [-165.5,-88], [-166.5,-89], [-168.5,-89], [-169.5,-90],
  [-171.5,-90], [-172.5,-91], [-175.5,-92], [-185.5,-100], [-188.5,-100], [-195,-93.5],
  [-195,-91.5], [-189.5,-86], [-188.5,-86], [-185.5,-83], [-182.5,-82], [-180.5,-80],
  [-178.5,-80], [-177.5,-79], [-176.5,-79], [-173.5,-77], [-171.5,-77], [-170.5,-76],
  [-167.5,-76], [-166.5,-75], [-161.5,-75], [-5,-74.5], [-4.5,-74], [0.5,-74],
  [3.5,-77], [4.5,-77], [29,-101.5], [29,-104.5], [8,-125.5], [29,-146.5],
  [29,-149.5], [0.5,-177], [-4.5,-177], [-6,-175.5], [-6,-139.5], [-6.5,-139],
  [-20.5,-153], [-21.5,-153], [-22.5,-154], [-25.5,-154], [-29,-150.5], [-29,-149.5],
  [-30,-148.5], [-30,-146.5], [-9,-125.5], [-9,-124.5], [-12.5,-121], [-13.5,-121],
  [-30,-104.5], [-30,-101.5], [-24.5,-96], [-22.5,-96], [-7.5,-111], [-6,-110.5],
  [-6,-75.5], [-5.5,-75], [-229,-85.5], [-228.5,-85], [-225.5,-85], [-215.5,-95],
  [-214.5,-95], [-212,-97.5], [-212,-98.5], [-210,-100.5], [-210,-102.5], [-208,-105.5],
  [-208,-107.5], [-207,-108.5], [-207,-109.5], [-205,-111.5], [-204,-114.5], [-201,-117.5],
  [-201,-118.5], [-193.5,-126], [-192.5,-126], [-186.5,-131], [-181.5,-133], [-179,-135.5],
  [-179,-138.5], [-178,-139.5], [-178,-141.5], [-177,-142.5], [-175,-147.5], [-168.5,-154],
  [-167.5,-154], [-165.5,-156], [-164.5,-156], [-162,-158.5], [-162,-160.5], [-161,-161.5],
  [-161,-172.5], [-160,-173.5], [-160,-185.5], [-159,-186.5], [-159,-196.5], [-158,-197.5],
  [-158,-204.5], [-157,-205.5], [-157,-211.5], [-156,-212.5], [-156,-217.5], [-155,-218.5],
  [-155,-223.5], [-154,-224.5], [-154,-228.5], [-153,-229.5], [-153,-235.5], [-153.5,-236],
  [-174.5,-236], [-177,-234.5], [-177,-233.5], [-178,-232.5], [-178,-230.5], [-179,-229.5],
  [-179,-227.5], [-180,-226.5], [-180,-223.5], [-181,-222.5], [-181,-219.5], [-182,-218.5],
  [-182,-215.5], [-183,-214.5], [-183,-210.5], [-184,-209.5], [-184,-205.5], [-185,-204.5],
  [-185,-199.5], [-186,-198.5], [-186,-191.5], [-187,-190.5], [-187,-183.5], [-188,-182.5],
  [-188,-173.5], [-189,-172.5], [-189,-169.5], [-191.5,-167], [-192.5,-167], [-194.5,-165],
  [-195.5,-165], [-198.5,-163], [-200,-163.5], [-200,-175.5], [-199,-176.5], [-199,-187.5],
  [-198,-188.5], [-198,-195.5], [-197,-196.5], [-197,-201.5], [-196,-202.5], [-196,-207.5],
  [-195,-208.5], [-195,-212.5], [-194,-213.5], [-194,-217.5], [-193,-218.5], [-193,-221.5],
  [-192,-222.5], [-192,-226.5], [-191,-227.5], [-191,-230.5], [-190,-231.5], [-190,-235.5],
  [-190.5,-236], [-207.5,-236], [-210,-233.5], [-210,-232.5], [-212,-229.5], [-212,-227.5],
  [-213,-226.5], [-213,-224.5], [-214,-223.5], [-214,-221.5], [-215,-220.5], [-215,-217.5],
  [-216,-216.5], [-216,-214.5], [-217,-213.5], [-217,-211.5], [-218,-210.5], [-218,-207.5],
  [-219,-206.5], [-219,-204.5], [-220,-203.5], [-220,-200.5], [-221,-199.5], [-221,-196.5],
  [-222,-195.5], [-222,-192.5], [-223,-191.5], [-223,-187.5], [-224,-186.5], [-224,-183.5],
  [-225,-182.5], [-225,-178.5], [-226,-177.5], [-226,-172.5], [-227,-171.5], [-227,-166.5],
  [-228,-165.5], [-228,-160.5], [-229,-159.5], [-229,-152.5], [-230,-151.5], [-230,-143.5],
  [-231,-142.5], [-231,-97.5], [-230,-96.5], [-230,-87.5], [-229,-86.5], [93,-89.5],
  [93.5,-89], [96.5,-89], [97,-89.5], [97,-92.5], [93,-97.5], [93,-98.5],
  [91,-101.5], [91,-103.5], [90,-104.5], [90,-112.5], [89,-113.5], [89,-114.5],
  [90,-115.5], [90,-122.5], [91,-123.5], [91,-129.5], [88.5,-132], [87.5,-132],
  [83.5,-135], [78.5,-137], [76.5,-139], [69.5,-142], [67.5,-144], [62.5,-146],
  [60.5,-148], [59.5,-148], [52.5,-152], [49.5,-152], [48,-149.5], [47,-144.5],
  [46,-143.5], [45,-140.5], [43,-138.5], [43,-137.5], [40,-134.5], [40,-133.5],
  [31,-124.5], [36.5,-120], [41.5,-118], [43.5,-116], [48.5,-114], [50.5,-112],
  [51.5,-112], [52.5,-111], [59.5,-108], [61.5,-106], [62.5,-106], [63.5,-105],
  [68.5,-103], [70.5,-101], [71.5,-101], [72.5,-100], [79.5,-97], [81.5,-95],
  [82.5,-95], [83.5,-94], [84.5,-94], [85.5,-93], [92.5,-90], [-91,-93.5],
  [-90.5,-93], [-86.5,-93], [-85.5,-94], [-78.5,-97], [-76.5,-99], [-69.5,-102],
  [-67.5,-104], [-60.5,-107], [-58.5,-109], [-51.5,-112], [-49.5,-114], [-42.5,-117],
  [-40.5,-119], [-33.5,-122], [-32,-123.5], [-33,-126.5], [-43,-136.5], [-43,-137.5],
  [-47,-142.5], [-47,-144.5], [-49,-147.5], [-49,-150.5], [-50.5,-152], [-53.5,-152],
  [-54.5,-151], [-57.5,-150], [-59.5,-148], [-66.5,-145], [-68.5,-143], [-75.5,-140],
  [-77.5,-138], [-82.5,-136], [-84.5,-134], [-89.5,-132], [-91.5,-130], [-92.5,-130],
  [-93.5,-129], [-96.5,-128], [-98.5,-126], [-101.5,-125], [-105,-121.5], [-104,-118.5],
  [-101,-115.5], [-100,-112.5], [-97,-108.5], [-96,-103.5], [-94,-100.5], [-94,-98.5],
  [-91.5,-94], [5.5,-94], [5,-94.5], [5,-110.5], [5.5,-111], [6.5,-111],
  [14,-103.5], [14,-101.5], [12.5,-100], [11.5,-100], [6,-94.5], [-161,-98.5],
  [-160.5,-98], [-147.5,-98], [-146.5,-99], [-143.5,-99], [-142.5,-100], [-140.5,-100],
  [-139.5,-101], [-136.5,-102], [-134.5,-104], [-133.5,-104], [-129,-108.5], [-129,-110.5],
  [-135.5,-117], [-138.5,-117], [-141.5,-114], [-142.5,-114], [-144.5,-112], [-146.5,-112],
  [-147.5,-111], [-159.5,-111], [-160.5,-112], [-162.5,-112], [-168.5,-117], [-171.5,-117],
  [-178,-110.5], [-178,-108.5], [-172.5,-103], [-171.5,-103], [-166.5,-100], [-161.5,-99],
  [-155,-122.5], [-152.5,-122], [-151.5,-123], [-149.5,-123], [-148.5,-124], [-147.5,-124],
  [-145,-126.5], [-145,-127.5], [-143,-130.5], [-143,-135.5], [-144,-136.5], [-145,-139.5],
  [-147.5,-142], [-148.5,-142], [-149.5,-143], [-157.5,-143], [-158.5,-142], [-159.5,-142],
  [-163,-138.5], [-163,-137.5], [-164,-136.5], [-164,-129.5], [-163,-128.5], [-163,-127.5],
  [-159.5,-124], [-158.5,-124], [-157.5,-123], [-155.5,-123], [5.5,-139], [5,-139.5],
  [5,-155.5], [5.5,-156], [6.5,-156], [14,-148.5], [14,-147.5], [6,-139.5],
  [-131,-145.5], [-130.5,-145], [-127.5,-145], [-126.5,-146], [-123.5,-147], [-121.5,-149],
  [-120.5,-149], [-119.5,-150], [-118.5,-150], [-117.5,-151], [-116.5,-151], [-115.5,-152],
  [-108.5,-155], [-106.5,-157], [-105.5,-157], [-104.5,-158], [-103.5,-158], [-102.5,-159],
  [-101.5,-159], [-90.5,-165], [-89.5,-165], [-87.5,-167], [-80.5,-170], [-78.5,-172],
  [-77.5,-172], [-76.5,-173], [-75.5,-173], [-71,-175.5], [-71.5,-176], [-141.5,-176],
  [-145.5,-173], [-146.5,-173], [-149,-170.5], [-149,-169.5], [-150,-168.5], [-150,-159.5],
  [-146.5,-157], [-144.5,-157], [-143.5,-156], [-138.5,-154], [-134,-149.5], [-134,-148.5],
  [-131.5,-146], [147,-148.5], [147.5,-148], [149,-149.5], [149,-169.5], [145.5,-173],
  [144.5,-173], [140.5,-176], [71.5,-176], [71,-175.5], [72.5,-175], [74.5,-173],
  [81.5,-170], [83.5,-168], [84.5,-168], [85.5,-167], [86.5,-167], [87.5,-166],
  [88.5,-166], [89.5,-165], [96.5,-162], [98.5,-160], [99.5,-160], [100.5,-159],
  [101.5,-159], [102.5,-158], [103.5,-158], [104.5,-157], [105.5,-157], [106.5,-156],
  [113.5,-153], [115.5,-151], [118.5,-151], [119.5,-152], [121.5,-152], [122.5,-153],
  [126.5,-153], [127.5,-154], [133.5,-154], [134.5,-153], [138.5,-153], [139.5,-152],
  [141.5,-152], [145.5,-149], [146.5,-149], [88,-189.5], [88.5,-189], [112.5,-189],
  [113.5,-190], [117.5,-190], [118.5,-191], [120.5,-191], [121.5,-192], [126.5,-194],
  [133,-200.5], [133,-201.5], [134,-202.5], [134,-204.5], [135,-205.5], [135,-208.5],
  [136,-209.5], [136,-216.5], [135,-217.5], [135,-220.5], [134,-221.5], [132,-226.5],
  [127.5,-231], [126.5,-231], [122.5,-234], [120.5,-234], [119.5,-235], [116.5,-235],
  [115.5,-236], [110.5,-236], [109.5,-237], [91.5,-237], [90.5,-236], [84.5,-236],
  [83.5,-235], [81.5,-235], [80.5,-234], [78.5,-234], [77.5,-233], [74.5,-232],
  [68,-225.5], [68,-224.5], [66,-221.5], [66,-219.5], [65,-218.5], [65,-206.5],
  [66,-205.5], [66,-203.5], [67,-202.5], [67,-201.5], [71.5,-196], [72.5,-196],
  [74.5,-194], [75.5,-194], [80.5,-191], [82.5,-191], [83.5,-190], [87.5,-190],
  [-134,-190.5], [-133.5,-190], [-85.5,-190], [-84.5,-191], [-81.5,-191], [-80.5,-192],
  [-77.5,-193], [-74,-196.5], [-74,-197.5], [-72,-200.5], [-72,-210.5], [-73,-211.5],
  [-74,-214.5], [-77.5,-218], [-78.5,-218], [-81.5,-220], [-84.5,-220], [-85.5,-221],
  [-117.5,-221], [-118,-221.5], [-118,-235.5], [-118.5,-236], [-133.5,-236], [-134,-235.5],
  [-134,-191.5], [-66,-190.5], [-65.5,-190], [-51.5,-190], [-51,-190.5], [-51,-215.5],
  [-50,-216.5], [-50,-218.5], [-47.5,-222], [-46.5,-222], [-42.5,-225], [-37.5,-225],
  [-36.5,-226], [-33.5,-226], [-32.5,-225], [-27.5,-225], [-26.5,-224], [-23.5,-223],
  [-21,-220.5], [-21,-219.5], [-19,-216.5], [-19,-190.5], [-18.5,-190], [-4.5,-190],
  [-4,-190.5], [-4,-220.5], [-5,-221.5], [-5,-223.5], [-7,-225.5], [-7,-226.5],
  [-13.5,-233], [-14.5,-233], [-15.5,-234], [-17.5,-234], [-18.5,-235], [-20.5,-235],
  [-21.5,-236], [-25.5,-236], [-26.5,-237], [-42.5,-237], [-43.5,-236], [-48.5,-236],
  [-49.5,-235], [-51.5,-235], [-52.5,-234], [-57.5,-232], [-63,-226.5], [-63,-225.5],
  [-65,-222.5], [-65,-220.5], [-66,-219.5], [-66,-191.5], [4,-190.5], [4.5,-190],
  [59.5,-190], [60,-190.5], [60,-199.5], [59.5,-200], [20.5,-200], [20,-200.5],
  [20,-206.5], [20.5,-207], [55.5,-207], [56,-207.5], [56,-216.5], [55.5,-217],
  [20.5,-217], [20,-217.5], [20,-224.5], [20.5,-225], [59.5,-225], [61,-226.5],
  [60,-227.5], [60,-235.5], [59.5,-236], [4.5,-236], [4,-235.5], [4,-191.5],
  [-117.5,-200], [-118,-200.5], [-118,-210.5], [-117.5,-211], [-91.5,-211], [-88,-208.5],
  [-88,-206.5], [-87,-205.5], [-88,-204.5], [-88,-202.5], [-89.5,-201], [-91.5,-201],
  [-92.5,-200], [-116.5,-200], [91.5,-200], [90.5,-201], [88.5,-201], [87.5,-202],
  [86.5,-202], [82,-206.5], [82,-207.5], [81,-208.5], [81,-216.5], [82,-217.5],
  [83,-220.5], [85.5,-223], [86.5,-223], [89.5,-225], [92.5,-225], [93.5,-226],
  [107.5,-226], [108.5,-225], [111.5,-225], [112.5,-224], [113.5,-224], [119,-218.5],
  [119,-215.5], [120,-214.5], [120,-210.5], [119,-209.5], [119,-207.5], [118,-206.5],
  [118,-205.5], [115.5,-203], [114.5,-203], [111.5,-201], [109.5,-201], [108.5,-200],
  [92.5,-200]
];
owl_paths = [
  [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,256,257,258,259,260,261,262,263,264,265,266,267,268,269,270,271,272,273,274,275,276,277,278,279,280,281,282,283,284,285,286,287,288,289,290,291,292,293,294,295,296,297,298,299,300,301,302,303,304,305,306,307,308,309,310,311,312,313,314,315,316,317,318,319,320,321,322,323,324,325,326,327,328,329,330,331,332,333,334,335,336,337,338,339,340,341,342,343,344,345,346,347,348,349,350,351,352,353,354,355,356,357,358,359,360,361,362,363,364,365,366,367,368,369,370,371,372,373,374,375,376,377,378,379,380,381,382,383,384,385,386,387,388,389,390,391,392,393,394,395,396,397,398,399,400,401,402,403,404,405,406,407,408,409,410,411,412,413,414,415,416,417,418,419,420,421,422,423,424,425,426,427,428,429,430,431,432,433,434,435,436,437,438,439,440,441,442,443,444,445,446,447,448,449,450,451,452,453,454,455,456,457,458,459,460,461,462,463,464,465,466,467,468,469,470,471,472,473,474,475,476,477,478,479,480,481,482,483,484,485,486,487,488,489,490,491,492,493,494,495,496,497,498,499,500,501,502,503,504,505,506,507,508,509,510,511,512,513,514,515,516,517,518,519,520,521,522,523,524,525,526,527,528,529,530,531,532,533,534,535,536,537,538,539,540,541,542,543,544,545,546,547,548,549,550,551,552,553,554,555,556,557,558,559,560,561,562,563,564,565,566,567,568,569,570,571,572,573,574,575,576,577,578,579,580,581,582,583,584,585,586,587,588,589,590,591,592,593,594,595,596,597,598,599,600,601,602,603,604,605,606,607,608,609,610,611,612,613,614,615,616,617,618,619,620,621,622,623,624,625,626,627,628,629,630,631,632,633,634,635,636,637,638,639,640,641,642,643,644,645,646,647,648,649,650,651],
  [652,653,654,655,656,657,658,659,660,661,662,663,664,665,666,667,668,669,670,671,672,673,674,675,676,677,678,679,680,681,682,683,684,685,686,687,688,689,690],
  [691,692,693,694,695,696,697,698,699,700,701,702,703,704,705,706,707,708,709,710,711,712,713,714,715,716,717,718,719,720,721,722,723,724,725,726,727,728,729,730,731,732,733,734],
  [735,736,737,738,739,740,741,742,743,744,745,746,747,748,749,750,751,752,753,754,755,756,757,758,759,760,761,762,763,764,765,766,767,768,769,770,771,772,773,774,775,776,777,778,779,780,781,782,783,784,785,786,787,788,789,790,791,792,793,794,795,796,797,798,799,800,801,802,803,804,805,806,807,808,809,810,811,812,813,814,815,816,817,818,819,820,821,822,823,824,825,826,827,828,829,830,831,832,833,834,835,836,837,838,839,840,841,842,843,844,845,846,847,848,849,850,851,852,853,854,855,856,857,858,859,860,861,862,863,864,865,866,867,868,869,870,871,872,873,874,875,876,877,878,879,880,881,882,883,884,885,886,887,888,889,890,891,892,893,894,895,896,897,898,899,900,901,902,903,904,905,906,907,908,909,910,911,912,913,914,915,916,917,918,919,920,921,922,923,924,925,926,927,928,929,930,931,932,933,934,935,936,937,938,939,940,941,942,943,944,945,946,947,948,949,950,951,952,953,954,955,956,957,958,959,960,961,962,963,964,965,966,967,968,969,970,971,972,973,974,975,976,977,978,979,980,981,982,983,984,985,986,987,988,989,990,991,992,993,994,995,996,997,998,999,1000,1001,1002,1003,1004,1005,1006,1007,1008,1009,1010,1011,1012,1013,1014,1015,1016,1017,1018,1019,1020,1021,1022,1023,1024,1025,1026,1027,1028,1029,1030,1031,1032,1033,1034,1035,1036,1037,1038,1039,1040,1041,1042,1043,1044,1045,1046,1047,1048,1049,1050,1051,1052,1053,1054,1055,1056,1057,1058,1059,1060,1061,1062,1063,1064,1065,1066,1067,1068,1069,1070,1071,1072,1073,1074,1075,1076,1077,1078,1079,1080,1081,1082,1083,1084,1085,1086,1087,1088,1089,1090,1091,1092,1093,1094,1095,1096,1097,1098,1099,1100,1101,1102,1103,1104,1105,1106,1107,1108,1109,1110,1111,1112,1113,1114,1115,1116,1117,1118,1119,1120,1121,1122,1123,1124,1125,1126,1127,1128,1129,1130,1131,1132,1133,1134,1135,1136,1137,1138,1139,1140,1141,1142,1143,1144,1145,1146,1147,1148,1149,1150,1151,1152,1153,1154,1155,1156,1157,1158,1159,1160,1161,1162,1163,1164,1165,1166,1167,1168,1169,1170,1171,1172,1173,1174,1175,1176,1177,1178,1179,1180,1181,1182,1183,1184,1185,1186,1187,1188,1189,1190,1191,1192,1193,1194,1195,1196,1197,1198,1199,1200,1201,1202,1203,1204,1205,1206,1207,1208,1209,1210,1211,1212,1213,1214,1215,1216,1217,1218,1219,1220,1221,1222,1223,1224,1225,1226,1227,1228,1229,1230,1231,1232,1233,1234,1235,1236,1237,1238,1239,1240,1241,1242,1243,1244,1245,1246,1247,1248,1249,1250,1251,1252,1253,1254,1255,1256,1257,1258,1259,1260,1261,1262,1263,1264,1265,1266,1267,1268,1269,1270,1271,1272,1273,1274,1275,1276,1277,1278,1279,1280,1281,1282,1283,1284,1285,1286,1287,1288,1289,1290,1291,1292,1293,1294,1295,1296,1297,1298,1299,1300,1301],
  [1302,1303,1304,1305,1306,1307,1308,1309,1310,1311,1312,1313,1314,1315,1316,1317,1318,1319,1320,1321,1322,1323,1324,1325,1326,1327,1328,1329,1330,1331,1332,1333,1334,1335,1336,1337,1338,1339,1340,1341,1342,1343,1344,1345,1346,1347,1348,1349,1350,1351,1352,1353,1354,1355,1356,1357,1358,1359,1360,1361,1362,1363,1364,1365,1366,1367,1368,1369,1370,1371,1372,1373,1374,1375,1376,1377,1378,1379,1380,1381,1382,1383,1384,1385,1386,1387,1388,1389,1390,1391,1392,1393,1394,1395,1396,1397,1398,1399,1400,1401],
  [1402,1403,1404,1405,1406,1407,1408,1409,1410,1411,1412,1413,1414,1415,1416,1417,1418,1419,1420,1421,1422,1423,1424,1425,1426,1427,1428,1429,1430,1431,1432,1433,1434,1435,1436,1437,1438,1439,1440,1441,1442,1443,1444,1445,1446,1447,1448,1449,1450,1451,1452,1453,1454,1455,1456,1457,1458,1459,1460,1461,1462,1463,1464,1465,1466,1467,1468,1469,1470,1471,1472,1473,1474,1475,1476,1477,1478,1479,1480,1481,1482,1483,1484,1485,1486,1487,1488,1489,1490,1491,1492,1493,1494,1495,1496,1497,1498,1499,1500,1501,1502],
  [1503,1504,1505,1506,1507,1508,1509,1510,1511,1512,1513,1514,1515,1516,1517,1518,1519,1520,1521,1522,1523,1524,1525,1526,1527,1528,1529,1530,1531,1532,1533,1534,1535,1536,1537,1538,1539,1540,1541,1542,1543,1544,1545,1546,1547,1548,1549,1550,1551,1552,1553,1554,1555,1556,1557,1558,1559,1560,1561,1562,1563,1564,1565,1566,1567,1568,1569,1570,1571,1572,1573,1574,1575,1576,1577,1578,1579,1580,1581,1582,1583,1584,1585,1586,1587,1588],
  [1589,1590,1591,1592,1593,1594,1595,1596,1597,1598,1599,1600,1601,1602,1603,1604,1605,1606,1607,1608,1609,1610,1611,1612,1613,1614,1615,1616,1617,1618,1619,1620,1621,1622,1623,1624,1625,1626,1627,1628,1629,1630,1631,1632,1633,1634,1635,1636,1637,1638,1639,1640,1641,1642,1643,1644,1645,1646,1647,1648,1649,1650,1651,1652,1653,1654,1655,1656,1657,1658,1659,1660,1661,1662,1663,1664,1665,1666,1667,1668,1669,1670,1671,1672,1673,1674,1675],
  [1676,1677,1678,1679,1680,1681,1682,1683,1684,1685,1686,1687,1688,1689,1690,1691,1692,1693,1694,1695,1696,1697],
  [1698,1699,1700,1701,1702,1703,1704,1705,1706,1707,1708,1709,1710,1711,1712,1713,1714],
  [1715,1716,1717,1718,1719,1720,1721,1722,1723,1724,1725,1726,1727,1728,1729,1730,1731,1732,1733,1734,1735,1736,1737,1738,1739,1740,1741,1742,1743,1744,1745,1746,1747,1748,1749,1750,1751,1752,1753,1754,1755,1756,1757,1758,1759,1760,1761,1762,1763,1764,1765,1766,1767,1768,1769,1770,1771,1772,1773,1774,1775,1776,1777,1778,1779,1780,1781,1782,1783],
  [1784,1785,1786,1787,1788,1789,1790,1791,1792,1793,1794,1795,1796,1797,1798,1799,1800,1801,1802,1803,1804,1805,1806,1807,1808,1809,1810,1811,1812,1813,1814,1815,1816,1817,1818,1819,1820,1821,1822,1823,1824,1825,1826,1827,1828,1829,1830,1831,1832,1833,1834,1835,1836,1837,1838,1839,1840,1841,1842,1843,1844,1845,1846,1847,1848,1849,1850,1851,1852,1853,1854,1855,1856,1857,1858,1859,1860,1861,1862,1863,1864,1865,1866,1867,1868,1869,1870,1871,1872,1873,1874,1875,1876,1877,1878,1879],
  [1880,1881,1882,1883,1884,1885,1886,1887,1888,1889,1890,1891,1892,1893,1894,1895,1896,1897,1898,1899,1900,1901,1902,1903,1904,1905,1906,1907,1908,1909,1910,1911,1912,1913,1914,1915,1916,1917,1918,1919,1920,1921,1922,1923,1924,1925,1926,1927,1928,1929,1930,1931,1932,1933,1934,1935,1936,1937,1938,1939,1940,1941,1942,1943,1944,1945,1946,1947,1948,1949,1950,1951,1952,1953,1954,1955,1956,1957,1958,1959,1960,1961,1962,1963,1964,1965,1966,1967,1968,1969,1970,1971,1972,1973,1974],
  [1975,1976,1977,1978,1979,1980,1981,1982,1983,1984,1985,1986,1987,1988,1989,1990,1991,1992,1993,1994,1995,1996,1997,1998,1999,2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010,2011,2012,2013,2014,2015,2016,2017,2018,2019,2020,2021,2022,2023,2024,2025,2026,2027,2028,2029,2030,2031,2032,2033,2034,2035,2036,2037,2038,2039,2040,2041,2042,2043,2044,2045,2046,2047,2048,2049,2050,2051,2052,2053,2054,2055,2056,2057,2058,2059,2060,2061,2062,2063,2064,2065,2066,2067,2068,2069,2070,2071,2072,2073,2074,2075,2076],
  [2077,2078,2079,2080,2081,2082,2083,2084,2085,2086,2087,2088,2089,2090,2091,2092,2093,2094,2095,2096,2097,2098,2099,2100,2101,2102,2103,2104,2105,2106,2107,2108,2109,2110,2111,2112,2113,2114,2115,2116,2117,2118,2119,2120,2121,2122,2123,2124,2125,2126,2127,2128,2129,2130,2131,2132,2133,2134,2135,2136,2137,2138,2139,2140,2141,2142,2143,2144,2145,2146,2147,2148,2149,2150,2151,2152,2153,2154,2155,2156,2157,2158,2159,2160,2161,2162],
  [2163,2164,2165,2166,2167,2168,2169,2170,2171,2172,2173,2174,2175,2176,2177,2178,2179,2180,2181,2182,2183,2184,2185,2186,2187,2188,2189,2190,2191,2192,2193,2194,2195,2196,2197,2198,2199,2200,2201,2202,2203,2204,2205,2206,2207,2208,2209,2210,2211,2212,2213,2214,2215,2216,2217,2218,2219,2220,2221,2222],
  [2223,2224,2225,2226,2227,2228,2229,2230,2231,2232,2233,2234,2235,2236,2237,2238,2239,2240,2241,2242,2243,2244,2245,2246,2247,2248,2249,2250,2251,2252,2253,2254,2255,2256,2257],
  [2258,2259,2260,2261,2262,2263,2264,2265,2266,2267,2268,2269,2270,2271,2272,2273,2274,2275,2276,2277,2278,2279,2280,2281,2282,2283,2284,2285,2286,2287,2288,2289,2290,2291,2292,2293,2294,2295,2296,2297,2298,2299,2300,2301,2302,2303,2304,2305,2306,2307,2308,2309,2310,2311,2312,2313,2314,2315,2316,2317,2318,2319,2320,2321,2322,2323,2324,2325,2326,2327,2328,2329,2330,2331,2332,2333,2334,2335,2336,2337,2338,2339,2340,2341,2342,2343,2344,2345,2346,2347,2348,2349,2350,2351,2352,2353,2354,2355,2356,2357,2358,2359,2360,2361,2362,2363,2364,2365,2366,2367,2368,2369,2370,2371,2372,2373,2374,2375,2376,2377,2378,2379,2380,2381,2382,2383,2384,2385,2386,2387,2388,2389,2390,2391,2392,2393,2394,2395,2396,2397,2398,2399,2400,2401,2402,2403,2404,2405,2406,2407,2408,2409,2410],
  [2411,2412,2413,2414,2415,2416,2417,2418,2419,2420,2421,2422,2423,2424,2425,2426,2427,2428,2429,2430,2431,2432,2433,2434,2435,2436,2437,2438,2439,2440,2441,2442,2443,2444,2445,2446,2447,2448,2449,2450,2451,2452,2453,2454,2455,2456,2457,2458,2459,2460,2461,2462,2463,2464,2465,2466,2467,2468,2469,2470],
  [2471,2472,2473,2474,2475,2476,2477,2478,2479,2480,2481,2482,2483,2484,2485,2486,2487,2488,2489,2490,2491,2492,2493,2494,2495,2496,2497,2498,2499,2500,2501,2502,2503,2504,2505,2506,2507,2508,2509,2510,2511,2512,2513,2514,2515,2516,2517,2518,2519,2520],
  [2521,2522,2523,2524,2525,2526,2527,2528,2529,2530],
  [2531,2532,2533,2534,2535,2536,2537,2538,2539,2540,2541,2542,2543,2544,2545,2546,2547,2548,2549,2550,2551,2552,2553,2554,2555,2556,2557,2558,2559,2560,2561],
  [2562,2563,2564,2565,2566,2567,2568,2569,2570,2571,2572,2573,2574,2575,2576,2577,2578,2579,2580,2581,2582,2583,2584,2585,2586,2587,2588,2589],
  [2590,2591,2592,2593,2594,2595,2596,2597],
  [2598,2599,2600,2601,2602,2603,2604,2605,2606,2607,2608,2609,2610,2611,2612,2613,2614,2615,2616,2617,2618,2619,2620,2621,2622,2623,2624,2625,2626,2627,2628,2629,2630,2631,2632,2633,2634,2635,2636,2637,2638,2639,2640],
  [2641,2642,2643,2644,2645,2646,2647,2648,2649,2650,2651,2652,2653,2654,2655,2656,2657,2658,2659,2660,2661,2662,2663,2664,2665,2666,2667,2668,2669,2670,2671,2672,2673,2674,2675,2676,2677,2678,2679,2680,2681,2682,2683,2684],
  [2685,2686,2687,2688,2689,2690,2691,2692,2693,2694,2695,2696,2697,2698,2699,2700,2701,2702,2703,2704,2705,2706,2707,2708,2709,2710,2711,2712,2713,2714,2715,2716,2717,2718,2719,2720,2721,2722,2723,2724,2725,2726,2727,2728,2729,2730,2731,2732,2733,2734,2735,2736,2737,2738,2739,2740,2741],
  [2742,2743,2744,2745,2746,2747,2748,2749,2750,2751,2752,2753,2754,2755,2756,2757,2758,2759,2760,2761,2762,2763,2764,2765,2766],
  [2767,2768,2769,2770,2771,2772,2773,2774,2775,2776,2777,2778,2779,2780,2781,2782,2783,2784,2785,2786,2787,2788,2789,2790,2791,2792,2793,2794,2795,2796,2797,2798,2799,2800,2801,2802,2803,2804,2805,2806,2807,2808,2809,2810,2811,2812,2813,2814,2815,2816,2817],
  [2818,2819,2820,2821,2822,2823,2824,2825,2826,2827,2828,2829,2830,2831,2832,2833,2834,2835,2836,2837,2838,2839,2840,2841,2842,2843],
  [2844,2845,2846,2847,2848,2849,2850,2851,2852,2853,2854,2855,2856,2857],
  [2858,2859,2860,2861,2862,2863,2864,2865,2866,2867,2868,2869,2870,2871,2872,2873,2874,2875,2876,2877,2878,2879,2880,2881,2882,2883,2884,2885,2886,2887,2888,2889,2890,2891,2892]
];

module owl_outline() { polygon(points = owl_points, paths = owl_paths); }
/* ---------- end generated owl artwork ---------- */

module owl2d(height) {
    resize([0, height], auto = true)
        owl_outline();
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
        /* The CYD's own USB socket, through the end wall opposite the GPS
         * slot. Only when there is no TP4056 in the base to charge through
         * instead. Measured off a working print; see USB_LID_*. */
        // Unconditional. EXT_CHARGER adds the TP4056's jack in the base, it
        // does not take the CYD's socket off the board, and that socket is
        // how the thing gets flashed. Gating this on the flag produced a
        // case with no way in.
        translate([USB_LID_X, -L/2, USB_LID_Z])
                rotate([90, 0, 0])
                    cylinder(h = 4 * WALL, d = USB_LID_D, center = true);

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
        // RGB LED, in the strip above the screen -- position checked on a print
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
                           LID_H - SCREEN_LIP - SCREEN_STACK - 0.5])
                    cylinder(h = SCREEN_SCREW_DEPTH + 0.5,
                             r = SCREEN_POST_PILOT);
        // micro-SD slot, through the side wall
        if (SD_SLOT)
            translate([SD_SIDE * W/2, BEZEL_Y + SD_Y, SD_Z])
                rotate([0, 90, 0])
                    linear_extrude(4*WALL, center = true)
                        offset(r = 1) offset(r = -1)
                            square([SD_HT, SD_W], center = true);
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
