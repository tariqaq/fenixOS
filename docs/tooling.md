# fenixOS tooling

Agent tooling for fenixOS. Build tools, versions and commands: `docs/toolchain.md`.

## Design: hallmark

| Item | Value |
|---|---|
| What | Hallmark 2.0.0 (`nutlope/hallmark`, MIT): a design skill with anti-"AI-generated" gates and a browser lint |
| Installed | 2026-10-29, project level: `npx skills add nutlope/hallmark --agent claude-code --skill '*' --copy -y` |
| Location | `.claude/skills/hallmark/` (copied, tracked in git); `skills-lock.json` pins the source hash |
| Browser checks | `puppeteer-core` 25.16.0 installed globally (`npm i -g puppeteer-core`); uses Chrome at `C:\Program Files\Google\Chrome\Application\chrome.exe` |
| Edit-time hook | `node .claude/skills/hallmark/scripts/install-hook.mjs` added a `PostToolUse` hook (`Write|Edit|MultiEdit`) to `.claude/settings.json`. It lints Hallmark HTML/CSS (and Svelte etc. inside a project with a `.hallmark` folder) on write; it advises, never blocks. Remove with `--remove`. The hook path is absolute (this PC) |
| Self-check | `node .claude/skills/hallmark/scripts/refcheck.mjs` → clean across 101 reference files |
| Lint a page | `node .claude/skills/hallmark/scripts/sloplint.mjs <file-or-dir> --render` |
| Locked system | Hallmark obeys a `DESIGN.md` at the repo root when present (it overrides its themes). fenixOS's Apple-dark system is locked there |

Reviewed before use (the installer warns that skills run with full agent permissions): its scripts only use local Chrome DevTools and, if `TOGETHER_API_KEY` is set, Together AI's image API (`scripts/imagegen.mjs`). That key is not set here.

### Gotcha: line endings

The global git setting `core.autocrlf=true` made the installer write CRLF files. Hallmark's `refcheck.mjs` then failed: `SKILL.md needs name and description in its frontmatter`, because its frontmatter regex expects LF. Fix: convert the skill's text files to LF, and add `.gitattributes` with `* text=auto eol=lf`, so the repo keeps LF on every checkout.

## Board access

- Board on `COM9`. Flash reads, writes and erases: `fenix read-flash` / `write` / `flash` / `erase` (corrected 2026-10-30: was `esptool` 5.6.0, whose read timeouts take the CH340 driver's crash path; `docs/board-info.md`).
- **460800 baud failed once on a full 4 MB read** (`Corrupt data, expected 0x1000 bytes but received 0xe49 bytes`, 2026-10-29, right after `erase-flash`). A retry at 230400 worked (190 s). Earlier reads at 460800 worked, and 921600 failed on 2026-10-28. For long reads, retry at 230400 if 460800 fails (esptool; `fenix read-flash` reads at 460800).

## Gotchas found during the build (2026-10-29)

- **Claude Code's Bash tool collapses a doubled backslash to a single one inside heredocs.** Files with escaped backslashes (C/Python strings like `"\x"`, regex `\(`) must be written with the Write/Edit tools, not `cat <<EOF`.
- **Spike build folders must stay ignored** (`/board/spikes/*/build/` etc. in `.gitignore`). One spike build tree was committed by mistake in `657b98c`-era history and removed in the next commit; it stays in history (force-push is not used).
- **Python on this PC writes cp1252 by default** (2026-10-29). `open(p).read()` / `open(p, 'w')` without `encoding="utf-8"` mangles or fails on `→ ‖ ≥` and can leave a file truncated when the write raises. Always pass `encoding="utf-8"` (or `pathlib.Path.read_text(encoding=...)`).
- **Do not keep `.py` files named like stdlib modules in the scratchpad** (an `inspect.py` there broke `import PIL` for scripts run from that folder).
