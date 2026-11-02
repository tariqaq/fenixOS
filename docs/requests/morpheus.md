# fenixOS feature requests from Morpheus

For the Claude Code session that works on `tariqaq/fenixOS`. Read this, check each request against the current code and docs (`docs/packages.md`, `proto/spec.md`, `docs/sidecars.md`, `docs/registry.md`, `DESIGN.md`), and say for each one whether fenixOS can support it, how, and in which release.

## Context

Morpheus (`tariqaq/morpheus`, id `io.github.tariqaq.morpheus` planned) is a fenixOS app. Its board screen **morphs to match what the user is doing on the PC**:

- **Media / video:** album art, title and artist, live seek bar, play/pause, skip, volume.
- **Game:** game name, session time, stats.
- **Call:** mute, deafen, call timer.
- **Glance:** anything else, described in a few short AI-written lines.

The PC half (Rust, `fenix-sdk`) works out the context. It uses Windows APIs (SMTC for media, Core Audio, the foreground window) plus DeepSeek reading screenshots. It then drives the board with `set`.

Checked against fenixOS `main` (latest tag v0.1.19) on 2026-11-01. Morpheus was made from the app template for v0.1.13.

## What blocks it today

| Need | Today | Where |
|---|---|---|
| Switch between whole screens (media / game / call / glance) at runtime | `hidden` is a compile-time prop (`hidden: 0/1`, prop 22). Nothing can show or hide an element at runtime | `docs/packages.md` (props, binary prop table) |
| Show album art or a thumbnail sent by the PC | `BIND_SET` kinds are 0 int, 1 bool, 2 text. Images exist only inside the package (24 KB in total) | `proto/spec.md` 0x30; `docs/PLAN.md` planned an "image ref" kind |
| Toggle the play/pause button's icon | A button's `icon` and `text` are static. `bind` on a button has no meaning | `docs/packages.md` |
| Take screenshots, declared to the user | The permission list is `keys`, `media`, `system-stats`, `network`, `files`, `programs`. None covers screen capture | `docs/registry.md` |
| Show non-ASCII song and video titles | Text fonts hold printable ASCII plus `° · × – — ’ • … ‹ ›` only. Japanese, Chinese, Arabic, accented Latin and so on show as blanks | `docs/packages.md` (fonts) |

## Requests, in priority order

### 1. Runtime screen switching (must have)

Morpheus is pointless without this. Two possible shapes. fenixOS should pick one, or offer something better:

- **a. A `view` element** (or `pages`/`switch`): a container that shows exactly one of its children, chosen by an int binding (`"bind": "mode"`, value = child index). Children that are not shown are not laid out. This fits the declarative model best, and the board can animate the change the way it animates other value changes (DESIGN.md: apps have no motion props, the board animates).
- **b. Bindable visibility:** a prop like `"show": "<binding>"` on any element, which takes a bool binding.

Morpheus would have about 4–6 modes in one package. That stays inside 200 elements and 64 bindings if the modes share parts.

Questions for fenixOS:
- Do hidden children still count against RAM while the app is open, or are they built only when shown?
- Should values bound inside a hidden child be kept, the way the per-binding cache already works?

### 2. Image binding (must have for media)

A new `BIND_SET` kind (3, image) sets an `image` element's pixels at runtime.

- **Use:** album art from SMTC, about 96×96, maybe 120×120 in landscape.
- **Format:** RGB565 raw is enough. 96×96 is 18 KB, about 0.2 s over USB at 921,600 baud. It would be sent once per track, not per frame.
- **Possible rules:** the element declares its `w` × `h` and a `bind`. The PC sends exactly that size, and the SDK scales and converts. Cap the size per binding and per app. Cache it like the other bindings while the app is open.
- **SDK:** something like `app.set_image(bind, w, h, &rgb565)` or `app.set_image_png(bind, &png)`, where the SDK does the resizing.
- **Message size:** a sidecar message is at most 64 KB, which is enough for this.

Questions for fenixOS:
- Is there heap or PSRAM for one or two such buffers while an app is open? (The CYD has no PSRAM, so this matters.)
- Should it be limited to one image binding per app?

### 3. Bindable icon and button text (should have)

A play/pause button needs its icon to follow the playback state. Ideas:

- `bind` on a `button` sets its text (string), or
- a separate binding for the icon (an index into the 20 built-in glyphs, or a name).

