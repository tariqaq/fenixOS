# Design language (locked)

Copied from fenixOS `DESIGN.md`, cut to what an app screen uses. Every fenixOS app looks like the system screens: Apple-style dark, grouped cards, Inter, Phosphor icons. Do not invent colours, fonts, shadows, gradients or new patterns. Machine-readable tokens: `design/tokens.json`.

## Colours (board tokens; write them with `_` in `layout.json`)

| Token | Value | Use |
|---|---|---|
| `bg` | `#000000` | Screen background (already black under your layout) |
| `surface_1` | `#1C1C1E` | Cards and grouped lists |
| `surface_2` | `#2C2C2E` | Raised controls, secondary buttons |
| `surface_3` | `#3A3A3C` | Pressed state, slider and bar tracks |
| `separator` | `#38383A` | Hairlines between rows (a 1 px box) |
| `label` | `#FFFFFF` | Primary text and icons |
| `label_2` | `#98989F` | Secondary text: captions, units, labels above values |
| `accent` | `#0A84FF` | Links, selection, progress, the one highlight |
| `accent_fill` | `#0071E3` | Filled primary button (white text) |
| `success` | `#30D158` | Connected, on, good |
| `warning` | `#FF9F0A` | Waiting, needs attention |
| `danger` | `#FF453A` | Destructive text |
| `danger_fill` | `#C9302C` | Destructive filled button |
| `tile_*` | gray `#636366`, indigo `#4B48C9`, blue `#0062CC`, teal `#00707F`, green `#1E7A3A`, orange `#B55400`, red `#D70F2B`, purple `#8E3AC2`, pink `#C2185B` | The app's tile colour (one, in `app.toml` and `manifest.json`); white text passes 4.5:1 on each |

Text on `surface_1` and `surface_2` uses `label` or `label_2` only. Status is shown with an icon and words, never with a lone coloured dot.

## Type (board fonts)

| Font | Size / weight | Use |
|---|---|---|
| `clock_64`, `clock_56` | 64 / 56 Inter Display Semibold, tabular | One huge number (digits, `:`, `-` only) |
| `title_l` | 22 Semibold | Big values on cards |
| `title` | 17 Semibold | Section titles, important values |
| `body`, `body_semibold` | 15 | Rows, messages; semibold for emphasis |
| `label`, `label_medium`, `label_semibold` | 13 | Labels above values, small rows |
| `caption`, `caption_medium` | 12 | Footnotes, units |

Sentence case, untracked. No all-caps labels, no eyebrow text. Keep words few: the screen is 2.8 inches.

## Shape and space

- 4 px grid. Screen margin 12 px landscape, 10 px portrait (the root box's `pad`).
- Cards: `bg: surface_1`, `radius: 12`, `pad: 10`, gap 4 to 10. Never a card inside a card.
- Buttons are capsules (the `button` element). Primary: `bg: accent_fill`; secondary: `bg: surface_2`; destructive: `bg: danger_fill`.
- Touch targets at least 40 px tall (buttons, switches, slider rows).
- No shadows, gradients or blur on the board.

## Icons

Phosphor, fill weight. Built-in glyphs (`app-format.md`) at sizes 12 to 40; colour `label` or `label_2`, `accent` when it marks the active thing. Extra artwork as A8 `image` assets drawn in the same Phosphor fill style. The tile icon (`icon.png`) is a white-on-transparent glyph; the board tints it white on the tile colour.

## Layout patterns

- **Value card:** `label` (font `label`, `label_2`) above a bound value (`title_l`), then a `bar` (`h: 6`, full width) when it is a level.
- **Card row:** two or three cards in a `row` box with `grow: 1` each; `portrait: { "flow": "column" }` on the row and `portrait: { "w": "100%" }` on the cards.
- **Control row:** a `row` box with `cross: center`: a small `icon` (`label_2`), a `slider` with `grow: 1`, a `switch`.
- **Action:** one full-width capsule button at the bottom; at most one primary button per screen.
- Show the most important value first and biggest. Anything secondary goes below the fold (the area scrolls).

## Motion

The board animates opening, closing and value changes itself. Apps have no motion props: do not try to fake animation with rapid `set` calls.

## PC side

The PC app draws the app's tile and settings page itself from `manifest.json` (name, description, tile colour, Phosphor `icon`, settings). There is no custom PC UI.
