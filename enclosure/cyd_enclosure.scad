include <jl_scad/box.scad>
include <jl_scad/parts.scad>
include <jl_scad/utils.scad>

$fs = $preview ? 2 : 0.25;
$fa = 1;

$slop = 0.1;

include <cyd_dims.scad>

// Print this many boxes side by side (2 for the twin-eye faceplate; the left
// one is the same print, just turned 180 degrees in the assembly).
copies = 2;

// Position of screwholes in inside-box coordinates
hole_pos = [
  [inset.x, inset.y],
  [pcb.x - inset.x, inset.y],
  [pcb.x - inset.x, pcb.y - inset.y],
  [inset.x, pcb.y - inset.y],
];

screw_diam = 2.5;
head_d = 5; // pan head diameter 4.5–5 mm

// Base standoffs: the head pocket goes right through the floor, so the
// standoff must be wider than the head or it is held on only by its fillet.
base_post_od = 7;
base_post_fillet = 1.5; // may run into the side walls, which only braces it

// Lid posts: limited by the LCD window, which comes within lid_window_gap of
// each post centre. lid_post_od / 2 + lid_post_fillet must stay inside that.
lid_post_od = 5.5;
lid_post_fillet = 0.75;
lid_window_margin = 0.25; // solid lid left between fillet foot and window

// Ribs tying each lid post to the two nearest side walls. They hang down
// rib_h from the lid ceiling, stopping short of the PCB, and sit on the wall
// side of the post, away from the LCD.
rib_t = 1.2;
rib_h = 3;

// distance from a point to the nearest point of a rectangle [x0, y0, x1, y1]
function rect_dist(p, r) = norm([max(r[0] - p.x, 0, p.x - r[2]), max(r[1] - p.y, 0, p.y - r[3])]);
lcd_window = [lcd_center.x - lcd.x / 2, lcd_center.y - lcd.y / 2, lcd_center.x + lcd.x / 2, lcd_center.y + lcd.y / 2];
lid_window_gap = min([for (p = hole_pos) rect_dist(p, lcd_window)]);
lid_post_fillet_max = lid_window_gap - lid_post_od / 2 - lid_window_margin;
echo(str("lid post: window ", lid_window_gap, " mm from post centre, max fillet ", lid_post_fillet_max, " mm for od ", lid_post_od));
assert(lid_post_fillet <= lid_post_fillet_max, "lid_post_fillet runs into the LCD window");
assert(base_post_od > head_d + 1, "base_post_od must clear the head pocket");

$box_show_previews = false;

middle_cut = 79; // screw cross-section
//middle_cut = 40; // center cross-section
cut_face_color = "#3331";

//cut_inspect(LEFT)
//cut_inspect(LEFT,ofs=-28)
//cut_inspect(LEFT, ofs=-middle_cut / 2, s=150, color=cut_face_color) cut_inspect(RIGHT, ofs=-middle_cut / 2, s=150, color=cut_face_color) // middle cutout
box_make(print=true, explode=0.1, hide_box=false)

  box_shell_base_lid(
    box_inner,
    walls_outside=true, wall_sides=wall_sides, wall_top=wall_top, wall_bot=wall_bot
  ) {

    // PCB preview
    up(pcb_zofs) move([clearance, clearance]) box_part(BOT, FRONT + LEFT) box_preview("#44b8") diff("holes") {
              // PCB
              cuboid(pcb, anchor=BOT + FRONT + LEFT) {
                position(TOP + FRONT + LEFT) move(lcd_center)
                    recolor("#F008") cuboid([lcd.x, lcd.y, lcd.z], anchor=BOT);
              }
              ;

              // screw holes
              for (p = hole_pos)
                move(p) tag("holes") cyl(h=20, d=screw_diam);
            }

    // lid window
    move(lcd_center - [pcb.x / 2, pcb.y / 2])
      box_part(TOP) box_cutout(rect([lcd.x, lcd.y], rounding=1), chamfer=0.75);

    // screw holes for enclosure
    // box_screw_clamp has one fillet for both halves, so build each half
    // with its own call.
    position(FRONT + LEFT) move([clearance, clearance]) for (p = hole_pos)
        move(p) for (half = [BOT, TOP]) box_half(half) box_screw_clamp(
            h=pcb_zofs-$slop, // unchanged
            gap=pcb.z, // unchanged
            od=base_post_od,
            od2=lid_post_od,
            id=screw_diam + 0.2, // clearance hole through base standoff
            id2=screw_diam - 0.4, // pilot hole in lid, e.g. 2.1 for M2.5
            idepth=6, // pilot depth; don't let it reach the lid top
            head_d=head_d,
            head_depth=wall_bot+$slop, // = wall_bot, so head sits flush with the floor
            fillet=half == BOT ? base_post_fillet : lid_post_fillet
          );

    // lid post ribs, toward the walls (away from the PCB centre)
    position(FRONT + LEFT) move([clearance, clearance]) for (p = hole_pos)
        let (dir = [p.x < pcb.x / 2 ? -1 : 1, p.y < pcb.y / 2 ? -1 : 1])
          move(p) box_half(TOP) box_pos() {
              // from inside the post wall (clear of the pilot hole) to a
              // little into the side wall
              r0 = lid_post_od / 2 - 0.5;
              lx = (dir.x < 0 ? p.x : pcb.x - p.x) + clearance + 0.5 - r0;
              ly = (dir.y < 0 ? p.y : pcb.y - p.y) + clearance + 0.5 - r0;
              // orient(DOWN) flips y in this frame
              move([dir.x * r0, 0]) cuboid([lx, rib_t, rib_h], anchor=BOT + (dir.x < 0 ? RIGHT : LEFT));
              move([0, -dir.y * r0]) cuboid([rib_t, ly, rib_h], anchor=BOT + (dir.y < 0 ? FRONT : BACK));
            }

    // hanger
    Y(-3) box_part(BOT, BACK) box_cutout(keyhole(3.5, 7.5), anchor=BACK);
    
    // power jacks
    Y(jack_y) box_part(RIGHT) box_cutout(rect(jack_cutout,chamfer=0.5),chamfer=0.5);

  }
  

