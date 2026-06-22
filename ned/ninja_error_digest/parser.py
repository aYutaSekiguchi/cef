"""Core parsing logic for Ninja build logs.

Simplified parser that extracts FAILED edges, classifies errors, normalizes messages,
clusters similar errors, and produces a summary JSON.
"""

import json
import re
import time
from pathlib import Path
from typing import Dict, List, Any
from collections import defaultdict

from .utils import (
    normalize_error_message,
    classify_error_line,
    redact_secrets,
    estimate_token_count,
)

# Error-kind constants
CPP_COMPILE_ERROR = "cpp_compile_error"
CPP_FATAL_ERROR = "cpp_fatal_error"
MISSING_HEADER = "missing_header"
LINK_UNDEFINED_SYMBOL = "link_undefined_symbol"
LINK_DUPLICATE_SYMBOL = "link_duplicate_symbol"
MSVC_COMPILE_ERROR = "msvc_compile_error"
SCRIPT_ERROR = "script_error"
PYTHON_TRACEBACK = "python_traceback"
FAILED_EDGE = "failed_edge"
UNKNOWN_ERROR = "unknown_error"


def parse_log(file_path: Path, format: str = "json") -> Dict[str, Any]:
    """Parse a Ninja log file and return a compressed summary.

    The returned dict follows the JSON schema documented in README.md.
    """
    start_time = time.time()
    raw_text = file_path.read_text(encoding="utf-8", errors="replace")
    raw_lines = raw_text.splitlines()
    raw_bytes = len(raw_text.encode("utf-8"))
    raw_line_count = len(raw_lines)

    # Step 1: Extract FAILED edges
    failed_edges = extract_failed_edges(raw_lines)

    # Step 2: Classify each line into an error kind
    classified = [classify_error_line(l) for l in raw_lines]

    # Step 3: Build clusters per (kind + normalized_message)
    clusters = build_clusters(classified, raw_lines)

    # Determine status BEFORE building summary dict
    has_errors = any(c["kind"] not in ("unknown_error", "warning", "note") for c in classified)
    status = "failed" if failed_edges or has_errors else "success"
    returncode = 1 if status == "failed" else 0

    # Compute summary bytes for compression ratio
    summary_bytes = sum(len(json.dumps(c, default=str).encode("utf-8")) for c in clusters)
    compression_ratio = summary_bytes / max(1, raw_bytes)

    # Step 4: Build AI-ready summary JSON structure
    summary = {
        "kind": "ninja_build_error_summary",
        "status": status,
        "returncode": returncode,
        "raw_log": {
            "saved_to": None,
            "bytes": raw_bytes,
            "lines": raw_line_count,
        },
        "summary": {
            "bytes": summary_bytes,
            "lines": len(clusters),
            "compression_ratio": compression_ratio,
        },
        "failed_edges": {
            "count": len(failed_edges),
            "sample": failed_edges[:3],
        },
        "clusters": clusters,
        "unparsed": {
            "error_like_lines": count_error_like_lines(raw_lines, classified),
            "sample": [],
        },
        "metrics": {
            "parse_time_ms": int((time.time() - start_time) * 1000),
            "estimated_raw_tokens": estimate_token_count(raw_text),
            "estimated_summary_tokens": 0,  # filled after redaction
            "estimated_token_reduction_ratio": 0.0,
        },
    }

    # Redact secrets in the AI-ready output
    summary = redact_secrets_recursive(summary)

    # Update token estimates after redaction
    summary_json = json.dumps(summary, default=str)
    summary["metrics"]["estimated_summary_tokens"] = estimate_token_count(summary_json)
    summary["metrics"]["estimated_token_reduction_ratio"] = (
        summary["metrics"]["estimated_summary_tokens"] / max(1, summary["metrics"]["estimated_raw_tokens"])
    )

    if format == "json":
        return summary
    else:
        raise NotImplementedError(f"Format '{format}' not yet implemented")


def extract_failed_edges(lines: List[str]) -> List[str]:
    """Extract FAILED edges (lines starting with 'FAILED:')."""
    edges = []
    for line in lines:
        if line.startswith("FAILED:"):
            edge = line[len("FAILED:"):].strip()
            edges.append(edge)
    return edges