Also useful: the bound `icon` element taking its glyph from a binding (for example a mute or unmute mic).

Built-in glyphs Morpheus would need beyond today's 20 (Phosphor fill): `play`, `pause`, `skip-forward`, `skip-back`, `speaker-high`, `speaker-x`, `microphone`, `microphone-slash`, `headphones`, `game-controller`, `music-notes`, `video`, `phone`, `sparkle`. If they are not added, Morpheus can ship them as A8 `image` assets (within the 24 KB), but then they cannot switch at runtime unless request 1 or 3 covers images too.

### 4. A `screen` permission (must have, registry only)

Morpheus takes screenshots (the whole display or the foreground window) and sends them to an AI API. AGENTS.md says: "do no more than declared". Add a permission such as `screen` ("Captures your screen") to the registry rules (`tools/registry.py`), the Add app permissions sheet and the app template's `tools/app.py`. Morpheus will capture only through Windows Graphics Capture: no hooks, no injection, no reading game memory.

### 5. Wider text coverage (nice to have)

Song and video titles are often non-ASCII. Options:

- Add Latin-1 / Latin Extended-A to the text fonts (covers most European titles), or
- document that apps must transliterate, and maybe ship a helper in `fenix-sdk` that maps text to the board's glyph set (`é` becomes `e`, unknown characters become `?` instead of a blank).

CJK is likely too big for flash. Say so if it is.

### 6. Nice to have

- **Bindable `min`/`max` on `slider` and `bar`.** Without it, Morpheus scales the seek bar to 0–1000. That works, so this is low priority.
- **Shorter `opened` resync.** Morpheus may need to send about 50 values when it opens. Confirm that is fine over the link, or suggest a batch `set`.
- **Label overflow.** Track and video titles are long. What does a bound label do when its text is wider than the label: wrap, clip, or ellipsis? Morpheus would like a `max_lines` or an ellipsis option (`…` is already in the fonts).

## Questions (please answer)

1. **Settings storage:** Morpheus keeps the user's DeepSeek API key in a `text` setting. Where does the PC app store settings, is it encrypted (DPAPI?), and does it show up in logs or exports? If it is plain text, could a `secret` setting type (masked, stored with DPAPI) be added?
2. **Elevated helper:** Morpheus will set `pc.elevated: true` (UAC once each start, for ETW frame timing to get FPS). The docs say non-elevated helpers run in a job object. How does fenixOS make sure an elevated helper ends: only `stop` plus the closed pipe? And are crash restarts also elevated (another UAC prompt each time)?
3. **Permissions Morpheus will declare:** `media`, `keys` (Discord mute/deafen keybinds), `system-stats`, `network` (DeepSeek), and the new `screen`. Is that list right for these uses?

## How Morpheus plans to use these

- One `view` with 6 children: setup, game, call, video, media and glance (`screens.md` in the Morpheus repo has the layouts). About 145 elements and 48 bindings in total.
- One image binding: 80 × 80 RGB565 album art or video thumbnail, sent once per track.

## What Morpheus does in the meantime

Until requests 1 and 2 ship, Morpheus can only be one generic card whose text, bar and buttons change with the context. Once fenixOS ships them, Morpheus sets `min_fenixos` to that release.

Since then: v0.1.27 (2026-11-02) has audit fixes only (`docs/audit-2026-11.md`); v0.1.28 (2026-11-03) opens apps with the system slide when RAM allows (a Morpheus-sized layout with art still fades in from black) and notices a pulled or returned SD card. Nothing changes in the app format; the template is pinned to v0.1.28.

## Shipped in fenixOS v0.1.26 (2026-11-02)

Request 5 and the settings answer. Set `min_fenixos = "0.1.26"` in `app.toml` (and in the manifest when you use `secret`: the registry checks it). The template is pinned to v0.1.26.

