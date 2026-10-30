# Manifest and registry

Copied from fenixOS `docs/registry.md`, cut for app authors. The fenixOS PC app lists apps from `registry/index.json` in `tariqaq/fenixOS`; each app's files stay in its own repo's GitHub releases.

## `manifest.json`

```json
{
  "id": "io.github.tariqaq.myapp",
  "name": "My app",
  "description": "One line: what it shows on the board",
  "version": "0.1.0",
  "min_fenixos": "0.1.0",
  "color": "teal",
  "icon": "puzzle-piece",
  "repo": "https://github.com/tariqaq/myapp",
  "board": {},
  "pc": { "elevated": false },
  "permissions": ["system-stats"],
  "settings": []
}
```

`board` and `pc` stay empty in the repo: `tools/app.py release` and `local` fill in `url`, `sha256` and `size`.

| Field | Rule |
|---|---|
| `id` | `io.github.<owner>.<name>`; letters, digits, `.` `-` `_`; ≤ 48 bytes. Equals `app/board/app.toml` |
| `name` | Tile label, ≤ 24 bytes |
| `description` | One line, ≤ 100 bytes (Add app list, app page header) |
| `version`, `min_fenixos` | `x.y.z`; `version` equals `app.toml` |
| `color` | Tile token (`gray` `indigo` `blue` `teal` `green` `orange` `red` `purple` `pink`) or `#RRGGBB`. Use a token |
| `icon` | A Phosphor icon name (fill weight) for the PC tile, e.g. `cpu`, `music-notes`. The board tile uses `app/board/icon.png` |
| `repo` | `https://github.com/tariqaq/<repo>`, shown as Source |
| `board` | Required: `url`, `sha256` (lowercase hex), `size` (≤ 64 KB) |
| `pc` | Optional: `url`, `sha256`, `size`, `elevated` |
| `permissions` | From `keys`, `media`, `system-stats`, `network`, `files`, `programs`; unique |
| `settings` | Options fenixOS renders on the app's page (below) |

**Allowed sources.** Download URLs must start with `https://github.com/tariqaq/`. On install the PC app checks size and sha256, then that the package's id and version match the manifest; any mismatch stops the install.

The PC app adds two permission rows itself: "Runs its own helper program while switched on" (there is a `pc` part) and "Asks for administrator rights each time it starts" (`pc.elevated`).

## Settings

Each entry: `key` (letters, digits, `_` `-`, ≤ 32, unique), `type`, `label` (≤ 40 chars) and, except `button`, a `default`. Values reach the PC half as `settings`.

| `type` | Extra fields | Value |
|---|---|---|
| `toggle` | | `true` / `false` |
| `slider` | `min`, `max`, `step`, `unit` (optional) | number |
| `choice` | `options`: `[{ "value": "1", "label": "1 s" }]`; `default` is one of the values | string |
| `text` | `placeholder` (optional), `max_len` (default 100) | string |
| `button` | | none: the PC half gets `button {key}` |

The board half never sees settings; the PC half applies them (for example by changing what it `set`s).

## Testing with a local registry

`python tools/app.py local` writes `dist/registry/` (`index.json` and the files). Quit fenixOS, set `FENIX_REGISTRY` to that folder, start fenixOS: Add app now lists only this app, from local files.

```powershell
$env:FENIX_REGISTRY = "$PWD\dist\registry"; & "$env:LOCALAPPDATA\fenixOS\fenixos.exe"
```

## Listing the app

1. Tag a release; the workflow attaches `app.fxp` (and `app.exe`) and produces the filled manifest (`<id>.json`).
2. PR on `tariqaq/fenixOS`: add it as `registry/apps/<id>.json` (file name = id), run `python tools/registry.py` to regenerate `registry/index.json`, commit both. CI there runs `python tools/registry.py --check`.
3. After merge, every fenixOS PC app lists it under Add app. A new version is a new release plus a PR updating the same file.
