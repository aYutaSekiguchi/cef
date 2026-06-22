"""Cluster‑related unit tests for the Ninja Error Digest tool."""

from ninja_error_digest.parser import build_clusters, normalize_error_message
from ninja_error_digest.utils import classify_error_line


def test_normalize_error_message():
    # Example normalization
    msg = "no member named 'DoThing' in 'FooService'"
    norm = normalize_error_message(msg)
    # Both quoted symbols should be normalized to <symbol>
    assert norm == "no member named <symbol> in <symbol>"
    # Additional check – numbers removed
    msg2 = "error at line 123, column 45"
    norm2 = normalize_error_message(msg2)
    assert "<num>" in norm2


def test_normalize_paths():
    msg = "cannot open /usr/local/include/foo.h"
    norm = normalize_error_message(msg)
    assert "<path>" in norm


def test_cluster_basic():
    # Create synthetic classified errors that should cluster together
    classified = [
        {"kind": "cpp_compile_error", "metadata": {"file": "a.cc", "line": "1", "message": "error: no member named 'X' in 'Y'"}},
        {"kind": "cpp_compile_error", "metadata": {"file": "b.cc", "line": "2", "message": "error: no member named 'Z' in 'W'"}},
    ]
    raw_lines = [
        "a.cc:1: error: no member named 'X' in 'Y'",
        "b.cc:2: error: no member named 'Z' in 'W'",
    ]
    clusters = build_clusters(classified, raw_lines)
    # Expect one cluster because messages normalize to same pattern
    assert len(clusters) == 1
    assert clusters[0]["kind"] == "cpp_compile_error"
    assert clusters[0]["count"] == 2
    assert clusters[0]["representative"]["file"] == "a.cc"


def test_cluster_different_kinds():
    classified = [
        {"kind": "cpp_compile_error", "metadata": {"file": "a.cc", "message": "error: missing header"}},
        {"kind": "link_undefined_symbol", "metadata": {"symbol": "foo"}},
    ]
    raw_lines = [
        "a.cc:1: error: missing header",
        "undefined reference to 'foo'",
    ]
    clusters = build_clusters(classified, raw_lines)
    # Should produce two clusters, one per kind
    assert len(clusters) == 2
    kinds = {c["kind"] for c in clusters}
    assert "cpp_compile_error" in kinds
    assert "link_undefined_symbol" in kinds