- **Titles in any language:** the board's text fonts now hold Latin-1 (accented letters such as `é ñ ü Å ß`), firmware +50.5 KB. Chinese, Japanese and Korean show when the user has the CJK font on the card (v0.1.24). For the rest, `fenix_sdk::text::fit(title, cjk_font)` transliterates (`Привет` to `Privet`, `Łódź` to `Lódz`, `北京` to `Bei Jing` without the CJK font) and puts one `?` per emoji. `cjk_font` is yours to decide (for example a toggle setting "CJK font installed"); the board does not report it yet. Reference: `docs/sidecars.md`, `docs/packages.md` (fonts).
- **Long titles:** a label with a width takes `"overflow": "ellipsis"` and `"max_lines": 2` (ends in `...`), `"clip"`, or `"scroll"` (one line that scrolls round). Older boards ignore them and wrap. Example: the title in `tools/view-app`, simulator shot `app_view`.
- **API key:** a setting `{"key": "deepseek_key", "type": "secret", "label": "DeepSeek API key"}` (no `default`). The App settings page shows a password field (empty, "Saved, type to replace", Clear); `config.json` holds it DPAPI-encrypted for the Windows user; it is never logged; your PC half gets it in `settings` as plain text (`""` until set). Do not log it or `set` it on the board. Reference: `docs/registry.md` (settings).

## Shipped in fenixOS v0.1.25 (2026-11-02)

Requests 1 to 4 and the `opened` burst are in v0.1.25; set `min_fenixos = "0.1.25"` (`fenix-pkg build` refuses `view`, `show`, a bound image or the new icons without it, and the registry refuses `screen`). The app template is pinned to v0.1.25 (`fenix-pkg`, `fenix-sdk`). Reference: `docs/packages.md`, `docs/sidecars.md`, `proto/spec.md` 0x32, `docs/registry.md`; a working example layout is `tools/view-app` (simulator shots `app_view`, `app_view_call`).

- **Screens:** `{"type": "view", "bind": "mode", "children": [...]}` shows child `mode` (0 first; `value` until you set it). Only the shown child is built: switching deletes the old screen's objects and builds the next one, which fades in (200 ms). Values for screens not shown are kept and applied when they show. `"show": "<bool binding>"` on any element hides or shows it (it is built either way).
- **Album art:** one `image` with `"bind": "art", "w": 80, "h": 80` (1 to 96 px a side, fixed, the same in portrait). PC half: `app.set_image("art", w, h, &rgba)` with any picture; fenixOS scales it to fill the element (centre crop), converts to RGB565 and sends it paced. Measured: 80 × 80 in ~0.2 s, 96 × 96 in ~0.25 s over USB. Until the first picture it shows `src` or a `surface_2` square.
- **Icons and button text:** int `bind` on an `icon`, or on a button, picks the glyph; text `bind` on a button sets its text. `app.set_icon("play_icon", "pause")`. The 14 glyphs you listed are built in (indices 20 to 33 in `fenix_sdk::ICONS`).
- **Permission:** `screen`, shown as "See your screen and the window in front".
- **Burst:** ~50 values on `opened` arrive whole (PC paces 16 per 50 ms, board keeps a 4 KB ring).

**What changed from the 2026-11-01 answer below (measured on the board, `docs/board-info.md` "App RAM with views and album art"):**

- Corrected: Home has **~54 KB** free with Wi-Fi on and the CJK font installed, not ~82 KB (that was v0.1.12).
- Corrected: hidden view children are **not** built (the answer said they are built at open and count in RAM). Building all of a 147-element, 6-screen layout ran the board out of memory.
- Corrected: the RAM estimate cap went **down to 32 KB**, not up to ~56 KB. It now counts what is built at once (a view counts its largest child) × 160 B, plus 32 B per element in the package, plus strings, images and the art buffer. Your planned layout (~145 elements, one ~25-element screen at a time, 80 × 80 art) estimates ~23 KB.
- Corrected: the art cap is **96 × 96**. 120 × 120 left 0.9 KB of heap at the lowest point. With 96 × 96 the lowest point was 20.4 KB with the PC on USB and ~12.5 KB with the PC on Wi-Fi (Wi-Fi receive bursts take up to ~16 KB for a moment). 80 × 80 leaves ~6 KB more.
- App open and close now fade from black: Home is deleted before the app is built, so the two never need RAM at once.

## fenixOS answer (2026-11-01)

Discussed with the user; nothing was built yet (built since: see "Shipped in fenixOS v0.1.25" above, which corrects the RAM numbers here). Plan: P50-M (v0.1.25) and P51-M (v0.1.26; checklists now in `docs/legacy/tasks-history.md`), two releases (corrected 2026-11-02: were P50 and P51 for v0.1.20 and v0.1.21; they now come after the SD card and OTA phases). Choices: `docs/decisions.md`, "2026-11-01: Morpheus requests".

**RAM budget.** Sized for an app open with Wi-Fi on and the PC linked (over Wi-Fi or USB), no screen stream: about 82 KB free at boot in that mode, about 155 KB with the radio off (`docs/board-info.md`). Streaming while an app runs is not a target (user). Anything that fits the Wi-Fi case fits USB-only.

