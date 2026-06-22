#!/usr/bin/env bash
# Typical AI workflow: ned → jq → feed to AI
#
# This script demonstrates a typical build error analysis flow.
# The AI agent does not read the raw log; it only receives the
# necessary information extracted by jq.

set -e

# Path to the ned command
NED="${NED:-ned}"
if ! command -v "$NED" >/dev/null 2>&1; then
    NED=".venv/bin/ned"
fi

LOG_FILE="${1:-build.log}"

if [ ! -f "$LOG_FILE" ]; then
    echo "Usage: $0 <log_file>" >&2
    exit 1
fi

echo "=== Step 1: Parse the log (heavy work, saved to a file) ==="
"$NED" parse "$LOG_FILE" --format json > /tmp/cef_summary.json

echo "=== Step 2: Check status (a few tokens) ==="
jq -r 'if .status == "failed" then "❌ Build failed" else "✅ Build succeeded" end' /tmp/cef_summary.json

echo ""
echo "=== Step 3: Number of failed edges (a few tokens) ==="
jq '.failed_edges.count' /tmp/cef_summary.json | xargs -I {} echo "Failed edges: {}"

echo ""
echo "=== Step 4: Top 3 cluster summary (~100 tokens) ==="
jq '.clusters[0:3] | .[] | "  [\(.id)] \(.kind) ×\(.count) — \(.representative.file):\(.representative.line)"' /tmp/cef_summary.json

echo ""
echo "=== Step 5: Detail of the most important cluster (~200 tokens) ==="
TOP_ID=$(jq -r '.clusters[0].id' /tmp/cef_summary.json)
echo "Investigating $TOP_ID:"
jq ".clusters[] | select(.id==\"$TOP_ID\")" /tmp/cef_summary.json

echo ""
echo "=== Step 6: AI-facing action (1 line) ==="
jq -r '.clusters[0].action' /tmp/cef_summary.json

# Note: A real AI does the following:
# 1. Does not read the full Step 1 file (too large)
# 2. Reads only the Step 2-3 results to understand the situation (a few tokens)
# 3. Looks at the top errors in Step 4 (~100 tokens)
# 4. Decides on a fix
# 5. Only reads Step 5 details when necessary (~200 tokens)
# 6. After the fix, re-runs parse and repeats the flow