# Printer Case Design — MC206H + XIAO ESP32-C3

Date: 2026-06-20

A 3D-printable desktop case that holds the MC206H thermal printer panel-mount
unit, mounts the XIAO ESP32-C3, and routes power/comms cabling. Authored as a
single parametric OpenSCAD file so dimensions are tunable without a CAD GUI and
the source diffs cleanly in git (no binary STLs committed).

## Goals

- Hold the MC206H securely, presenting it face-up on a desk, paper feeding out
  the top.
- Mount the XIAO ESP32-C3 with permanent USB-C cable access (USB-C is the C3's
  power source — it stays plugged in).
- Route the two external cables (USB-C to C3, DC power to printer) cleanly with
  strain relief; keep the internal 4-pin TTL JST run inside.
- Print on a stock FDM printer with no support material.

## Non-goals (YAGNI for v1)

- No closed/removable base plate — open bottom is sufficient. A snap-on plate
  can be added later if desired.
- No display, buttons, or panel labels.
- No cable glands or connectors beyond open notches with printed strain relief.

## Form factor

- **Desktop tray, paper up.** The printer sits face-up; the top face of the
  tray *is* the mounting panel.
- **Mounting: panel cutout + factory clips.** The MC206H drops through a
  rectangular cutout in the top face; its bezel lip rests on top and the
  factory spring clips grip the underside of that face. The top-face thickness
  is therefore set to the printer's specified panel-thickness range so the clips
  engage.

## Parts

Two parts on one print plate:

1. **Tray** — open-bottom box. Top face = mounting panel with the printer
   cutout. Side/back walls give internal depth for the printer body + loaded
   paper roll. Corner feet on the underside.
2. **C3 mount** — holds the XIAO ESP32-C3 against an interior side wall with its
   USB-C port aligned to a side notch. Implemented as an OpenSCAD `module` so it
   can be rendered integral to the tray or as a separate clip; default integral.

## Geometry detail

### Tray
- Top face (the panel): thickness `panel_thickness` (default 2 mm, set to the
  printer's clip spec). Rectangular hole `cutout_w × cutout_h`.
- The bezel lip overhangs the cutout on all sides, resting on the top face —
  `bezel_w × bezel_h` must exceed `cutout_w × cutout_h`.
- Internal height = `body_depth + roll_clearance + floor_gap` so the loaded
  paper roll clears the open underside / desk.
- Walls vertical (no overhangs) so the part prints without support.
- Outer footprint = cutout + wall thickness + margin for the C3 mount on one
  side.

### Feet
- Four corner pads on the underside, flat-bottomed, sized for stick-on rubber
  feet: `foot_dia` (default 10 mm), `foot_height` (default 3 mm). These set the
  stand-off height and keep the open bottom and bottom-edge cable notches off
  the desk.

### C3 mount
- Captures the 21 × 17.5 mm board. USB-C edge faces a **side notch** —
  an open-topped slot (not an enclosed port window) since the cable is
  permanent. A printed strain-relief bump pinches the cable so tugs don't
  transfer to the board.

### Cable routing
- **USB-C notch** — side wall, open-topped slot + strain-relief bump → C3.
- **DC power notch** — back wall, open-topped slot for the printer's 5–9 V
  supply lead → printer power JST.
- **4-pin TTL JST** — C3 ↔ printer, fully internal; just slack space, no feature.

## Parameters (top-of-file block)

All seeded with MC206H datasheet defaults; the user overrides with caliper
measurements before final render.

| Variable | Meaning | Default (to verify) |
|---|---|---|
| `cutout_w`, `cutout_h` | Panel cutout (printer drop-through hole) | TBD — measure |
| `bezel_w`, `bezel_h` | Bezel/faceplate outer size (lip) | TBD — measure |
| `panel_thickness` | Top-face thickness = clip spec | 2 mm — verify |
| `body_depth` | Printer body depth behind panel, incl. loaded roll | TBD — measure |
| `roll_clearance` | Extra space below body for roll | small margin |
| `floor_gap` | Gap from printer bottom to desk plane | derived |
| `wall` | Tray wall thickness | 2.4 mm (FDM-friendly) |
| `c3_w`, `c3_l` | XIAO ESP32-C3 board | 17.5 × 21 mm (known) |
| `foot_dia`, `foot_height` | Rubber-foot pad size | 10 mm, 3 mm |
| `usb_notch_w`, `dc_notch_w` | Cable notch widths | size to cable |

`// === MEASUREMENTS ===` block holds the printer-specific values; everything
below is derived geometry.

## Pending measurements (blockers for final render, not for design)

1. Panel cutout W × H
2. Bezel/faceplate outer W × H
3. Recommended panel thickness for the clips
4. Body depth behind panel, including loaded paper roll
5. Power JST + 4-pin TTL JST exit locations (back / side / bottom)

The model renders with defaults so geometry can be reviewed before the user has
final numbers; the user plugs in caliper values and re-renders for the print.

## Print notes

- FDM, no supports (all overhangs avoided by vertical walls and open bottom).
- Suggested: 3 perimeters, ~20% infill; PLA fine for indoor desk use.
- Single plate: tray + C3 clip (if rendered separate).
