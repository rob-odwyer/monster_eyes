// Faceplate that bridges two CYD enclosures and shows only the eyes.
//
// The front is any 2D outline, extruded. Use the built-in pumpkin or set
// face_svg to your own shape (an SVG or DXF with one closed outline; any holes
// inside it are cut through as well, e.g. a jack-o'-lantern mouth).
// The back has two sockets the box lids press into, which also set the eye
// spacing. Print it front-face down: no supports, and the bed gives the front
// a clean finish.
//
// part = "print"    : the faceplate, oriented for printing
//        "assembly" : faceplate with stand-in boxes, viewed from the front
//        "outline"  : the 2D front outline with eye and socket positions

include <cyd_dims.scad>

part = "print"; // [print, assembly, outline]

$fs = $preview ? 1 : 0.25;
$fa = 1;

// --- Shape ---------------------------------------------------------------
face_svg = ""; // e.g. "shapes/skull.svg"; empty = built-in pumpkin
face_width = 214; // the outline is scaled to this width, height follows
eyes_y = 4; // eye row height above the outline's centre

// --- Plate -----------------------------------------------------------------
face_thickness = 2.4;
eye_chamfer = 1.5; // front bevel on the eye holes, opens up the viewing angle

// Grooves engraved on the front (pumpkin ribs). Ignored for face_svg.
rib_width = 1.6;
rib_depth = 0.6;

// --- Sockets ---------------------------------------------------------------
// Rims on the back that the box lids push into. Each rim has a notch where
// the box's power jack cutout is, so the plug is not blocked.
socket_fit = 0.25; // per side; raise for a looser push fit
socket_wall = 1.6;
socket_depth = 5;
socket_inner = [box_outer.x + 2 * socket_fit, box_outer.y + 2 * socket_fit];

// --- 2D --------------------------------------------------------------------
// All 2D shapes are as seen from the front, centred on the eye row's midpoint.

module ellipse(r) scale([r.x, r.y]) circle(r=1, $fn=96);

// Built-in pumpkin, unit width (x from -0.5 to 0.5), lobes centred on y=0
pumpkin_lobes = [
  // [x, rx, ry]
  [0, 0.17, 0.33],
  [-0.15, 0.2, 0.33],
  [0.15, 0.2, 0.33],
  [-0.28, 0.22, 0.31],
  [0.28, 0.22, 0.31],
];

module pumpkin_unit() {
  for (l = pumpkin_lobes) translate([l[0], 0]) ellipse([l[1], l[2]]);
  // stem, leaning a little to the right
  polygon([[-0.045, 0.27], [0.045, 0.27], [0.065, 0.39], [0.085, 0.41], [0.075, 0.425], [0.015, 0.42]]);
}

module pumpkin_ribs_unit(w) {
  // inner lobe edges, kept off the outer rim
  intersection() {
    offset(delta=-2 * w) for (l = pumpkin_lobes) translate([l[0], 0]) ellipse([l[1], l[2]]);
    for (l = [for (i = [0:2]) pumpkin_lobes[i]]) translate([l[0], 0])
        difference() {
          ellipse([l[1], l[2]]);
          ellipse([l[1] - w, l[2] - w]);
        }
  }
}

module face_outline() {
  translate([0, -eyes_y])
    if (face_svg == "")
      scale(face_width) pumpkin_unit();
    else
      resize([face_width, 0], auto=true) import(face_svg, center=true);
}

module face_ribs() {
  if (face_svg == "")
    translate([0, -eyes_y]) scale(face_width) pumpkin_ribs_unit(rib_width / face_width);
}

module eye_holes(extra=0) {
  for (e = eye_centers) translate(e) circle(d=eye_d + 2 * extra);
}

module socket_rects(grow=0) {
  for (c = box_centers) translate(c)
      square([socket_inner.x + 2 * grow, socket_inner.y + 2 * grow], center=true);
}

// Power jack cutouts, on each box's right side before it is spun
module jack_notches() {
  for (i = [0, 1]) translate(box_centers[i]) rotate(box_spins[i])
        translate([box_outer.x / 2 - 1, jack_y - (jack_cutout.x + 1) / 2])
          square([socket_wall + socket_fit + 2, jack_cutout.x + 1]);
}

// --- 3D --------------------------------------------------------------------

// Front face on z=0, back toward +z. Viewed from below, so x is mirrored.
module faceplate_print() {
  mirror([1, 0, 0]) difference() {
      union() {
        linear_extrude(face_thickness) face_outline();
        translate([0, 0, face_thickness - 0.01])
          linear_extrude(socket_depth + 0.01) difference() {
              socket_rects(socket_wall);
              socket_rects();
              jack_notches();
            }
      }
      // eye holes, bevelled on the front
      for (e = eye_centers) translate([e.x, e.y, 0]) {
          translate([0, 0, -0.01]) cylinder(h=face_thickness + 0.02, d=eye_d);
          translate([0, 0, -0.01]) cylinder(h=eye_chamfer, d1=eye_d + 2 * eye_chamfer, d2=eye_d);
        }
      translate([0, 0, -0.01]) linear_extrude(rib_depth + 0.01) difference() {
          face_ribs();
          eye_holes(1);
        }
    }
}

// Stand-in box in the print frame: lid on z=0, floor up at box_outer.z.
// The red block marks the power jack cutout on the box's right side.
module box_standin() {
  color("#4488cc") translate([-box_outer.x / 2, -box_outer.y / 2, 0]) cube(box_outer);
  color("#cc3333") translate([box_outer.x / 2, jack_y - jack_cutout.x / 2, (box_outer.z - jack_cutout.y) / 2])
      cube([2, jack_cutout.x, jack_cutout.y]);
}

if (part == "print") {
  faceplate_print();
} else if (part == "assembly") {
  // front facing +z, boxes behind it
  rotate([0, 180, 0]) {
    color("#e87a1d") faceplate_print();
    mirror([1, 0, 0]) for (i = [0, 1])
        translate([box_centers[i].x, box_centers[i].y, face_thickness]) rotate(box_spins[i]) box_standin();
  }
} else if (part == "outline") {
  difference() {
    face_outline();
    eye_holes();
  }
  %socket_rects(socket_wall);
}

echo(str("faceplate: eye spacing ", eye_spacing, " mm, eye hole ", eye_d, " mm, sockets span ",
  2 * (box_outer.x + 2 * (socket_fit + socket_wall)) + box_gap, " x ", box_outer.y + 2 * (socket_fit + socket_wall), " mm"));
