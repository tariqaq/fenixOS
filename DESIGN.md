# fenixOS design system

Locked design system for everything in fenixOS: the board firmware UI, the PC app, and every extra app on either side. Hallmark (`.claude/skills/hallmark`) treats this file as a locked system: it overrides Hallmark's themes.

Direction: **Apple dark**, after Apple's Human Interface Guidelines (dark mode, grouped lists, large titles, Shortcuts-style tiles), built only from openly licensed parts. Apple's SF fonts and SF Symbols are not licensed for non-Apple software (`ideas.md` C6), so they are never used.

Status: v0.1 draft, 2026-10-29. It will change through the mock-up review (`docs/mockups.md`).

## Colour tokens

All colours go through these tokens (CSS custom properties on the PC; a C++ constants header on the board, converted to RGB565). Contrast is measured against the surface each colour sits on (WCAG 2.x ratio).

### Surfaces and text

| Token | Value | Use | Contrast |
|---|---|---|---|
| `bg` | `#000000` | Screen background (board home, PC window) | |
| `surface-1` | `#1C1C1E` | Grouped list cells, cards, sidebar | |
| `surface-2` | `#2C2C2E` | Raised cells, controls, inactive tiles | |
| `surface-3` | `#3A3A3C` | Pressed state, slider track | |
| `separator` | `#38383A` | Hairlines between list rows | |
| `label` | `#FFFFFF` | Primary text and icons | 21 on `bg`, 11.4 on `surface-3` |
| `label-2` | `#98989F` | Secondary text (dates, captions, values) | 7.3 on `bg`, 5.9 on `surface-1`, 4.9 on `surface-2` |
| `scrim` | `rgba(0,0,0,0.55)` | Behind sheets and pop-ups | |

### Accents (on dark surfaces)

| Token | Value | Use | Contrast |
|---|---|---|---|
| `accent` | `#0A84FF` | Links, selected state, progress, icons | 5.8 on `bg`, 4.7 on `surface-1` |
| `accent-fill` | `#0071E3` | Filled primary buttons (white text) | white 4.7 |
| `success` | `#30D158` | Connected, switch on | 8.4 on `surface-1` |
| `warning` | `#FF9F0A` | Waiting, mismatch | 8.3 on `surface-1` |
| `danger` | `#FF453A` | Destructive text (Shut down) | 5.0 on `surface-1` |
| `danger-fill` | `#C9302C` | Destructive filled button (white text) | white 5.3 |

### Tile colours (Shortcuts-style; white glyph and label on the fill)

Deeper than Apple's bright system colours so that white labels pass 4.5:1.

| Token | Value | White text | Default use |
|---|---|---|---|
| `tile-gray` | `#636366` | 6.0 | Settings |
| `tile-indigo` | `#4B48C9` | 6.8 | Session |
| `tile-blue` | `#0062CC` | 5.8 | Stream |
| `tile-teal` | `#00707F` | 5.8 | app |
| `tile-green` | `#1E7A3A` | 5.4 | app |
| `tile-orange` | `#B55400` | 5.0 | app |
| `tile-red` | `#D70F2B` | 5.3 | app |
| `tile-purple` | `#8E3AC2` | 5.9 | app |
| `tile-pink` | `#C2185B` | 5.9 | app |

Each installed app's manifest picks one tile colour from this list.

## Type

- Family: **Inter** (OFL). Large sizes use the **Inter Display** cut. Numbers that change (clock, values) use tabular figures (`font-variant-numeric: tabular-nums`; on the board, a tabular digit set).
- Labels and captions are sentence case, untracked: no letter-spaced capitals, no eyebrow labels.

### Board (pixel sizes on the 320×240 panel)

On the board, every size is a generated anti-aliased bitmap font (`lv_font_conv`, 4 bpp), so the list is kept short.

| Role | Size / weight | Where |
|---|---|---|
| `clock` | 64 px Inter Display Semibold, tabular | Home time (portrait 56 px) |
| `title-l` | 22 px Semibold | Boot wordmark, sheet titles |
| `title` | 17 px Semibold | Nav bar title, primary buttons |
| `body` | 15 px Regular / Medium | List rows, messages |
| `label` | 13 px Semibold | Tile names, date line (Medium) |
| `caption` | 12 px Regular | Footnotes, values in small rows |

### PC app (CSS px)

| Role | Size / weight |
|---|---|
| `large-title` | 34 px Inter Display Bold |
| `title-2` | 22 px Semibold |
| `headline` | 17 px Semibold |
| `body` | 15 px Regular |
| `callout` | 14 px Regular |
| `caption` | 12 px Medium |

## Icons

**Phosphor** (MIT), v2.1.1. Fill weight on tiles and status glyphs; bold weight for chevrons, plus and close. On the board, the used glyphs are packed into an icon font (`lv_font_conv`) at 20 px and 28 px.

| Meaning | Phosphor name |
|---|---|
| Settings | `gear-six` |
| Session | `power` |
| Stream | `screencast` |
| Reboot | `arrow-clockwise` |
| Back | `caret-left` |
| Host connected / not connected | `plugs-connected` / `plugs` |
| Brightness / auto-dim / dim timeout | `sun` / `moon` / `timer` |
| Rotation / touch calibration | `device-rotate` / `hand-tap` |
| Storage / host PC / about | `hard-drives` / `desktop-tower` / `info` |

## Shape and space

- 4 px grid. Board margins 12 px (landscape) or 10 px (portrait); PC content margins 32 px.
- Corner radii: board tiles 14 px, list groups 12 px, buttons fully rounded (capsule); PC tiles 20 px, cards 14 px, buttons 10 px.
- Touch targets: at least 44 px on the PC and 40 px on the board (stylus; 40 px = 6.7 mm on this 2.8" panel).
- No shadows on the board; on the PC, only for floating sheets and menus.
- No gradients, no glass blur on the board (no RAM for it). The PC title bar may use a translucent fill.

## Motion

- Curves: ease-out `cubic-bezier(0.2, 0.8, 0.2, 1)` for most moves; a light spring (no visible overshoot beyond 4 %) for tiles appearing.
- Durations: 200 ms (press, toggle), 250 ms (fade, nav push), 350 ms (app open, sheet).
- Board app open: scale 0.9 → 1 + fade from the tile's area. Nav push: slide 100 % left with the previous page moving 30 %. Sheet: slide up from the bottom over a fading scrim. Tile install/remove: fade + scale 0.8 ↔ 1.
- Board budget: full-screen steps are held to ~30 fps (one full push is ~22 ms at 55 MHz SPI, no vsync); small regions run up to the fps cap. Every animation has a reduced-motion fallback (a plain fade) on the PC.

## Component patterns

- **Nav bar (board apps):** 32 px tall; `< Home` (caret + label, `accent`) at left, title centred (`title`).
- **Grouped list:** rows of 40 px (board) / 44 px (PC) inside a `surface-1` group with 12 px radius; leading icon in a 24 px rounded square of a tile colour; value or chevron at right in `label-2`.
- **Tile (board):** filled with its tile colour, icon at top left (20 px), name at bottom left (`label`). On the PC: the same at 260×140 with an iOS switch at bottom right.
- **Switch:** iOS-style, 51×31 on the PC; on means `success` track; off means `surface-3`.
- **Sheet / pop-up (board):** bottom sheet on `surface-1` over the `scrim`, buttons as full-width capsules.
- **Status:** shown with an icon and words, never with a lone coloured dot.
