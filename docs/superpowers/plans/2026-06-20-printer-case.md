# Little Printer Case Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A single parametric OpenSCAD model that prints a desktop tray holding the MC206H thermal printer (panel-cutout mount) with the XIAO ESP32-C3 mounted inside and cabling routed out.

**Architecture:** One file, `case/little-printer-case.scad`. A measurement block at the top carries the caliper-measured printer dimensions; everything below is derived geometry. The case is an open-bottom box whose top face *is* the mounting panel — the printer drops through a cutout, its bezel lip rests on top, factory spring clips grip the underside. The C3 lives in a slotted holder against an interior wall with its (permanent) USB-C cable exiting a side notch. OpenSCAD `assert()` statements validate the geometry on every render, so a bad dimension fails the build instead of producing silent garbage.

**Tech Stack:** OpenSCAD (CSG), rendered headless via the macOS app's CLI binary.

## Global Constraints

- OpenSCAD CLI: `/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD`. Every "run" step exports to `/tmp` with `--hardwarnings` so warnings *and* failed asserts both cause a non-zero exit:
  `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --hardwarnings -o /tmp/lpcase.stl case/little-printer-case.scad`
- Measured dimensions (calipers, 2026-06-20), copied verbatim — do NOT change these:
  - Printer body / panel cutout: **77.15 × 53.3 mm**
  - Bezel outer: **82.1 × 58 mm**
  - Body depth behind panel incl. loaded roll: **44.4 mm**
  - Top-face thickness: **3 mm** (sprung clips clamp up to ~8 mm)
- XIAO ESP32-C3 board: **17.5 × 21 mm**, USB-C on a 17.5 mm short edge.
- FDM, no support material: all walls vertical, bottom open. Never introduce an overhang that needs supports.
- Units are millimetres. Z=0 is the open bottom plane; +Z is up toward the printer face.
- Commit after each task. Files live under `case/`.

---

### Task 1: File scaffold — parameters, derived geometry, validation asserts

**Files:**
- Create: `case/little-printer-case.scad`

**Interfaces:**
- Produces: the global parameter and derived-dimension variables every later task uses — `cutout_w, cutout_h, bezel_w, bezel_h, body_depth, panel_th, wall, fit, roll_clear, foot_dia, foot_h, c3_w, c3_l, c3_th, usb_notch_w, dc_notch_w, notch_depth, box_x, box_y, box_z, inner_x, inner_y, cutout_x0, cutout_y0`. Coordinate convention as in Global Constraints.

- [ ] **Step 1: Write the file with parameters, derived dims, and asserts that must hold**

```openscad
// little-printer-case.scad — MC206H thermal printer + XIAO ESP32-C3 desktop case
// Parametric. Edit the MEASUREMENTS block if you re-measure the printer.
// Render/test: OpenSCAD --hardwarnings -o out.stl little-printer-case.scad
$fn = 48;

// === MEASUREMENTS (calipers, 2026-06-20) — printer-specific ===
cutout_w   = 77.15;  // printer body that drops through the panel hole, X
cutout_h   = 53.3;   // ditto, Y
bezel_w    = 82.1;   // faceplate outer (the lip resting on the panel), X
bezel_h    = 58.0;   // ditto, Y
body_depth = 44.4;   // depth behind the panel incl. a loaded paper roll

// === DESIGN PARAMETERS ===
panel_th    = 3;     // top-face thickness; clips self-adjust up to ~8mm
wall        = 2.4;   // side-wall thickness (FDM: ~6 perimeters at 0.4 nozzle)
fit         = 0.4;   // clearance added to the printer cutout so it drops in
clear       = 1.0;   // gap between printer body and cavity side walls
roll_clear  = 4;     // extra cavity depth below the body for roll/cable slack
c3_bay      = 19;    // extra cavity length (X) reserved for the C3 holder
foot_dia    = 10;    // stick-on rubber foot pad diameter
foot_h      = 3;     // foot height = desk stand-off
c3_w        = 17.5;  // XIAO board width (USB-C edge)
c3_l        = 21.0;  // XIAO board length
c3_th       = 1.2;   // XIAO PCB thickness
usb_notch_w = 12;    // USB-C cable channel width
dc_notch_w  = 9;     // printer DC lead channel width
notch_depth = 9;     // how far an open-topped wall notch cuts down from the rim

// === DERIVED GEOMETRY ===
inner_x = cutout_w + fit + 2*clear + c3_bay;   // cavity X (printer + C3 bay)
inner_y = cutout_h + fit + 2*clear;            // cavity Y
box_x   = inner_x + 2*wall;                     // outer footprint X
box_y   = inner_y + 2*wall;                     // outer footprint Y
box_z   = body_depth + roll_clear + panel_th;   // total height incl. top face

// printer cutout placed against the -X side, leaving the C3 bay on +X
cutout_x0 = wall + clear;                        // cutout origin X (cavity-relative + wall)
cutout_y0 = wall + (inner_y - (cutout_h + fit))/2;  // centred in Y

// bezel lip overhang per printer side (material the lip needs to rest on)
lip_x = (bezel_w - cutout_w)/2;
lip_y = (bezel_h - cutout_h)/2;

// === VALIDATION (fails the render if a measurement makes the case impossible) ===
assert(box_z - panel_th >= body_depth, "cavity too shallow for printer body + roll");
assert(inner_x >= cutout_w + fit, "cavity too narrow in X for printer body");
assert(inner_y >= cutout_h + fit, "cavity too narrow in Y for printer body");
assert(box_x >= bezel_w && box_y >= bezel_h, "top face smaller than the bezel");
assert(cutout_x0 >= lip_x, "left border too thin for the bezel lip to seat");
assert((box_y - (cutout_y0 + cutout_h + fit)) >= lip_y, "top/bottom border too thin for bezel lip");

echo(str("box: ", box_x, " x ", box_y, " x ", box_z, " mm"));
```

