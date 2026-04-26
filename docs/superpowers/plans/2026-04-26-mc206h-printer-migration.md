# MC206H Printer Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate project documentation and acceptance test from the dead CSN-A2 to the working MC206H thermal printer, preserving the printer-agnostic firmware design in SPEC.md.

**Architecture:** Documentation refactor + a single physical hardware-acceptance test against the existing `diag/` firmware. The `diag/` firmware needs only a one-line cosmetic fix; SPEC.md gets surgical edits (model swap, wiring/power audit, heating-defaults retune based on the diag run); project memory is refreshed.

**Tech Stack:** Markdown spec docs; ESP-IDF v5.x (already configured in `diag/`); file-based memory in `~/.claude/`.

**Note on TDD:** This plan is documentation refactor + a physical hardware test. There is no automated test harness for spec correctness. Verification steps are: grep checks for stale references, visual readback for coherence, and inspection of paper output from the diag run. Treat the diag run as the integration test for the whole migration.

**Reference spec:** `docs/superpowers/specs/2026-04-26-mc206h-printer-migration-design.md`

---

### Task 1: Reframe diagnostic-firmware-spec.md for the MC206H

**Files:**
- Modify: `/Users/myles/dev/little-printer/diagnostic-firmware-spec.md`

- [ ] **Step 1: Replace the title heading**

Find:
```markdown
# CSN-A2 Diagnostic Firmware
```
Replace with:
```markdown
# MC206H Acceptance Test Firmware
```

- [ ] **Step 2: Replace the Purpose section**

Find the entire Purpose section (from `## Purpose` through the blank line before `## Success criteria`) and replace it with:

```markdown
## Purpose

Minimal ESP-IDF firmware for XIAO ESP32-C3 that exercises a newly-acquired MC206H thermal printer with standard ESC/POS commands at three progressively more aggressive heating-parameter presets, to verify the printer is wired correctly, speaks the expected protocol, and produces legible output before building the full briefing firmware on top.

This is throwaway acceptance-test code, not the start of the main project. Do not pull in weather APIs, Wi-Fi, scheduling, or any of the scope from `SPEC.md`. Target: smallest possible firmware that exercises the printer.
```

- [ ] **Step 3: Update the Hardware section's printer line**

Find:
```
- **CSN-A2 thermal printer** powered by its own 9V 3A supply
```
Replace with:
```
- **MC206H thermal printer** powered by its own external supply (verify voltage/current spec on the unit's label or datasheet — typical for this class is 5–9V at 2A peak)
```

- [ ] **Step 4: Replace the baud-rate note in the Hardware section**

Find:
```
- Printer baud rate: **try 9600 first**. If garbage / nothing, rebuild with 19200 and try again. The baud is set at `uart_config_t.baud_rate` — easy to change.
```
Replace with:
```
- Printer baud rate: **9600** (confirmed via the MC206H self-test on this unit). If you swap the printer, re-run the self-test and update `uart_config_t.baud_rate` in `main/main.c` if it differs.
```

- [ ] **Step 5: Add a Pre-flight section before "Project structure"**

Find the line that reads exactly `## Project structure` and insert immediately above it (with a blank line separating):

```markdown
## Pre-flight

Before connecting any wires to the C3, verify the printer is in a state the firmware expects:

1. With the printer's data leads disconnected, hold the **FEED** button while connecting power. The printer will print a configuration page.
2. Confirm the page reports:
   - **Command mode:** `EPSON(ESC/POS)` — anything else means the firmware's command bytes won't work.
   - **Interface:** includes `TTL` — RS-232 mode would damage the C3's GPIOs.
   - **Baud:** `9600, 8N1` — anything else means update `uart_config_t.baud_rate` in `main/main.c` and rebuild before flashing.
3. If any of those don't match, do not proceed to wiring until you've reconfigured the printer (DIP switch / button combo / different baud) or updated the firmware to match.

This particular MC206H was verified against these criteria on 2026-04-26.
```

- [ ] **Step 6: Replace the Interpretation section**

