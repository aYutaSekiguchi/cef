# ned workflow

How to use [`ned`](README.md) together with the CEF / QNX build to diagnose
a build failure without burning the AI's context window.

The companion shell script
[`ninja_error_digest/examples/jq_workflow.sh`](ninja_error_digest/examples/jq_workflow.sh)
runs the same steps automatically. This doc explains the pieces so the agent
(you) can run them by hand or adapt them.

## 1. Capture

Always save ninja output to a file. The CEF / QNX build routinely produces
10–100 MB of log lines, of which only a few hundred are useful for diagnosis.

```bash
# QNX build (preferred — full output, both stdout and stderr)
./out/qnx_release/ninja_qnx.sh base_unittests -k 20 2>&1 | tee build.log

# Or with the underlying ninja binary
ninja -C out/qnx_release base_unittests -k 20 2>&1 | tee build.log
```

## 2. Compress

`ned parse` produces a JSON summary that is typically 0.1–1% of the raw log
size. Never read the raw log first; always compress.

```bash
.venv/bin/ned parse build.log --format json > build.summary.json
```

## 3. Inspect with `jq`

The summary has a stable JSON shape. Slice it cluster-by-cluster so each turn
stays under a few hundred tokens.

```bash
# Status check (a few tokens)
jq '{status, failed: .failed_edges.count}' build.summary.json

# Top 5 clusters (≈100 tokens — what the AI should see on first read)
jq '[.clusters[0:5] | .[] | {id, kind, count, representative, action}]' \
   build.summary.json

# Drill into a single cluster (≈200 tokens)
jq '.clusters[] | select(.id=="C001")' build.summary.json

# Human-readable Markdown report
.venv/bin/ned parse build.log --format json | \
  .venv/bin/ned report .ned/runs/latest --format markdown
```

For the exact JSON schema, see
[`ninja_error_digest/README.md`](ninja_error_digest/README.md).

## 4. End-to-end example

```bash
# Build and capture
./out/qnx_release/ninja_qnx.sh base_unittests -k 20 2>&1 | tee build.log

# Compress
.venv/bin/ned parse build.log --format json > build.summary.json

# Ask the AI to fix only the top cluster (small, focused context)
TOP=$(jq -r '.clusters[0].id' build.summary.json)
jq --arg id "$TOP" '.clusters[] | select(.id==$id)' build.summary.json
```

## 5. Why this matters

Raw ninja logs for this codebase easily reach tens or hundreds of megabytes.
Feeding them to an AI directly burns the context window on:

- compiler invocation lines (`g++ -Iinclude -c foo.cc -o obj/foo.o ...`)
- `ninja: Entering directory` banners
- per-file build commands

None of which help diagnosis. `ned` collapses 99%+ of those bytes into a
structured summary, and `jq` lets the agent pull exactly one cluster at a
time so each turn stays under a few hundred tokens.

## Token budget rules

These rules are also restated in `../../AGENTS.md` (the project contract) and
in [`ninja_error_digest/examples/jq_workflow.sh`](ninja_error_digest/examples/jq_workflow.sh):

- Never read `build.log` directly; use `ned parse` first.
- When investigating, load only **one** cluster per turn (≈200 tokens).
- Re-run `ned parse` after each fix; do not rely on the previous summary.
- If a cluster's representative location is unclear, fetch the raw lines
  from the corresponding `FAILED:` edge in `build.log` — still scoped, not
  the whole file.