- [ ] **Step 2: Run it and confirm asserts pass (render exits 0)**

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --hardwarnings -o /tmp/lpcase.stl case/little-printer-case.scad`
Expected: exit code 0, console prints `ECHO: "box: 102.95 x 60.5 x 51.4 mm"` (or similar), no assert error. There is no geometry yet so the STL may be empty — that's fine for this task.

- [ ] **Step 3: Prove the asserts actually bite (negative check)**

Temporarily set `body_depth = 444;` and re-run the command. Expected: non-zero exit with `ERROR: Assertion '...' failed: "cavity too shallow for printer body + roll"`. Then restore `body_depth = 44.4;` and re-run to confirm exit 0 again.

- [ ] **Step 4: Commit**

```bash
git add case/little-printer-case.scad
git commit -m "case: parametric scaffold with measured dims and validation asserts"
```

---

### Task 2: Tray shell with printer cutout

**Files:**
- Modify: `case/little-printer-case.scad`

**Interfaces:**
- Consumes: all variables from Task 1.
- Produces: `module shell()` — the hollow open-bottom box with the printer cutout through the top face. Called at file end via a top-level `shell();` (replaced by full assembly in Task 5).

- [ ] **Step 1: Add the shell module and call it**

Append to the file:

```openscad
// Hollow open-bottom box; the top face (thickness panel_th) is the mounting panel.
module shell() {
    difference() {
        cube([box_x, box_y, box_z]);                 // solid outer block
        // cavity: hollow from the open bottom up to the underside of the top face
        translate([wall, wall, -1])
            cube([inner_x, inner_y, box_z - panel_th + 1]);
        // printer cutout straight through the top face
        translate([cutout_x0, cutout_y0, box_z - panel_th - 1])
            cube([cutout_w + fit, cutout_h + fit, panel_th + 2]);
    }
}
shell();
```

- [ ] **Step 2: Render to confirm it compiles and is non-empty**

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --hardwarnings -o /tmp/lpcase.stl case/little-printer-case.scad`
Expected: exit 0, `/tmp/lpcase.stl` exists and is larger than a few hundred bytes (real geometry now). Check size:
`ls -l /tmp/lpcase.stl`

