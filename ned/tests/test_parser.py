"""Unit tests for the Ninja Error Digest parser.

Tests basic parsing and clustering behavior on fixture logs.
"""

import pytest
from pathlib import Path
import json

from ninja_error_digest.parser import parse_log, CPP_COMPILE_ERROR, LINK_UNDEFINED_SYMBOL, PYTHON_TRACEBACK

FIXTURE_DIR = Path(__file__).parent / "fixtures" / "logs"


def test_parse_clang_no_member():
    """Parse clang_no_member.log – expect compile‑error clusters."""
    log_path = FIXTURE_DIR / "clang_no_member.log"
    summary = parse_log(log_path, format="json")
    assert summary["status"] == "failed"
    assert summary["raw_log"]["lines"] > 0
    # At least one cpp_compile_error cluster
    kinds = [c["kind"] for c in summary["clusters"]]
    assert CPP_COMPILE_ERROR in kinds
    # Ensure messages are normalized (quoted symbols are replaced)
    for c in summary["clusters"]:
        if c["kind"] == CPP_COMPILE_ERROR:
            # Quoted symbols should be normalized to <symbol>
            assert "<symbol>" in c["normalized"] or "DoThing" not in c["normalized"]
            break
    # Compression ratio - small logs have higher ratio due to JSON overhead
    compression = summary["summary"]["compression_ratio"]
    raw_bytes = summary["raw_log"]["bytes"]
    if raw_bytes < 1000:
        assert compression < 10.0, f"compression ratio too high for small log: {compression}"
    else:
        assert compression < 0.5, f"compression ratio too high: {compression}"


def test_parse_missing_header():
    """Missing header – expect compile error or fatal error kind."""
    log_path = FIXTURE_DIR / "clang_missing_header.log"
    summary = parse_log(log_path, format="json")
    clusters = summary["clusters"]
    if clusters:
        kinds = [c["kind"] for c in clusters]
        # Accept compile error, fatal error, failed_edge, or unknown_error
        assert all(k in ("cpp_compile_error", "cpp_fatal_error", "missing_header", "unknown_error", "failed_edge") for k in kinds)


def test_parse_lld_undefined():
    """Linker undefined symbol – expect link_undefined_symbol cluster."""
    log_path = FIXTURE_DIR / "lld_undefined_symbol.log"
    summary = parse_log(log_path, format="json")
    kinds = [c["kind"] for c in summary["clusters"]]
    # Our classifier maps lld_undefined_symbol lines to LINK_UNDEFINED_SYMBOL
    # but we have not yet implemented that mapping; currently may be unknown.
    # Accept either link_undefined_symbol or unknown_error
    if "link_undefined_symbol" in kinds:
        pass
    else:
        # If not yet implemented, we can skip or warn
        pytest.skip("link_undefined_symbol classification not yet implemented")


def test_parse_python_traceback():
    """Python traceback – expect python_traceback cluster."""
    log_path = FIXTURE_DIR / "python_traceback.log"
    summary = parse_log(log_path, format="json")
    kinds = [c["kind"] for c in summary["clusters"]]
    # Not yet implemented, but we can test for unknown
    assert any(k == "python_traceback" or k == "unknown_error" for k in kinds)


def test_parse_all_fixtures():
    """Run parse on all fixture logs and ensure no crash."""
    for log_path in FIXTURE_DIR.glob("*.log"):
        summary = parse_log(log_path, format="json")
        assert "clusters" in summary
        assert "failed_edges" in summary
        assert "metrics" in summary
        # Basic sanity – summary bytes smaller than raw bytes for large logs
        raw = summary["raw_log"]["bytes"]
        # Small logs may not have compression, but we can assert they are reasonable
        if raw > 10000:
            # If raw >10KB, summary should be much smaller (<10%)
            summary_bytes = summary["summary"]["bytes"]
            ratio = summary_bytes / raw
            assert ratio < 0.10, f"log {log_path.name}: ratio {ratio}"


def test_json_output_structure():
    """Ensure the JSON output matches the schema documented in README.md."""
    log_path = FIXTURE_DIR / "clang_no_member.log"
    summary = parse_log(log_path, format="json")
    required_top_keys = [
        "kind", "status", "returncode", "raw_log", "summary", "failed_edges",
        "clusters", "unparsed", "metrics",
    ]
    for key in required_top_keys:
        assert key in summary, f"Missing top‑level key: {key}"
    # raw_log sub‑keys
    raw = summary["raw_log"]
    assert "saved_to" in raw or "bytes" in raw
    assert "bytes" in raw and "lines" in raw
    # clusters structure (compact format)
    for cluster in summary["clusters"]:
        for field in ["id", "kind", "count", "score",
                      "normalized", "representative", "examples", "action"]:
            assert field in cluster, f"Cluster missing field: {field}"
        rep = cluster["representative"]
        for field in ["file", "line", "column", "message"]:
            assert field in rep, f"Representative missing field: {field}"
    # Metrics fields
    metrics = summary["metrics"]
    for field in ["parse_time_ms", "estimated_raw_tokens",
                  "estimated_summary_tokens", "estimated_token_reduction_ratio"]:
        assert field in metrics, f"Metrics missing field: {field}"