Find the entire Interpretation section and replace with:

```markdown
## Interpretation

Expected outcomes for a healthy MC206H:

- **All three passes print legibly:** acceptance complete. Note which pass produced the cleanest, sharpest output — those `n1/n2/n3` heating values become the recommended defaults for `SPEC.md`'s `thermal_printer_init` and the briefing firmware.
- **Pass 1 too light, passes 2/3 better:** unit needs hotter defaults than typical. Use pass 2 or 3's values.
- **Marks scattered or missing on every pass:** likely a wiring or supply problem, not a printer fault. Re-check TX/RX swap, common ground, supply current capacity. Re-run after correcting.
- **Paper advances but no marks at all on any pass:** unexpected given the pre-flight passed. Most likely a damaged head or seriously wrong wiring. Investigate before drawing protocol conclusions.
- **Paper doesn't advance and `ESP_LOGI` shows nothing on serial:** firmware isn't running or USB-CDC console isn't set up. Check `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in sdkconfig.
- **Log messages appear but no print activity (no paper motion, no sound):** UART isn't reaching the printer. Check TX wiring and that you're on UART1 (UART0 is the USB console on the C3).
```

- [ ] **Step 7: Replace the closing-notes line**

Find:
```
- Once diagnosis is complete, this project should be deleted. The real project uses `SPEC.md`.
```
Replace with:
```
- Once acceptance is complete, this firmware can stay in the repo as a smoke test for future hardware swaps, or be deleted. The real project uses `SPEC.md`.
```

- [ ] **Step 8: Verify no stale references remain**

Run:
```bash
grep -ni 'csn-a2\|csn a2\|over-voltage\|over voltage\|damaged head\|cooked' /Users/myles/dev/little-printer/diagnostic-firmware-spec.md
```
Expected: no output. If any matches appear, re-read each in context and edit out the stale framing.

- [ ] **Step 9: Commit**

```bash
cd /Users/myles/dev/little-printer
git add diagnostic-firmware-spec.md
git commit -m "reframe diag firmware spec for MC206H acceptance"
```

---

### Task 2: Fix stale log string in diag firmware

**Files:**
- Modify: `/Users/myles/dev/little-printer/diag/main/main.c:58`

- [ ] **Step 1: Update the boot log message**

In `/Users/myles/dev/little-printer/diag/main/main.c`, find:

```c
    ESP_LOGI(TAG, "CSN-A2 diagnostic firmware booting");
```

Replace with:

```c
    ESP_LOGI(TAG, "MC206H acceptance firmware booting");
```

- [ ] **Step 2: Verify nothing else references CSN-A2 in the diag tree**

Run:
```bash
grep -rni 'csn-a2\|csn a2' /Users/myles/dev/little-printer/diag/main/ /Users/myles/dev/little-printer/diag/CMakeLists.txt /Users/myles/dev/little-printer/diag/sdkconfig.defaults
```
Expected: no output.

- [ ] **Step 3: Confirm the firmware still builds**

Run:
```bash
cd /Users/myles/dev/little-printer/diag
idf.py build
```
Expected: build succeeds. If `idf.py` is not on PATH, source the IDF export script first (`. $IDF_PATH/export.sh` or equivalent).

- [ ] **Step 4: Commit**

```bash
cd /Users/myles/dev/little-printer
git add diag/main/main.c
git commit -m "rename diag boot log from CSN-A2 to MC206H"
```

---

### Task 3: Run the MC206H acceptance test (physical task)

**Files:** none changed; output is paper.

This task requires physical access to the printer and the C3. The user does the wiring and presses keys; record observations for later tasks.

- [ ] **Step 1: Re-confirm pre-flight if any time has passed since 2026-04-26**

Power the MC206H alone (no data leads to C3) with FEED held. Confirm the config page still reports:
- Command mode: `EPSON(ESC/POS)`
- Interface includes `TTL`
- Baud: 9600, 8N1

If any criterion fails, stop and resolve before wiring.

- [ ] **Step 2: Wire the printer to the C3**

Per the diagnostic-firmware-spec.md Hardware section:
- C3 GPIO21 (D6) → printer RX (data input)
- C3 GPIO20 (D7) ← printer TX (data output, unused but wire it)
- C3 GND → printer data GND
- Printer V+ → external supply +V (5–9V; check unit label)
- Printer V- → external supply GND

Power the C3 via USB-C from the development machine. The external supply powers only the printer.

Do not run a wire from the C3 GND to the supply ground separately — the printer's data GND and power GND are typically internally tied.

- [ ] **Step 3: Build and flash**

Run:
```bash
cd /Users/myles/dev/little-printer/diag
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

