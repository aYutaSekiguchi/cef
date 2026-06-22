# ned — Ninja Error Digest

A log transformation tool that parses Ninja build errors and lets AI agents
identify likely root causes with minimal token consumption.

`ned` lives inside the CEF source tree as a self-contained sub-project. It is
not part of upstream CEF — it is a QNX-port convenience tool that compresses
the (often huge) Ninja build logs that the CEF / Chromium build produces, so
that an AI assistant can read a few hundred tokens instead of tens of
megabytes.

## Quick start

From the **repository root**:

```bash
uv venv .venv
uv pip install -e ned/
.venv/bin/ned parse build.log --format json | jq '.clusters[0:5]'
```

That is the whole pipeline: build → `tee build.log` → `ned parse` → `jq` to
pick the cluster you want to investigate.

## Why this lives in the CEF tree

The CEF build (Chromium + CEF + QNX patches) is large. When `ninja -k 20` is
run on `out/qnx_release`, the resulting log is routinely 10–100 MB of
compiler invocations, file paths, and per-target build commands. None of
that helps an AI diagnose the failure — the useful signal is the few hundred
compiler/linker error lines buried inside.

`ned` extracts those error lines, normalizes them (so `"no member named
'DoThing' in 'FooService'"` and `"no member named 'RunTask' in
'FooService'"` cluster together), ranks them by severity × count, and
emits a small JSON summary that `jq` can slice cluster-by-cluster.

## Layout

```
ned/
├── README.md                           ← this file
├── WORKFLOW.md                         ← capture / parse / analyze recipes
├── pyproject.toml                      ← Python build config (package root)
├── ninja_error_digest/                 ← Python package
│   ├── __init__.py
│   ├── cli.py                          (argparse entry points)
│   ├── parser.py                       (Ninja log → clusters)
│   ├── utils.py                        (classification, normalization, redaction)
│   ├── reporter.py                     (JSON → Markdown)
│   ├── benchmark.py                    (parse-time / compression metrics)
│   ├── README.md                       ← full usage reference
│   ├── SETUP.md                        ← venv / install details
│   └── examples/
│       └── jq_workflow.sh              ← end-to-end capture → parse → jq script
└── tests/
    ├── test_parser.py
    ├── test_cluster.py
    ├── test_jq_workflow.py
    └── fixtures/logs/                  (12 .log files + 1 .json)
```

## Documentation map

| Want to… | Read |
|----------|------|
| Get `ned` installed in 30 seconds | [ninja_error_digest/SETUP.md](ninja_error_digest/SETUP.md) |
| See the capture / parse / analyze workflow | [WORKFLOW.md](WORKFLOW.md) |
| See a worked end-to-end example (runnable) | [ninja_error_digest/examples/jq_workflow.sh](ninja_error_digest/examples/jq_workflow.sh) |
| See all `ned` subcommands and JSON schema | [ninja_error_digest/README.md](ninja_error_digest/README.md) |
| Understand the project-level rules for using `ned` | `../AGENTS.md` — section **"Ninja result capture"** |
| Run the test suite | [tests/](tests/) |

## Tests

```bash
.venv/bin/pytest ned/tests/ -v
```

14 tests cover parser, clustering, normalization, and the `jq` workflow.

## Do not commit

The `ned/` source tree is committed, but these are local-only:

- `.venv/` — local Python environment (at the repo root)
- `ned/ninja_error_digest.egg-info/`, `__pycache__/` — build artifacts
- `build.log`, `build.summary.json` — local analysis outputs

See `../AGENTS.md` for the canonical list.

## License

`ned` is provided under the same license as the parent CEF repository.
