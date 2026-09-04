// ═══════════════════════════════════════════════════════════════════════════════
//      R2-D2 RAZOR HUB MOTOR 16x11mm AXLE CLAMPING MOUNT (PARAMETRIC)
// ═══════════════════════════════════════════════════════════════════════════════
// Designed for 16mm OD / 11mm Flat-to-Flat Double-D Stationary Axles
// Fits Home Depot R2-D2 Foot Shells for 12mm Ground Clearance

$fn = 64;

// ─── PARAMETERS ───
axle_dia      = 16.2; // Major outer rounded diameter (+0.2mm tolerance)
axle_flat     = 11.2; // Minor width across flats (+0.2mm tolerance)
axle_length   = 12.0; // Depth of axle engagement pocket
wall_thick    = 4.0;  // Backing wall thickness
clamp_width   = 40.0; // Total width of clamp body (X-axis)
clamp_depth   = axle_length + wall_thick; // Total thickness (16mm)
top_height    = 47.0; // Height from axle center to top mounting plate
bot_height    = 12.0; // Height of bottom cap
bolt_spacing  = 27.0; // Spacing between clamp bolts (X-axis)
bolt_dia      = 4.5;  // M4 clearance hole (Ø4.5mm)
flange_width  = 54.0; // Top mounting flange width
flange_thick  = 6.0;  // Top mounting flange thickness
flange_hole_x = 21.0; // Top mounting hole spacing (+/- 21mm)

// ─── DOUBLE-D AXLE PROFILE MODULE ───
module double_d_axle(h) {
    intersection() {
        cylinder(d=axle_dia, h=h, center=true);
        cube([axle_dia + 2, axle_flat, h + 1], center=true);
    }
}

// ─── 1. TOP CLAMP BRACKET (MAIN MOUNT) ───
module top_clamp_bracket(with_wire_slot=true) {
    difference() {
        union() {
            // Main vertical body
            translate([0, top_height/2, clamp_depth/2])
                cube([clamp_width, top_height, clamp_depth], center=true);
            
            // Top mounting flange
            translate([0, top_height - flange_thick/2, clamp_depth/2])
                cube([flange_width, flange_thick, clamp_depth], center=true);
        }

        // Axle Pocket (Top Half)
        translate([0, 0, axle_length/2])
            double_d_axle(axle_length + 0.1);

        // Clamping Bolt Holes (Left and Right M4 clearance)
        translate([-bolt_spacing/2, 0, clamp_depth/2])
            cylinder(d=bolt_dia, h=clamp_depth*3, center=true);
        translate([bolt_spacing/2, 0, clamp_depth/2])
            cylinder(d=bolt_dia, h=clamp_depth*3, center=true);

        // M4 Hex Nut Traps in Top Bracket
        translate([-bolt_spacing/2, 14, clamp_depth/2])
            cylinder(r=4.2, h=clamp_depth+1, $fn=6, center=true);
        translate([bolt_spacing/2, 14, clamp_depth/2])
            cylinder(r=4.2, h=clamp_depth+1, $fn=6, center=true);

        // Top Frame Mounting Holes (2x M4/M5 holes through flange)
        translate([-flange_hole_x, top_height/2, clamp_depth/2])
            rotate([90, 0, 0])
                cylinder(d=bolt_dia, h=flange_thick*3, center=true);
        translate([flange_hole_x, top_height/2, clamp_depth/2])
            rotate([90, 0, 0])
                cylinder(d=bolt_dia, h=flange_thick*3, center=true);

        // Motor Wire Escape Channel (6mm x 8mm curved slot)
        if (with_wire_slot) {
            translate([0, 8, -0.1])
                cube([8.0, 16.0, clamp_depth + 1], center=true);
        }
    }
}

// ─── 2. BOTTOM CLAMP CAP ───
module bottom_clamp_cap() {
    difference() {
        // Main bottom body
        translate([0, -bot_height/2, clamp_depth/2])
            cube([clamp_width, bot_height, clamp_depth], center=true);

        // Axle Pocket (Bottom Half)
        translate([0, 0, axle_length/2])
            double_d_axle(axle_length + 0.1);

        // Clamping Bolt Holes
        translate([-bolt_spacing/2, 0, clamp_depth/2])
            cylinder(d=bolt_dia, h=clamp_depth*3, center=true);
        translate([bolt_spacing/2, 0, clamp_depth/2])
            cylinder(d=bolt_dia, h=clamp_depth*3, center=true);

        // Counterbore for M4 Socket Head Bolt Heads
        translate([-bolt_spacing/2, -bot_height + 3, clamp_depth/2])
            cylinder(d=8.0, h=bot_height, center=true);
        translate([bolt_spacing/2, -bot_height + 3, clamp_depth/2])
            cylinder(d=8.0, h=bot_height, center=true);
    }
}

// ─── ASSEMBLY VIEW ───
// To view top bracket:
top_clamp_bracket(with_wire_slot=true);

// To view bottom cap (separated for printing):
translate([clamp_width + 10, 0, 0])
    bottom_clamp_cap();