Expected serial output (TAG `diag`):
```
I (xxx) diag: MC206H acceptance firmware booting
I (xxx) diag: UART1 up @ 9600 baud (TX=GPIO21, RX=GPIO20)
I (xxx) diag: Sending ESC @ (reset)
I (xxx) diag: Pass 1: DEFAULT (n1=7 n2=120 n3=40)
I (xxx) diag: Pass 2: MEDIUM (n1=11 n2=200 n3=20)
I (xxx) diag: Pass 3: MAXIMUM (n1=15 n2=255 n3=2)
I (xxx) diag: All passes complete; halting
```

Press Ctrl+] to exit the monitor once all three passes have run.

- [ ] **Step 4: Inspect the paper output**

Tear off the printed strip. Look for three labeled blocks (`=== PASS 1: DEFAULT ===`, `=== PASS 2: MEDIUM ===`, `=== PASS 3: MAXIMUM ===`), each containing the four test pattern lines (`ABCDEFGHIJKLMNOP`, `0123456789!@#$%^`, pipes, hashes).

Record:
- Which pass(es) printed legibly.
- Which pass produced the **cleanest, sharpest** output — that pass's `n1/n2/n3` values become the new heating-defaults baseline for SPEC.md.
- Any visible defects (faint columns, smearing, vertical misregistration).

- [ ] **Step 5: Decide branch**

If at least one pass printed legibly: proceed to Task 4. The cleanest pass's `n1/n2/n3` will be used.

If all three passes are blank despite confirmed pre-flight + correct wiring: stop. Do not proceed with Tasks 4-6 as written. Refer to the design doc's Phase 3b fallback section, which is out of scope for this plan.

- [ ] **Step 6: No commit**

Nothing in the repo changed in this task.

---

### Task 4: Replace CSN-A2 model references in SPEC.md

**Files:**
- Modify: `/Users/myles/dev/little-printer/SPEC.md`

- [ ] **Step 1: Enumerate every CSN-A2 occurrence**

Run:
```bash
grep -n 'CSN-A2\|CSN A2' /Users/myles/dev/little-printer/SPEC.md
```
Note the line numbers. Most are straightforward swaps. The exception is the heating-defaults paragraph in the "Thermal printer component" section, which currently credits Adafruit's CSN-A2 reverse-engineering as the provenance of the values — that's a historical fact and should NOT just become "MC206H reverse-engineering" after a bulk swap. Step 3 fixes that.

- [ ] **Step 2: Bulk-replace CSN-A2 with MC206H**

Run:
```bash
cd /Users/myles/dev/little-printer
sed -i '' 's/CSN-A2/MC206H/g; s/CSN A2/MC206H/g' SPEC.md
```
This will over-replace the Reference subsection, which we fix in the next step.

- [ ] **Step 3: Fix the over-replaced heating-defaults paragraph**

After the bulk `sed`, the heating-defaults paragraph in the "Thermal printer component" section now reads (incorrectly):
```
Defaults worth setting at init (these come from Adafruit's MC206H reverse-engineering and produce legible output on standard paper): heating dots = 11, heating time = 120 (×10µs), heating interval = 40 (×10µs).
```
This is wrong: Adafruit's library was reverse-engineered against the CSN-A2, not the MC206H.

