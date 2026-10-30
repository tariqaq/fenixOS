# AGENTS.md

This repo is one **fenixOS app**: a screen on the ESP32-2432S028R board (the "CYD") plus an optional Windows helper. The fenixOS PC app installs it from the registry in `tariqaq/fenixOS`. Rules for any AI agent working here.

## Read first, in this order

1. `docs/fenixos/app-format.md`: what the board half can be (`app.toml`, `layout.json`, limits).
2. `docs/fenixos/design.md`: the design language. It is **locked**: use its tokens, fonts, icons and patterns only. No new colours, fonts, shadows or gradients.
3. `docs/fenixos/pc-half.md`: the Windows helper (`fenix-sdk`), only if the app needs data from the PC.
4. `docs/fenixos/registry.md`: the manifest and how the app gets listed.

These four files are copies from fenixOS (version in `README.md`). Where they disagree with the fenixOS repo, fenixOS wins: re-copy, do not patch.

## What an app is

- **Board half** (required): a declarative screen compiled into a `.fxp` package. No code runs on the board; values come from bindings and touches go out as events.
- **PC half** (optional): a Windows `.exe` that fenixOS starts while the app is switched on. It sets bound values and gets events and settings.
- **Settings** (optional): declared in `manifest.json`; fenixOS renders them on the app's page in the PC app. Never build a settings UI yourself.

## Hard rules

1. The id is `io.github.tariqaq.<name>` in `manifest.json` and `app/board/app.toml`, and they match. Same for `version`. Bump `version` in both for every release.
2. Downloads must be GitHub release assets under `https://github.com/tariqaq/`; the registry rejects anything else. So the app repo lives under `tariqaq`.
3. Stay inside the limits in `app-format.md` (64 KB package, 200 elements, 24 KB images, 40 KB RAM estimate, depth 8). `fenix-pkg build` checks them; fix the layout, never work around a check.
4. Design: dark only, `bg` black screen, `surface_1` cards with radius 12, text in `label` / `label_2`, one accent. Touch targets at least 40 px tall. Lay out for **both** landscape (320 × 208 under the nav bar) and portrait (240 × 288) with `portrait` overrides.
5. Text: sentence case, no letter-spaced capitals, short. The board fonts hold printable ASCII plus `° · × – — ’ • … ‹ ›` only: no emoji, no other symbols (they show as blanks). Clock fonts hold digits, `:` `-` and space.
6. PC half: exit on `stop` or a closed pipe (`fenix-sdk` does). Ask only for the permissions it uses; `elevated: true` only if it truly needs admin. No hooks, injection, process scanning or input capture: users run games with anti-cheat beside fenixOS.
7. No AI attribution or `Co-Authored-By` lines in commits, PRs or files.
8. Keep `README.md` and the manifest description true to what the app does.

## Build and test loop

```powershell
pwsh tools/build.ps1                 # dist/app.fxp (+ dist/app.exe if app/pc exists)
python tools/app.py check            # manifest rules and app.toml match
python tools/app.py local            # dist/registry: a local registry for testing
```

Then quit fenixOS (tray > Quit), start it with `FENIX_REGISTRY` set to the printed folder, open Apps > Add app, add the app and switch it on. Check it on the board in both rotations (board Settings > Display > Rotation). `fenix-pkg inspect dist/app.fxp` shows what the package holds.

## Release

1. Bump `version` in `manifest.json` and `app/board/app.toml`, commit, tag `v<version>`, push the tag.
2. `.github/workflows/release.yml` builds `app.fxp` (and `app.exe`), attaches them to the GitHub release, and uploads the finished registry manifest as the `registry-manifest` artifact (`python tools/app.py release v<version>` does the same locally).
3. Open a PR on `tariqaq/fenixOS` adding that file as `registry/apps/<id>.json`, then run `python tools/registry.py` there to refresh `registry/index.json`. Once merged, every fenixOS PC app lists it.
