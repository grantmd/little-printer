# MC206H Printer Migration — Design

## Background

The original CSN-A2 thermal printer this project was built around is dead (over-voltage damage; confirmed via the `diag/` firmware on 2026-04-23, see `diagnostic-firmware-spec.md` and the project memory note). The replacement is a different model: an **MC206H**.

`SPEC.md` is currently written specifically against the CSN-A2 — references to the model, lead colors, command-set provenance (Adafruit's CSN-A2 reverse engineering), and tunable defaults are all CSN-A2-flavored. The networking, NTP, API integration, scheduler, and print-layout sections are printer-agnostic.

We need to migrate the project to the MC206H without throwing away the printer-agnostic work.

## Known facts about the MC206H

- TTL serial interface, default 9600 baud (per included paper insert).
- Vendor product page lists "RS232/TTL+USB" with RS-232 as default — contradicts the in-box paper. The in-box paper takes precedence for the specific unit, but we will verify via the printer's self-test before any wiring (TTL vs RS-232 mismatch would fry the C3's GPIOs).
- Almost certainly speaks ESC/POS or a near-superset (most printers in this class do), but this is unverified until we run a real print test.

## Strategy

**Validate-first, then in-place edit `SPEC.md`.**

The existing `diag/` firmware is exactly the right acceptance test: it sends standard ESC/POS commands at 9600 baud and produces visible test patterns. If the MC206H prints anything legible, we know the protocol is compatible enough that `SPEC.md` only needs surgical edits. If it doesn't, we know early — before having rewritten anything substantial — and can pivot to a larger rewrite of just the `thermal_printer` component.

Alternatives considered and rejected:

- **Generalize SPEC.md to be printer-agnostic.** Real cost (refactor work, abstraction overhead) for hypothetical benefit (next printer swap). YAGNI for a one-off hobby project.
- **Archive SPEC.md, write fresh.** Throws away ~90% of correct content (Wi-Fi, NTP, APIs, scheduler, layout, error handling — none depends on printer model).

## Plan

### Phase 1 — Repurpose `diag/` as MC206H acceptance test

**Code (`diag/main/main.c`):** likely no changes needed. The firmware already sends standard ESC/POS init + ESC 7 heating params + printable text at 9600 baud on UART1 (TX=GPIO21, RX=GPIO20). All printer-agnostic ESC/POS.

**Spec (`diagnostic-firmware-spec.md`):**
- Retitle: "CSN-A2 Diagnostic Firmware" → "MC206H Acceptance Test" (or similar).
- Reframe purpose: from "is this damaged head still alive" to "does this new unit speak ESC/POS at 9600 baud and print legibly".
- Update **Hardware** section: replace CSN-A2 with MC206H; remove the over-voltage backstory.
- Add a **pre-flight** subsection: power on the printer alone with FEED held → it prints a self-test/config page → confirm reported mode is "TTL" and note the baud rate **before** wiring it to the C3. If the page reports RS-232, do not connect — find the mode switch first. If the reported baud differs from 9600, update `uart_config_t.baud_rate` in `diag/main/main.c` and rebuild before flashing.
- Update **Interpretation** section: the success path is "Pass 1 prints cleanly" (defaults are fine); Pass 3's existence is now about confirming the head can drive every element rather than a last-ditch test on damaged hardware.

**Optional simplification:** since stress-testing damaged hardware is no longer the goal, we could collapse three passes to one. We'll keep all three for now — they're already written, they cost nothing extra to run, and pass 1 vs pass 3 output gives a useful signal for whether the default heating params will need retuning in `SPEC.md`.

### Phase 2 — Run the diag firmware

Flash and observe.

- **Marks come out, legible across all three passes** → MC206H speaks ESC/POS, defaults are fine. Note which pass produced the cleanest output. Proceed to Phase 3a.
- **Marks come out, but only on later (hotter) passes** → ESC/POS works; defaults need retuning. Note the best-looking pass's `n1/n2/n3` values. Proceed to Phase 3a, applying those values as the new default.
- **Paper advances cleanly through all passes but no marks** → ESC/POS commands are accepted (the LF that advances paper is universal) but either (a) the printer uses different command bytes for printable text, or (b) it expects different framing/encoding. Proceed to Phase 3b.
- **Paper doesn't advance / nothing happens / `ESP_LOGI` shows nothing** → wiring or baud or mode (RS-232 not switched to TTL) issue. Debug before drawing any conclusion about protocol compatibility.

### Phase 3a — In-place SPEC.md edits (the easy path)

Surgical edits to `SPEC.md`:

- **Project summary, Hardware list:** `CSN-A2` → `MC206H` (every occurrence).
- **Wiring section:**
  - Verify lead colors / connector type with the printer in hand. The current table assumes black/green/yellow flying leads. MC206H may have a JST connector or different colors. Update the table to match what's on the actual unit.
  - Add an explicit "verify TTL mode via self-test before connecting" note (carry over from `diagnostic-firmware-spec.md`'s pre-flight).
- **Power section:** revisit voltage and current spec for the MC206H. CSN-A2 was 5–9V / 2A peak; MC206H may differ. Update if needed; if the user's existing supply is adequate keep the 9V 3A example.
- **Determining baud rate:** keep — universally applicable, the FEED+power self-test is standard for these printers.
- **Thermal printer component / Commands actually used:** keep the table as-is unless Phase 2 surfaced a missing or differently-numbered command. The defaults at init (heating dots = 11, time = 120, interval = 40) become whatever Phase 2 found best.
- **Reference subsection:** Adafruit's CSN-A2 library is a generic ESC/POS reference; keep it but reword to "Adafruit's CSN-A2 reverse engineering is a useful generic ESC/POS reference — most commands carry over to any ESC/POS printer including the MC206H."

### Phase 3b — Larger rewrite (only if Phase 2 fails the protocol check)

Triggered only if standard ESC/POS doesn't print on the MC206H.

- Locate MC206H datasheet / vendor docs to identify the actual command set.
- Rewrite SPEC.md's "Thermal printer component" section against that command set: update the **Commands actually used** table, the heating-init defaults, and any pacing notes.
- Everything else in SPEC.md stays as-is — Wi-Fi, NTP, APIs, scheduler, layout, error handling are protocol-independent.
- The `thermal_printer` component implementation in the eventual firmware will need a different set of byte sequences, but its public API (`thermal_printer_print`, `_set_bold`, `_set_justify`, etc.) should still hold.

### Phase 4 — Update memory

- Replace `printer_hardware_status.md` in `/Users/myles/.claude/projects/-Users-myles-dev-little-printer/memory/` to reflect: original CSN-A2 confirmed dead; MC206H acquired and (per Phase 2 outcome) acceptance-tested; main project unblocked.
- Update the entry in `MEMORY.md` index accordingly.

## Out of scope

- Building the actual briefing firmware. That's the work `SPEC.md` already describes; this design only covers migrating the spec and re-running acceptance.
- Generalizing the spec to support multiple printer models simultaneously.
- Switching frameworks, changing the C3, or revisiting the briefing's content/scheduling.

## Success criteria

- `diag/` firmware spec is updated and `diag/main/main.c` runs cleanly against the MC206H, producing legible test patterns.
- `SPEC.md` references the MC206H throughout; wiring, power, and heating-defaults sections reflect the actual unit; no stale CSN-A2 references remain except where intentionally cited as historical reference (e.g., Adafruit ESC/POS lib).
- The project memory note no longer says the project is blocked.
- A subsequent session reading SPEC.md cold can build the briefing firmware against the MC206H without surprises.