Replace that paragraph with:
```
Starting heating values for the MC206H, established by the diag/ acceptance test: heating dots = 11, heating time = 120 (×10µs), heating interval = 40 (×10µs). Retune based on the cleanest pass observed during `diagnostic-firmware-spec.md`'s acceptance run.
```
If Task 3.4's cleanest pass was pass 2, change the values to `11 / 200 / 20`. If pass 3, change to `15 / 255 / 2`. If pass 1 was cleanest, the existing `11 / 120 / 40` are fine. (Note: the diag firmware's "default" pass uses `n1=7`, but SPEC.md's existing init recommendation was `n1=11` — keep `n1=11` for SPEC.md unless Task 3.4 specifically called out the n1=7 defaults as cleanest.)

- [ ] **Step 4: Verify replacement is clean**

Run:
```bash
grep -n 'CSN-A2\|CSN A2' /Users/myles/dev/little-printer/SPEC.md
```
Expected: no output.

Then visually skim the file and confirm:
- The model is MC206H throughout.
- The Adafruit Reference subsection still credits Adafruit (without falsely claiming Adafruit reverse-engineered the MC206H).
- The heating-defaults paragraph cites the diag/ acceptance test as the provenance.

- [ ] **Step 5: Commit**

```bash
cd /Users/myles/dev/little-printer
git add SPEC.md
git commit -m "switch SPEC.md printer model to MC206H"
```

---

### Task 5: Audit SPEC.md wiring and power against the actual MC206H

**Files:**
- Modify: `/Users/myles/dev/little-printer/SPEC.md`

- [ ] **Step 1: Inspect the MC206H's data cable and labels**

Look at the printer in hand. Note:
- Data lead colors and what each is labeled (some MC206H units use a single ribbon connector; some have flying leads).
- Connector type on the printer end (JST, screw terminal, bare wires).
- Power lead colors and connector type.
- The printed/molded voltage and current rating on the unit's label.

- [ ] **Step 2: Update the data-leads wiring table if colors or connector differ**

The current SPEC.md table is:

| Printer lead          | Typical color | → | XIAO C3 pin (label / GPIO)  |
| --------------------- | ------------- | - | --------------------------- |
| GND (data)            | Black (thin)  | → | GND                         |
| RX (printer input)    | Green         | → | **D6 / GPIO21** (UART1 TX)  |
| TX (printer output)   | Yellow        | → | **D7 / GPIO20** (UART1 RX) — optional, for status reads |

If the MC206H's actual lead colors differ, edit the "Typical color" column to match. Preserve the C3 pin assignments (right-hand column) — those don't depend on the printer.

If the MC206H uses a connector instead of flying leads, replace the "Typical color" column with a "Connector pin" column showing the pin number on whatever connector the unit ships with.

- [ ] **Step 3: Update the power-leads table similarly**

The current power table is:

| Printer lead            | → | Connection                    |
| ----------------------- | - | ----------------------------- |
| + (red, thick)          | → | Supply +V (5V–9V)             |
| − (black, thick)        | → | Supply GND                    |

Update colors / connector if the MC206H differs.

- [ ] **Step 4: Update voltage/current spec if the MC206H label differs from CSN-A2's**

Find:
```markdown
- **External 5V–9V DC power supply, minimum 2A (3A recommended)** — this is not negotiable; see power notes
```
If the MC206H's label specifies a different range or current, update the bullet to match. Then check the matching detail in `### Critical power notes` ("up to ~2A peak") and reconcile.

If the spec is unchanged, skip this step.

- [ ] **Step 5: Update heating defaults to match Task 3's cleanest pass**

(This was partially handled in Task 4 Step 3. If Task 3.4 found that pass 1 was cleanest, no further change. If pass 2 was cleanest, update the three heating-default numbers to `n1=11, n2=200, n3=20`. If pass 3, use `15/255/2`.)

Verify by reading the heating-defaults paragraph in the "Thermal printer component" section.

- [ ] **Step 6: Visual coherence check**