- [ ] **Step 3: Export a top-down preview PNG to eyeball the cutout**

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --camera=0,0,0,0,0,0,300 --viewall -o /tmp/lpcase.png case/little-printer-case.scad`
Expected: exit 0; open `/tmp/lpcase.png` and confirm a rectangular tray with a rectangular hole offset toward one side (the C3 bay is the wider blank margin).

- [ ] **Step 4: Commit**

```bash
git add case/little-printer-case.scad
git commit -m "case: tray shell with printer panel cutout"
```

---

### Task 3: Corner feet and cable notches

**Files:**
- Modify: `case/little-printer-case.scad`

**Interfaces:**
- Consumes: Task 1 variables, `shell()` from Task 2.
- Produces: `module feet()` (additive) and `module cable_notches()` (subtractive, applied to the shell). USB-C notch in the +X wall, DC notch in the +Y (back) wall.

- [ ] **Step 1: Add feet and notch modules; subtract notches from the shell**

Replace the `module shell() { ... } shell();` block's trailing `shell();` call, and add modules. First add these modules anywhere after Task 1's variables:

```openscad
// Four flat-bottomed corner pads for stick-on rubber feet (also the desk stand-off).
module feet() {
    inset = foot_dia/2 + 1;
    for (x = [inset, box_x - inset], y = [inset, box_y - inset])
        translate([x, y, -foot_h])
            cylinder(h = foot_h, d = foot_dia);
}

// Open-topped slots in the walls so the permanent USB-C and the DC leads route out.
module cable_notches() {
    // USB-C: +X wall (right), aligned with the C3 bay; open from the top rim down.
    translate([box_x - wall - 1, cutout_y0 + (cutout_h + fit)/2 - usb_notch_w/2, box_z - notch_depth])
        cube([wall + 2, usb_notch_w, notch_depth + 1]);
    // DC power: +Y wall (back), toward the printer side.
    translate([cutout_x0 + (cutout_w + fit)/2 - dc_notch_w/2, box_y - wall - 1, box_z - notch_depth])
        cube([dc_notch_w, wall + 2, notch_depth + 1]);
}
```

Then change the shell module so notches are subtracted, and replace the bare `shell();` call:

```openscad
module tray() {
    difference() {
        shell();
        cable_notches();
    }
    feet();
}
tray();
```

(Leave `module shell()` as written in Task 2; just remove its standalone `shell();` call so only `tray();` renders at top level.)

- [ ] **Step 2: Render and confirm exit 0**

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --hardwarnings -o /tmp/lpcase.stl case/little-printer-case.scad`
Expected: exit 0.

- [ ] **Step 3: Preview PNG and confirm feet + two notches**

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --camera=0,0,0,55,0,25,320 --viewall -o /tmp/lpcase.png case/little-printer-case.scad`
Expected: exit 0; `/tmp/lpcase.png` shows four corner feet beneath the box and an open slot in the right wall and the back wall.

- [ ] **Step 4: Commit**

```bash
git add case/little-printer-case.scad
git commit -m "case: corner feet and USB-C/DC cable notches"
```

---

### Task 4: C3 holder

**Files:**
- Modify: `case/little-printer-case.scad`

**Interfaces:**
- Consumes: Task 1 variables, `tray()` from Task 3.
- Produces: `module c3_holder()` — a slotted bracket on the cavity floor in the C3 bay (+X side), board plane parallel to the +X wall, USB-C short edge facing the +X notch. Added inside `tray()`.

- [ ] **Step 1: Add the C3 holder module and place it inside the tray**

Add the module:

```openscad
// Slotted holder for the XIAO C3. The board stands vertically, its 17.5mm USB-C
// edge facing the +X wall notch; two grooved posts capture the long edges.
module c3_holder() {
    slot   = c3_th + 0.4;          // board slip-fit
    post_w = 3;                    // post footprint along the board edge
    post_d = 5;                    // post depth (across the board face)
    post_h = c3_l * 0.6;           // capture ~60% of the board length
    // place board centred in the C3 bay, ~6mm in from the +X wall (USB-C clearance)
    bx = box_x - wall - 6;                                  // board face X position
    by = cutout_y0 + (cutout_h + fit)/2;                   // centred on printer Y
    for (s = [-1, 1]) {
        translate([bx - post_d/2, by + s*(c3_w/2) - post_w/2, 0])
            difference() {
                cube([post_d, post_w, post_h]);
                // groove the board edge slides into, facing inboard
                translate([post_d/2 - slot/2, -1, 1])
                    cube([slot, post_w + 2, post_h]);
            }
    }
    // back stop so the board can't slide toward the wall past the USB-C gap
    translate([bx + post_d/2, by - c3_w/2 - post_w, 0])
        cube([2, c3_w + 2*post_w, 4]);
}
```

Then add the call inside `tray()`'s additive section:

```openscad
module tray() {
    difference() {
        shell();
        cable_notches();
    }
    feet();
    c3_holder();
}
tray();
```

- [ ] **Step 2: Render and confirm exit 0**

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --hardwarnings -o /tmp/lpcase.stl case/little-printer-case.scad`
Expected: exit 0.

