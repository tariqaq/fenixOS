# AGENTS.md

Hard rules for any AI agent in this repo. No exceptions.

## Hard rules

### 1. Never add co-authoring
- No `Co-Authored-By:` or similar trailer in commits.
- No "Generated with ..." or other AI attribution in commits, PRs or files.
- If a commit with co-authoring is found, rewrite history to remove it and force-push.

### 2. Commit, push, sync often on `main`
- Work on `main`. No feature branches.
- Commit small, logical units often.
- After every commit: `git pull --rebase` then `git push`, so local and `origin/main` match.

### 3. Plan first, track in `tasks.md`
- Before any work, write a scoped, detailed plan as a checklist in `tasks.md`.
- After every step, mark it done, in progress or blocked, with notes.
- Keep it accurate so a cut-off session can resume without guessing.
- Commit and push it with the work (rule 2).

### 4. Balance own knowledge, web search, browser
- Own knowledge first when it is enough and you are sure.
- Web search when facts are niche, recent or uncertain; cross-check with more than one reliable source.
- Claude in Chrome when a page must be viewed or used.
- Do not search for everything; do not rely only on memory.

### 5. Document every important finding
- Write it down when found: hardware facts, pin maps, versions, errors and fixes, gotchas, corrections.
- Put it in the matching doc (`docs/board-info.md`, `pics/ANALYSIS.md`, `docs/vendor/README.md`, ...) or a new one under `docs/`.
- When a finding corrects an older one, fix the old text and note it ("Corrected: ..."). Never leave wrong info.
- Commit and push docs with the work (rule 2).

### 6. Clean up stale content
- Every change updates every doc and code comment it affects, in the same commit.
- At the end of each task or phase, sweep `*.md`, code comments, CLI/help text, `tasks.md`, screenshots used by docs.
- Stale: old behaviour, wrong numbers or versions, removed features, resolved questions, done TODOs.
- Fix in place; note "Corrected: ..." where history matters. Remove dead code and debug leftovers found.

### 7. Docs and code disagree: code wins
- Follow the code for conventions: naming, structure, idioms, numbers.
- List each mismatch found in your final reply, and fix the stale doc in the same changes.
- Two exceptions, where the doc wins: these hard rules (never loosened to match code), and sections marked planned (ahead of the code on purpose).

### 8. Read first, edit, test once
- Before changing anything, list every file the change needs and read them in one pass.
- Then make all the edits. Then run the tests once (see Testing).
- No read, edit, read, edit loop.

### 9. Searches stay inside this repo
- Never grep, find or list from `C:\`, `%USERPROFILE%` or any other drive-wide root.
- If a search outside the repo is needed, ask the user first.

## Testing

What CI runs; run the parts your change touches:
- `python tools/version.py --check`, `tools/gen_tokens.py --check`, `tools/registry.py --check`, `tools/licenses.py --check`
- `cargo test --workspace`
- `pnpm -C app exec svelte-check`, `pnpm -C app build`
- Firmware: `pwsh -NoProfile -File board/idf.ps1 build`; simulator screens: `board/sim/build.ps1`, then `python tools/sim_shots.py`
- On this laptop, wrap builds in `pwsh -NoProfile -File tools/capped.ps1 <command>` (4-core cap, normal priority), one build at a time. All cores only when the user asks, for that build only.

## Resuming a session
1. Read `tasks.md`.
2. Find the first item not done.
3. Check `git log` and `git status` for the real state.
4. Continue from there.
