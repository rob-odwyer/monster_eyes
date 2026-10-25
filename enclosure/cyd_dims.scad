// Shared dimensions for the CYD enclosure and the twin-eye faceplate.
// Plain OpenSCAD (no library), so faceplate.scad can use it on its own.

pcb = [85, 50, 2]; // PCB x, y, thickness
pcb_rounding = 5;
inset = [3.75, 3.75]; // PCB edge -> hole centre (x, y)
pcb_zofs = 5; // standoff height (floor to PCB underside)
clearance = 1; // PCB edge -> inner wall

// LCD x, y, thickness
lcd = [70, 50, 5];
lcd_center = [pcb.x / 2, pcb.y / 2];
lcd_gap = 0.0; // optional: keep the glass from pressing on the lid

// Box walls (passed to box_shell_base_lid)
wall_sides = 2;
wall_top = 1.5;
wall_bot = 2;

// Power jack cutout in the box's right wall: [along wall, height]. Sized for
// a typical USB-C plug overmold (about 12 x 7 mm) with room to spare.
// Centred on the inside height, jack_y along the wall (negative = front).
jack_cutout = [24, 10];
jack_y = -6;

box_inner =[pcb.x + 2 * clearance, pcb.y + 2 * clearance, pcb_zofs + pcb.z + lcd.z + lcd_gap];
box_outer = [box_inner.x + 2 * wall_sides, box_inner.y + 2 * wall_sides, box_inner.z + wall_top + wall_bot];

// --- Eye ---------------------------------------------------------------
// The library draws a square eye centred on the 320x240 panel (Eyes_Display.cpp
// setEyeSize: origin = (panel - shown) / 2), and the eye itself is the circle
// inscribed in that square. eye_px is the size it picked; the serial log prints
// it at boot ("Tables built ... (size N"). It is at most 240 on this panel.
eye_px = 240;
lcd_pitch = 0.18; // mm per pixel: 2.8" ILI9341, 57.6 x 43.2 mm active area
eye_d = eye_px * lcd_pitch;

// Active-area centre relative to the LCD outline centre, landscape, viewed from
// the front with the power jack on the right. Measure and adjust if the eye
// does not sit centred in its hole.
lcd_active_offset = [0, 0];

// Eye centre relative to the centre of the box's outer footprint (same view)
eye_in_box = lcd_center - [pcb.x / 2, pcb.y / 2] + lcd_active_offset;

// --- Twin layout ---------------------------------------------------------
// Two boxes side by side, landscape. The left box is turned 180 degrees so
// both power jacks face outward; that board needs its image flipped in
// firmware (setOrientation(false, true, true, true)).
box_gap = 3; // between the two boxes' outer walls

box_centers = [
  [-(box_outer.x + box_gap) / 2, 0],
  [(box_outer.x + box_gap) / 2, 0],
];
box_spins = [180, 0];
eye_centers = [
  box_centers[0] - eye_in_box, // rotated 180
  box_centers[1] + eye_in_box,
];
eye_spacing = eye_centers[1].x - eye_centers[0].x;