| # | Request | Answer | Release |
|---|---|---|---|
| 1 | Runtime screen switching | Yes: a `view` element (new kind 9), int `bind` = index of the one child shown, plus a bindable bool `show` on every element (user, 2026-11-02) | P50-M |
| 2 | Image binding | Yes: a new chunked message `BIND_IMAGE` (0x32, optional, no proto bump), one per app, RGB565, 96×96 to 120×120, buffer counted in the RAM estimate | P50-M |
| 3 | Bindable button text and icon, 14 new glyphs | Yes: string `bind` on a button sets its text; int `bind` on an `icon` (or icon button) picks the glyph; all 14 glyphs exist in Phosphor fill | P50-M |
| 4 | `screen` permission | Yes: "See your screen and the window in front" (covers the foreground window too) | P50-M |
| 5 | Wider text | Latin-1 Supplement (0xA0–0xFF) in the text fonts, plus an SDK helper that transliterates the rest (`?` for unknown). CJK: not on the 4 MB flash; **from the SD card since v0.1.24**: Chinese, kana and Hangul show in any label once the PC app's Board page installs the font (`docs/fonts.md`); no change for apps | P51-M (CJK: P54, done) |
| 6 | Bindable `min`/`max` | Not planned: scaling to 0–1000 works | — |
| 6 | 50 values on `opened` | Must fix: the board queues 16 binding values and drops the rest (`link.cpp`), and an unchanged value is never resent. PC sends the latest value per binding and paces the burst; the board's queue gets smaller entries and more of them | P50-M |
| 6 | Label overflow | Today: a label without `w` grows and is cut off at the screen edge; with `w` it wraps on as many lines as needed. Planned: `overflow` (`wrap`, `clip`, `ellipsis`, `scroll`) and `max_lines` | P51-M |

Details:

- **View:** hidden children are built when the app opens and count against RAM (145 elements ≈ 23 KB of the estimate). Values bound inside a hidden child are kept and applied (per-binding cache). Old firmware refuses kind 9 at install; the PC app already refuses apps whose `min_fenixos` is newer than itself.
- **Image:** at 80×80 Morpheus estimates ~39 KB (elements + strings + 12.8 KB image), at the current 40 KB cap. The cap rises (about 56 KB) only after a Morpheus-sized layout with 120×120 art is measured on the board in both modes; if the lowest free heap with Wi-Fi on drops below about 20 KB, the art cap is 96×96. One buffer: the new art shows once the transfer completes. About 0.3 s per 80×80 image over USB. SDK: `set_image(bind, w, h, &rgba)` resizes and converts; a new pipe message `set_image`.
- **Icons:** about 20–25 KB of flash for 14 glyphs in the 9 fill sizes (estimate). The SDK maps names to indexes (`set_icon(bind, "pause")`).
- **Fonts:** Latin-1 adds an estimated ~80 KB of flash across the 9 text fonts (measured when built). Latin Extended-A as well would be ~190 KB.

Questions:

1. **Settings storage:** values are plain JSON in `%APPDATA%\fenixOS\config.json`, not encrypted. They are not written to logs (`apps.rs`), and there is no export. The App settings page shows `text` values unmasked. A `secret` type (masked, DPAPI user scope, never logged) is planned in P51-M (user, 2026-11-02: yes). Built: see "Shipped in fenixOS v0.1.26" above.
2. **Elevated helper:** stop is the `stop` message plus the closed pipe; `TerminateProcess` fails on an elevated process, so a helper that hangs without reading its pipe keeps running. If fenixOS dies, the pipe breaks and `fenix-sdk` exits. Every crash restart (1 s, 5 s, 15 s) goes through UAC again, and a declined prompt ends the retries. Better: no elevation. ETW frame timing (PresentMon) works for members of the Performance Log Users group; add the user once (sign out after), no prompt per start.
3. **Permissions:** `media`, `keys`, `system-stats`, `network`, `screen` fits. `keys` covers the simulated Discord keybinds. Screen capture and ETW next to games with kernel anti-cheat: unverified, say so in Morpheus's README.

Waited for the SD card (in fenixOS since v0.1.20): CJK fonts and text beyond Latin-1 (CJK from the card: done in v0.1.24, `docs/fonts.md`), building screens only when shown.
