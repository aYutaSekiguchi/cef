# Historical Notes

This directory contains older investigation notes, status snapshots, focused reviews, and handoff material collected during the QNX port.

## Most useful references

Start here when the top-level docs are not enough:

- `handoffs/launchprocess-handoff.md` — background on the spawn-based `LaunchProcess` work
- `research/mincore-analysis.md` — memory-discard behavior and `mincore()` investigation
- `research/local-mismatch.md` — environment mismatch analysis when results diverge across machines
- `archive/phase1/summary.md` — early bootstrap and toolchain bring-up summary
- `archive/problems_and_solutions.md` — older problem/solution inventory

## Layout

| Path | Contents |
|---|---|
| `status/` | dated status snapshots, TODO snapshots, and blocker reproductions |
| `handoffs/` | targeted handoff notes for larger implementation areas |
| `analysis/` | broader planning, tradeoff, and problem-analysis documents |
| `research/` | focused platform/API investigations |
| `archive/` | older standalone notes preserved as reference, including `phase1/` and `reviews/` subdirectories |

## How to use this directory

- Start with the top-level docs in `docs/qnx/` for the current workflow.
- Use `history/` only when you need older reasoning, prior experiments, or subsystem-specific context.
- Prefer the CEF-managed patch set and current validation flow over older ad-hoc procedures when they disagree.
