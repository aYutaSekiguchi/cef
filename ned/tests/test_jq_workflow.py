"""Test that the jq-based token reduction works correctly.

The key insight: AI agents don't read the full JSON output.
They use jq to extract just what they need, achieving significant
token reduction even on small logs.
"""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from ninja_error_digest.parser import parse_log

FIXTURE_DIR = Path(__file__).parent / "fixtures" / "logs"


def estimate_tokens(text: str) -> int:
    """Rough token estimation: 1 token ≈ 4 characters."""
    return max(1, len(text) // 4)


def has_jq() -> bool:
    """Check if jq is available."""
    return shutil.which("jq") is not None


@pytest.mark.skipif(not has_jq(), reason="jq not installed")
def test_jq_top_clusters_reduces_tokens():
    """Top 3 clusters via jq should use far fewer tokens than raw log."""
    log_path = FIXTURE_DIR / "realistic_compile_failure.log"
    summary = parse_log(log_path, format="json")
    summary_file = log_path.with_suffix(".json")
    summary_file.write_text(json.dumps(summary, indent=2, default=str))

    raw_text = log_path.read_text()
    raw_tokens = estimate_tokens(raw_text)

    # AI uses jq to extract top 3 clusters with key fields only
    result = subprocess.run(
        ["jq", "-c", '[.clusters[0:3] | .[] | {id, kind, count, action}]', str(summary_file)],
        capture_output=True, text=True, check=True
    )
    extracted_tokens = estimate_tokens(result.stdout)

    # Should achieve at least 70% token reduction
    reduction = 1 - (extracted_tokens / raw_tokens)
    assert reduction > 0.70, f"Token reduction only {reduction:.1%}, expected >70%"


@pytest.mark.skipif(not has_jq(), reason="jq not installed")
def test_jq_status_check_minimal_tokens():
    """Just checking status should use very few tokens."""
    log_path = FIXTURE_DIR / "realistic_compile_failure.log"
    summary = parse_log(log_path, format="json")
    summary_file = log_path.with_suffix(".json")
    summary_file.write_text(json.dumps(summary, indent=2, default=str))

    # Just get status and failed edges count
    result = subprocess.run(
        ["jq", "-c", '{status, failed: .failed_edges.count}', str(summary_file)],
        capture_output=True, text=True, check=True
    )
    tokens = estimate_tokens(result.stdout)

    # Should be very small (<20 tokens)
    assert tokens < 20, f"Status check uses {tokens} tokens, expected <20"


@pytest.mark.skipif(not has_jq(), reason="jq not installed")
def test_jq_single_cluster_drilldown():
    """Drilling into a single cluster should be moderate size."""
    log_path = FIXTURE_DIR / "realistic_compile_failure.log"
    summary = parse_log(log_path, format="json")
    summary_file = log_path.with_suffix(".json")
    summary_file.write_text(json.dumps(summary, indent=2, default=str))

    # Get just the top cluster
    result = subprocess.run(
        ["jq", "-c", ".clusters[0]", str(summary_file)],
        capture_output=True, text=True, check=True
    )
    tokens = estimate_tokens(result.stdout)

    # Single cluster should be < 200 tokens
    assert tokens < 200, f"Single cluster uses {tokens} tokens, expected <200"


@pytest.mark.skipif(not has_jq(), reason="jq not installed")
def test_full_json_vs_jq_reduction():
    """Compare full JSON size vs jq-extracted size."""
    log_path = FIXTURE_DIR / "realistic_compile_failure.log"
    summary = parse_log(log_path, format="json")
    summary_file = log_path.with_suffix(".json")
    summary_file.write_text(json.dumps(summary, indent=2, default=str))

    raw_text = log_path.read_text()
    raw_tokens = estimate_tokens(raw_text)

    # Full JSON (if AI read everything)
    full_json_tokens = estimate_tokens(json.dumps(summary))

    # jq top 5 minimal
    result = subprocess.run(
        ["jq", "-c", '[.clusters[0:5] | .[] | {id, kind, count}]', str(summary_file)],
        capture_output=True, text=True, check=True
    )
    jq_tokens = estimate_tokens(result.stdout)

    # The point: full JSON may be larger, but jq-extracted is always much smaller
    assert jq_tokens < full_json_tokens, "jq extraction should always be smaller"
    assert jq_tokens < raw_tokens / 2, f"jq ({jq_tokens}) should be < 50% of raw ({raw_tokens})"
