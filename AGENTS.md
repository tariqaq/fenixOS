# AGENTS.md

Rules for any AI agent working in this repository. These are hard rules: no exceptions.

## Hard rules

### 1. Never add co-authoring
- Never add `Co-Authored-By:` (or any similar attribution trailer) to commit messages.
- Never add "Generated with ..." or other AI attribution to commits, PRs, or files.
- If a commit with co-authoring is found, rewrite history to remove it and force-push.

### 2. Commit, push, sync frequently on the default branch
- Work directly on the default branch (`main`). Do not create feature branches.
- Commit small, logical units of work often.
- After every commit: `git pull --rebase` then `git push`, so local and `origin/main` stay in sync.

### 3. Plan first, track progress in `tasks.md`
- Before working on anything, write a well-scoped, detailed plan.
- Put the plan in `tasks.md` as a checklist before starting execution.
- Update `tasks.md` immediately after every step: mark it done, in progress, or blocked, with notes.
- Keep `tasks.md` accurate at all times, so a cut-off session can resume from it without guessing.
- Commit and push `tasks.md` updates along with the work (rule 2).

### 4. Balance own knowledge, web search, and browser
- Use your own knowledge first when it is enough and you are confident.
- Use web search freely when facts are niche, recent, uncertain, or need checking.
- Verify web info: cross-check against more than one reliable source before relying on it.
- Use Claude in Chrome browser control freely when a page needs to be viewed or interacted with.
- Balance them: do not search for everything, and do not rely only on memory.

### 5. Always document important findings
- Write down every important finding as soon as it is found: hardware facts, pin maps, versions, errors and their fixes, gotchas, corrections to earlier claims.
- Put findings in the matching doc (for example `docs/board-info.md`, `pics/ANALYSIS.md`, `docs/vendor/README.md`), or create a new doc under `docs/`.
- When a finding corrects an earlier one, update the old text and note the correction. Do not leave wrong info in place.
- Commit and push the docs with the work (rule 2).

### 6. Regularly clean up stale content
- After every change, update every doc and code comment the change affects, in the same commit.
- At the end of each task or phase, sweep for stale content: docs (`*.md`), code comments, CLI/help text, `tasks.md`, screenshots referenced by docs.
- Stale means: describes old behaviour, wrong numbers or versions, removed features, resolved open questions, or "TODO/pending" items that are done.
- Fix it in place: correct or delete it. Where a correction matters for history, note it ("Corrected: ...").
- Remove dead code and unused debug leftovers found during the sweep.

## Resuming a session
1. Read `tasks.md`.
2. Find the first item that is not done.
3. Check `git log` and `git status` to confirm the real state.
4. Continue from there.
