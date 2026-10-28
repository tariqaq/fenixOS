# fenixOS tooling

Tools set up for fenixOS (the FenixDeck PoC toolchain is in `docs/toolchain.md`, kept for reference).

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
| Locked system | Hallmark obeys a `DESIGN.md` at the repo root when present (it overrides its themes). fenixOS's Apple-dark system will live there once decided |

Reviewed before use (the installer warns that skills run with full agent permissions): its scripts only use local Chrome DevTools and, if `TOGETHER_API_KEY` is set, Together AI's image API (`scripts/imagegen.mjs`). That key is not set here.

### Gotcha: line endings

The global git setting `core.autocrlf=true` made the installer write CRLF files. Hallmark's `refcheck.mjs` then failed: `SKILL.md needs name and description in its frontmatter`, because its frontmatter regex expects LF. Fix: convert the skill's text files to LF, and add `.gitattributes` with `* text=auto eol=lf`, so the repo keeps LF on every checkout.

## Board access

- `esptool` 5.6.0 (system Python 3.14), board on `COM9`.
- **460800 baud failed once on a full 4 MB read** (`Corrupt data, expected 0x1000 bytes but received 0xe49 bytes`, 2026-10-29, right after `erase-flash`). A retry at 230400 worked (190 s). Earlier reads at 460800 worked, and 921600 failed on 2026-10-28. For long reads, retry at 230400 if 460800 fails.