Open `SPEC.md` in a viewer or skim it end-to-end. Specifically check:
- No remaining stale model references.
- Wiring tables match the actual unit.
- Power section matches the actual supply requirements.
- Heating defaults match Task 3's findings.
- Cross-references between sections (e.g., "see power notes") still resolve.

- [ ] **Step 7: Commit**

```bash
cd /Users/myles/dev/little-printer
git add SPEC.md
git commit -m "update SPEC.md wiring/power/heating-defaults for MC206H"
```

---

### Task 6: Refresh project memory

**Files:**
- Replace contents of: `/Users/myles/.claude/projects/-Users-myles-dev-little-printer/memory/printer_hardware_status.md`
- Modify one line in: `/Users/myles/.claude/projects/-Users-myles-dev-little-printer/memory/MEMORY.md`

- [ ] **Step 1: Overwrite `printer_hardware_status.md`**

Write the following content to `/Users/myles/.claude/projects/-Users-myles-dev-little-printer/memory/printer_hardware_status.md`, replacing all existing content:

```markdown
---
name: MC206H printer hardware status
description: MC206H thermal printer acceptance-tested and in use; main SPEC.md project unblocked
type: project
---
The original CSN-A2 thermal printer was confirmed dead from over-voltage on 2026-04-23. A replacement MC206H thermal printer was acquired and acceptance-tested via the `diag/` firmware on 2026-04-26.

Confirmed via printer self-test page (FEED + power on):
- Command mode: EPSON(ESC/POS)
- Interface: USB & TTL (both available; we use TTL)
- Baud: 9600, 8N1

The diag/ firmware was repurposed as the MC206H acceptance test — its three-pass progressive heating sequence is now documented as a standard validation procedure for any future hardware swap.

**Why:** This memory replaces the prior CSN-A2-status note now that the project is unblocked and on different hardware.

**How to apply:** When the user resumes work on SPEC.md, the briefing firmware can be built directly. The wiring and command-set sections of SPEC.md are now MC206H-specific. The `diag/` firmware remains useful as a quick smoke test if anything seems wrong with the printer in the future.
```

- [ ] **Step 2: Update the entry in `MEMORY.md`**

In `/Users/myles/.claude/projects/-Users-myles-dev-little-printer/memory/MEMORY.md`, find the line:

```
- [CSN-A2 printer hardware status](printer_hardware_status.md) — original printer confirmed dead 2026-04-23; main SPEC.md project blocked on replacement, diag/ firmware reusable as acceptance test
```

Replace with:

```
- [MC206H printer hardware status](printer_hardware_status.md) — replacement printer acceptance-tested 2026-04-26; main SPEC.md project unblocked, diag/ firmware retained as acceptance test for future swaps
```

- [ ] **Step 3: Verify**

Read both files and confirm:
- `printer_hardware_status.md` frontmatter `name` and `description` match the body.
- `MEMORY.md` index entry's title text matches the new memory's `name`.

- [ ] **Step 4: No commit**

The memory directory is not a git repo; no commit needed.

---

## Self-Review Notes

Spec coverage:
- Phase 1 (diag spec update): Task 1 ✓
- Phase 1 corollary (stale code log string): Task 2 ✓
- Phase 2 (run diag, observe): Task 3 ✓
- Phase 3a (in-place SPEC.md edits, model + wiring + heating): Tasks 4 + 5 ✓
- Phase 3b (rewrite if protocol fails): Out of scope; flagged in Task 3 Step 5 with pointer to design doc ✓
- Phase 4 (memory update): Task 6 ✓

Identifier consistency: `n1/n2/n3` heating params used consistently across tasks. Pass numbers (1/2/3) and labels (DEFAULT/MEDIUM/MAXIMUM) match `diag/main/main.c`. File paths absolute throughout.

Open assumptions:
- Task 3 Step 3 expects `idf.py` on PATH — same assumption SPEC.md already makes.
- Task 5 cannot prescribe exact wiring-table edits sight-unseen; it instructs the engineer to inspect the unit and update the table to match. This is intentional, not a placeholder.