- [ ] **Step 3: Preview PNG and confirm the holder sits in the bay under the USB-C notch**

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --camera=0,0,0,55,0,25,320 --viewall -o /tmp/lpcase.png case/little-printer-case.scad`
Expected: exit 0; two grooved posts inside the wider (C3-bay) side of the cavity, aligned with the right-wall USB-C notch.

- [ ] **Step 4: Commit**

```bash
git add case/little-printer-case.scad
git commit -m "case: slotted XIAO C3 holder in the cable bay"
```

---

### Task 5: STL export target, README, and final review

**Files:**
- Modify: `case/little-printer-case.scad`
- Create: `case/README.md`

**Interfaces:**
- Consumes: the finished `tray()` model.

- [ ] **Step 1: Export the production STL into the repo**

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --hardwarnings -o case/little-printer-case.stl case/little-printer-case.scad`
Expected: exit 0; `case/little-printer-case.stl` created. (STL is a build artifact; the `.scad` is the source of truth. Commit the STL too so a print is one download away.)

- [ ] **Step 2: Write `case/README.md`**

```markdown
# Printer case (MC206H + XIAO ESP32-C3)

Parametric OpenSCAD model for a desktop tray that holds the MC206H thermal
printer (panel-cutout mount, factory spring clips) with the XIAO ESP32-C3
inside and cabling routed out.

- Source of truth: `little-printer-case.scad`. Edit the `MEASUREMENTS` block if
  you re-measure the printer; everything else derives from it. The `assert()`s
  fail the render if a dimension makes the case impossible.
- `little-printer-case.stl` is the exported print artifact.

## Render / re-export

```bash
OPENSCAD="/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD"
"$OPENSCAD" --hardwarnings -o little-printer-case.stl little-printer-case.scad
```

## Print

- FDM, **no supports** (walls vertical, bottom open by design).
- ~3 perimeters, 20% infill, PLA is fine for an indoor desk.
- Print as-is (open bottom down on the bed); the top face / panel ends up on top.

## Assembly

1. Slide the XIAO C3 into the slotted holder, USB-C edge toward the side notch.
2. Route the permanent USB-C cable out the side notch; printer DC lead out the back notch.
3. Connect the 4-pin TTL JST between C3 and printer (stays inside).
4. Drop the printer down through the top cutout until the bezel lip seats and the
   spring clips grab the underside of the 3 mm top face.
5. Stick rubber feet onto the four corner pads.
```

- [ ] **Step 3: Self-check the model against the spec**

Re-render once more and confirm exit 0. Open `/tmp/lpcase.png` (or `case/little-printer-case.stl` in a viewer) and verify against the spec: face-up cutout, ~2.5 mm lip border on the printer sides, C3 bay with holder, USB-C side notch, DC back notch, four feet, open bottom.

Run: `"/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD" --hardwarnings -o /tmp/lpcase.stl case/little-printer-case.scad`
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add case/little-printer-case.scad case/little-printer-case.stl case/README.md
git commit -m "case: export STL, add print/assembly README"
```

---

## Self-Review

**Spec coverage:**
- Desktop tray, paper up → Task 2 shell, top-face cutout. ✓
- Panel cutout + clips mount → Task 1 dims (3 mm top face), Task 2 cutout. ✓
- C3 mount with permanent USB-C → Task 4 holder + Task 3 USB-C notch. ✓
- Cable routing (USB-C, DC; TTL internal) → Task 3 notches; TTL is slack space, no feature (per spec). ✓
- Corner rubber feet → Task 3 feet. ✓
- Open bottom → Task 2 cavity open at Z=0. ✓
- Parametric single .scad, no committed binary except the convenience STL → Tasks 1–5. ✓
- No supports / FDM → Global Constraints + Task 5 README. ✓

**Placeholder scan:** No TBD/TODO; all geometry is concrete OpenSCAD. ✓

**Type/name consistency:** `shell()`, `cable_notches()`, `feet()`, `c3_holder()`, `tray()` and all variables match across tasks. `tray()` is the single top-level call from Task 3 onward. ✓
