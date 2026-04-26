# MC206H Printer Migration — Design

## Background

The original CSN-A2 thermal printer this project was built around is dead (over-voltage damage; confirmed via the `diag/` firmware on 2026-04-23, see `diagnostic-firmware-spec.md` and the project memory note). The replacement is a different model: an **MC206H**.

`SPEC.md` is currently written specifically against the CSN-A2 — references to the model, lead colors, command-set provenance (Adafruit's CSN-A2 reverse engineering), and tunable defaults are all CSN-A2-flavored. The networking, NTP, API integration, scheduler, and print-layout sections are printer-agnostic.

We need to migrate the project to the MC206H without throwing away the printer-agnostic work.

## Known facts about the MC206H

Confirmed via the printer's self-test page (FEED held while powering on) on 2026-04-26:

- **Command mode: EPSON (ESC/POS)** — same family as the CSN-A2; existing command-byte tables in SPEC.md and `diag/` apply.
- **Interface: USB & TTL** — both available simultaneously. We will use the TTL pins. The vendor product page's claim of "RS-232 default" is incorrect for this unit; ignore it.
- **Baud: 9600, 8N1** — matches `diag/`'s existing UART config; no code change needed there.

This collapses what would have been Phase 2's protocol-uncertainty: ESC/POS compatibility is no longer in question, only print quality (i.e., whether the heating defaults baked into `diag/` produce legible marks).

## Strategy

**Validate-first, then in-place edit `SPEC.md`.**

The existing `diag/` firmware is exactly the right acceptance test: it sends standard ESC/POS commands at 9600 baud and produces visible test patterns. If the MC206H prints anything legible, we know the protocol is compatible enough that `SPEC.md` only needs surgical edits. If it doesn't, we know early — before having rewritten anything substantial — and can pivot to a larger rewrite of just the `thermal_printer` component.

Alternatives considered and rejected:

- **Generalize SPEC.md to be printer-agnostic.** Real cost (refactor work, abstraction overhead) for hypothetical benefit (next printer swap). YAGNI for a one-off hobby project.
- **Archive SPEC.md, write fresh.** Throws away ~90% of correct content (Wi-Fi, NTP, APIs, scheduler, layout, error handling — none depends on printer model).

## Plan

### Phase 1 — Repurpose `diag/` as MC206H acceptance test

**Code (`diag/main/main.c`):** no changes needed. The firmware already sends standard ESC/POS init + ESC 7 heating params + printable text at 9600 baud on UART1 (TX=GPIO21, RX=GPIO20), which exactly matches the MC206H's confirmed config.

**Spec (`diagnostic-firmware-spec.md`):**
- Retitle: "CSN-A2 Diagnostic Firmware" → "MC206H Acceptance Test" (or similar).
- Reframe purpose: from "is this damaged head still alive" to "does this new unit speak ESC/POS at 9600 baud and print legibly".
- Update **Hardware** section: replace CSN-A2 with MC206H; remove the over-voltage backstory.
- Add a **pre-flight** subsection documenting the self-test procedure (power on with FEED held → printer prints its config page) and what to look for: command mode `EPSON(ESC/POS)`, interface includes `TTL`, baud 9600 8N1. This unit has been verified, but the procedure documents how to verify any future replacement and should run before connecting to the C3.
- Update **Interpretation** section: the success path is "Pass 1 prints cleanly" (defaults are fine); Pass 3's existence is now about confirming the head can drive every element rather than a last-ditch test on damaged hardware.

**Optional simplification:** since stress-testing damaged hardware is no longer the goal, we could collapse three passes to one. We'll keep all three for now — they're already written, they cost nothing extra to run, and pass 1 vs pass 3 output gives a useful signal for whether the default heating params will need retuning in `SPEC.md`.

### Phase 2 — Run the diag firmware

With protocol compatibility already confirmed, the remaining unknown is print quality across the three heating-parameter presets.

Flash and observe:

- **Marks come out, legible across all three passes** → defaults are fine. Note which pass produced the cleanest output (almost always pass 1 for a healthy unit). Proceed to Phase 3a using the existing defaults.
- **Marks come out, but only on later (hotter) passes** → defaults are too conservative for this unit. Note the best-looking pass's `n1/n2/n3` values and apply them as the new default in Phase 3a.
- **Paper advances cleanly but no marks** → unexpected, since ESC/POS is confirmed. Most likely cause is wiring (TX/RX swapped, missing common ground, or a damaged head). Debug before drawing protocol conclusions; Phase 3b stays as a fallback only if the head turns out to also be dead.
- **Paper doesn't advance / nothing happens / `ESP_LOGI` shows nothing** → wiring or UART config issue. Debug before continuing.

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