def build_clusters(classified: List[Dict[str, Any]], raw_lines: List[str]) -> List[Dict[str, Any]]:
    """Group classified errors into clusters and rank them."""
    buckets = defaultdict(list)
    for idx, (cls, line) in enumerate(zip(classified, raw_lines)):
        kind = cls["kind"]
        message = cls["metadata"].get("message", "") or cls["metadata"].get("raw", "") or line.strip()
        normalized = normalize_error_message(message)
        key = (kind, normalized)
        buckets[key].append((idx, cls, line))

    clusters = []
    for (kind, normalized_msg), items in buckets.items():
        items.sort(key=lambda x: x[0])
        count = len(items)
        representative = None
        examples = []
        for idx, cls, line in items[:5]:
            examples.append({
                "file": cls["metadata"].get("file"),
                "line": int(cls["metadata"].get("line")) if cls["metadata"].get("line") and cls["metadata"]["line"].isdigit() else None,
                "message": normalize_error_message(cls["metadata"].get("message", "") or cls["metadata"].get("raw", "") or ""),
            })
            if representative is None and (cls["metadata"].get("file") or cls["metadata"].get("line")):
                representative = {
                    "file": cls["metadata"].get("file"),
                    "line": int(cls["metadata"].get("line")) if cls["metadata"].get("line") and cls["metadata"]["line"].isdigit() else None,
                    "column": int(cls["metadata"].get("column")) if cls["metadata"].get("column") and cls["metadata"]["column"].isdigit() else None,
                    "message": normalize_error_message(cls["metadata"].get("message", "") or cls["metadata"].get("raw", "") or ""),
                    "raw_line": line,
                }
        if representative is None:
            idx, cls, line = items[0]
            representative = {
                "file": None,
                "line": None,
                "column": None,
                "message": normalize_error_message(cls["metadata"].get("message", "") or cls["metadata"].get("raw", "") or ""),
                "raw_line": line,
            }

        # Simple ranking score (severity weight + count)
        score = count * 10
        if kind == CPP_COMPILE_ERROR:
            score += 50
        elif kind in (LINK_UNDEFINED_SYMBOL, LINK_DUPLICATE_SYMBOL):
            score += 30
        elif kind in (PYTHON_TRACEBACK, SCRIPT_ERROR):
            score += 5

        # Compact examples - only include first example with location
        compact_examples = []
        if examples:
            first = examples[0]
            compact_examples.append({
                "file": first.get("file"),
                "line": first.get("line"),
            })

        # Compact representative - remove raw_line if message is sufficient
        compact_rep = {
            "file": representative.get("file"),
            "line": representative.get("line"),
            "column": representative.get("column"),
            "message": representative.get("message"),
        }

        clusters.append({
            "id": f"C{len(clusters)+1:03d}",
            "kind": kind,
            "count": count,
            "score": round(score, 1),
            "normalized": normalized_msg,
            "representative": compact_rep,
            "examples": compact_examples,
            "action": generate_ai_action(kind),
        })

    clusters.sort(key=lambda c: c["score"], reverse=True)
    return clusters


def count_error_like_lines(lines: List[str], classified: List[Dict[str, Any]]) -> int:
    """Count lines that look like errors but were not classified."""
    error_patterns = [
        r"^\s*(error|fatal error|warning|note)\s*:",
        r"^\s*Error\s*:",
        r"^\s*FAILED:\s*",
        r"^\s*ld\.lld:\s*error",
        r"^\s*undefined reference",
        r"^\s*duplicate symbol",
    ]
    count = 0
    for line, cls in zip(lines, classified):
        if cls["kind"] != UNKNOWN_ERROR:
            continue
        if any(re.search(p, line, re.IGNORECASE) for p in error_patterns):
            count += 1
    return count


def generate_ai_action(kind: str) -> str:
    """Generate a short action hint describing how an AI should handle this error kind."""
    actions = {
        CPP_COMPILE_ERROR: "Handle this cluster first. Read only the representative source location and nearby related declarations.",
        MISSING_HEADER: "Check the missing path, include origin, and whether it is a generated or normal header.",
        LINK_UNDEFINED_SYMBOL: "Inspect the symbol name, the referencing location, and ensure the definition exists in a linked library.",
        LINK_DUPLICATE_SYMBOL: "Identify the duplicate symbol and resolve the linking conflict.",
        MSVC_COMPILE_ERROR: "Read the MSVC error code and the source line, then consult the documentation.",
        SCRIPT_ERROR: "Inspect the first user‑script frame in the traceback.",
        PYTHON_TRACEBACK: "Focus on the exception type and the first project file frame.",
        FAILED_EDGE: "Review the failed ninja edge and its command.",
        UNKNOWN_ERROR: "Manually inspect the raw log line.",
    }
    return actions.get(kind, "Manually inspect the raw log line.")


def redact_secrets_recursive(obj: Any) -> Any:
    """Recursively redact secrets in JSON‑serializable dict/list structures."""
    if isinstance(obj, dict):
        return {k: redact_secrets_recursive(v) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [redact_secrets_recursive(v) for v in obj]
    elif isinstance(obj, str):
        return redact_secrets(obj)
    else:
        return obj