# Public-form Mobile Polish — Design

## Background

The Fly.io message-queue form at `little-printer-msgs.fly.dev/` was built desktop-first. It works on mobile but the experience is rough:

- No viewport meta tag, so phones render the page at desktop width (~980px) and shrink to fit. Tiny text, pinch-zoom required.
- A leading-newline bug in the rendered `<textarea>` (a Prettier formatting artifact) leaves the textarea contents pre-populated with whitespace on every render.
- The submit button's touch target is small (~36px) — below Apple HIG's 44px recommendation.
- The sender input lacks mobile-keyboard hints (no autocapitalize, no autocomplete).

This pass cleans those up.

## Goals

- The form is usable on a phone without zooming or fighting the keyboard.
- The textarea doesn't render with stale whitespace.
- The submit button is tappable without precision.
- The sender field cooperates with the mobile keyboard.

## Non-goals

- A redesign. Visual style stays the same — small black-on-white form with minimal CSS.
- A new framework / build pipeline / preprocessor. Still a single static HTML file rendered by Go's `html/template`.
- Dark mode, animation, validation messaging, character counters. Out of scope for this pass.
- Restyling the attribution footer, beyond what falls out of the changes below.

## Changes

Single file modified: `fly-message-queue/templates/index.html`.

### 1. Viewport meta tag

Add to `<head>`:

```html
<meta name="viewport" content="width=device-width, initial-scale=1" />
```

This is the single highest-impact change. Without it, mobile browsers use a virtual ~980px viewport.

### 2. Textarea whitespace fix

Currently:

```html
<textarea id="message" name="message" maxlength="280" required>
{{.MessageText}}</textarea
            >
```

Renders (when `.MessageText` is empty) as `<textarea ...>\n</textarea>` — a leading newline shows up in the field on every form render. Move the template variable to be flush against the opening and closing tags so empty input renders as truly empty:

```html
<textarea id="message" name="message" maxlength="280" required>{{.MessageText}}</textarea>
```

### 3. Submit-button touch target

Bump the padding so the button is comfortably tappable. Currently `padding: 0.5rem 1.5rem`; bump to `0.75rem 1.5rem` (≈ 44px tall at 16px base font, matching Apple HIG).

### 4. Mobile-keyboard hints on the sender input

Add three attributes to the sender `<input>`:

- `autocapitalize="words"` — auto-caps the first letter of each word (typical for names)
- `autocomplete="name"` — browsers can offer to autofill from contacts
- `inputmode="text"` — explicit (not strictly required since this is the default for `<input type="text">`, but kept consistent)

On the message textarea, leave defaults — autocomplete on free-form text is usually unhelpful and `autocapitalize` defaults to "sentences" which is what we want.

### 5. iOS auto-zoom prevention

iOS Safari zooms the viewport when focusing an input whose computed font-size is < 16px. Our existing `input, textarea { font-size: 1rem; }` resolves to 16px on default browser settings, so we're already safe. No code change needed; documenting this so a future contributor doesn't accidentally drop the rule.

## Out of scope (intentional, will not be touched)

- Footer link text (the full GitHub URL stays as-is even though it's long — shortening was offered but the user opted out for this pass).
- Color scheme, fonts, button styling.
- A character counter or any client-side validation feedback.
- Server-side validation messaging UX.
- Server endpoints — this is a template-only change. No Go code modified.

## Failure modes

- **Existing browsers caching the old HTML:** the form is small and Fly's HTTP responses don't set a long cache header. After deploy, a hard refresh (Cmd+Shift+R / pull-to-refresh) is enough.
- **Template render with non-empty `MessageText`:** unlikely path (we never re-render the form with the previous message's text — successful submit clears, failed submit returns 400 plain-text). If it ever happens, the new tighter textarea formatting renders correctly.
- **Old browsers without `inputmode` / `autocapitalize`:** these attributes are silently ignored. No degradation.

## Repo / commit shape

A single commit. Then a `fly deploy` to push the change live.

## Success criteria

1. On a real iPhone (or Chrome DevTools mobile emulation), the form renders at the phone's viewport width — text is readable without zooming.
2. Loading the form shows a truly empty message textarea (no stray whitespace; cursor sits at row 1, col 1).
3. The submit button is at least 44px tall.
4. Tapping the sender field on iOS brings up a keyboard with first-letter capitalisation enabled.
5. Tapping into either input on iOS does NOT cause the page to zoom in.
