# Legacy: FenixDeck PoC docs

Except `tasks-history.md`, these docs describe the FenixDeck proof of concept (PC hardware monitor), which ended on 2026-10-28. The code is in `C:\Users\trqboi\Documents\fenixdeck-legacy` (GitHub `tariqaq/fenixdeck`). References in these files to `tasks.md`, the README, crates and board paths point to that repo, not to fenixOS.

| File | What it is |
|---|---|
| `PLAN.md` | The FenixDeck v1 plan, as built |
| `protocol.md` | FenixDeck wire protocol v1. fenixOS keeps its framing (COBS + CRC-16) and defines its own messages in `proto/` |
| `metrics-validation.md` | Accuracy check of the PC metrics collectors |
| `screens/` | LCD read-back screenshots of the PoC UI |
| `tasks-history.md` | fenixOS: done phases 0 to P40 from `tasks.md`, verbatim |

What the PoC taught is summarised in `docs/POC-WRITEUP.md`. The fenixOS plan is `docs/PLAN.md`.
