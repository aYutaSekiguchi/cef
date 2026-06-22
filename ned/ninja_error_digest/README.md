# Ninja Error Digest (ned)

A log transformation tool that parses Ninja build errors and lets AI agents identify likely root causes with minimal token consumption.

## Overview

Instead of feeding the raw Ninja build log directly to the AI, this tool extracts errors, normalizes them, and clusters similar issues into a compact summary.

## Installation

The minimum install is:

```bash
# From the repository root
uv venv .venv
uv pip install -e ned/
```

For prerequisites, venv recreation, troubleshooting, and what `.venv/`
contains, see **[SETUP.md](SETUP.md)**.

## Usage

### `ned parse` — Parse a saved log

```bash
ned parse build.log --format json
```

**Options:**

| Option | Description | Default |
|--------|-------------|---------|
| `--format` | Output format (json, yaml, text) | json |

**Output example:**

```json
{
  "kind": "ninja_build_error_summary",
  "status": "failed",
  "clusters": [
    {
      "id": "C001",
      "kind": "cpp_compile_error",
      "count": 12,
      "representative": {
        "file": "foo/bar.cc",
        "line": 123,
        "message": "no member named 'DoThing' in 'FooService'"
      }
    }
  ]
}
```

### `ned build` — Run Ninja and parse (not yet implemented)

```bash
ned build -C out/Default target --keep-going 20
```

### `ned report` — Generate report (not yet implemented)

```bash
ned report .ned/runs/latest --format markdown
```

### `ned bench` — Benchmark (not yet implemented)

```bash
ned bench ned/tests/fixtures/logs
```

## Error classification

Supported error formats:

| Kind | Example |
|------|---------|
| cpp_compile_error | `foo.cc:12:9: error: no member named 'X'` |
| msvc_compile_error | `foo.cc(123,9): error C2039: 'X'` |
| link_undefined_symbol | `undefined reference to 'Foo::Bar()'` |
| link_duplicate_symbol | `duplicate symbol: Foo::Bar()` |
| python_traceback | `Traceback (most recent call last):` |
| script_error | `Error: something failed` |

## Normalization

Message normalization groups similar errors together:

- Paths → `<path>`
- Quoted symbols → `<symbol>`
- Numeric literals → `<num>`
- Whitespace → single spaces

Example:
```
no member named 'DoThing' in 'FooService'
↓
no member named <symbol> in <symbol>
```

## Tests

```bash
.venv/bin/pytest ned/tests/ -v
```

## Performance evaluation

### Token reduction via query-based extraction

The real value of this tool is not the raw JSON size, but the number of tokens the AI actually reads. Using `jq` to extract only what's needed achieves high token reduction.

Example: 3,491-byte CEF build log (872 tokens)

| Query pattern | Output size | Tokens | Reduction |
|---------------|-------------|--------|-----------|
| Raw log (baseline) | 3,491B | 872 | 0% |
| `jq '.[0:3] \| {id, kind, count, action}'` | 502B | 125 | 85.7% |
| `jq '.[0:5] \| {id, kind, count}'` | 252B | 63 | 92.8% |
| `jq '{status, failed_count, top3 messages}'` | 158B | 39 | 95.5% |

### Usage examples

```bash
# View only the top 5 clusters (what the AI sees on first read)
ned parse build.log --format json | jq '.clusters[0:5] | .[] | {id, kind, count, representative}'

# Drill into a single cluster (for deeper investigation)
ned parse build.log --format json | jq '.clusters[] | select(.id=="C001")'

# Just the failed edges
ned parse build.log --format json | jq '.failed_edges.sample'
```

### Characteristics by log size

| Log size | Full JSON read | jq extraction | Use case |
|----------|----------------|---------------|----------|
| >1MB | Large | Major reduction | Maximum efficiency |
| 10KB-1MB | Medium | Major reduction | Standard use |
| <10KB | Inflated by JSON metadata | Major reduction | Raw log may also be fine |

