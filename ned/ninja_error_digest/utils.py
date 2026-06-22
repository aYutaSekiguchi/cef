"""Utility functions for the Ninja Error Digest tool.

Provides simple normalization, classification, and metrics helpers.
"""

import re
from datetime import datetime
from pathlib import Path
from typing import Dict, Any, List

# Constants
DEFAULT_RUN_BASE = Path(".ned")


def generate_timestamp_dir(base: Path = DEFAULT_RUN_BASE) -> Path:
    """Create a timestamped run directory and return its path."""
    timestamp = datetime.utcnow().strftime("%Y%m%d-%H%M%S")
    run_dir = base / "runs" / timestamp
    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir


def redact_secrets(text: str) -> str:
    """Redact common secret patterns from text."""
    patterns = [
        (r"API[_-]?KEY\s*=\s*[^\s&]+", "API_KEY=***REDACTED***"),
        (r"SECRET\s*=\s*[^\s&]+", "SECRET=***REDACTED***"),
        (r"PASSWORD\s*=\s*[^\s&]+", "PASSWORD=***REDACTED***"),
        (r"Bearer\s+\S+", "Bearer ***REDACTED***"),
        (r"--token\s+\S+", "--token ***REDACTED***"),
        (r"--password\s+\S+", "--password ***REDACTED***"),
    ]
    for pat, repl in patterns:
        text = re.sub(pat, repl, text, flags=re.IGNORECASE)
    return text


def estimate_token_count(text: str) -> int:
    """Rough token count estimation (1 token ≈ 4 characters)."""
    return max(1, len(text) // 4)


def normalize_error_message(message: str) -> str:
    """Normalize an error message for clustering.

    Simple normalization: remove paths, numbers, quoted symbols.
    """
    # Remove absolute paths
    message = re.sub(r"\b(/[\w./-]+)", "<path>", message)
    # Remove quoted symbols
    message = re.sub(r"['\"]([^'\"]+)['\"]", "<symbol>", message)
    # Remove numeric literals (including in identifiers like Helper0)
    message = re.sub(r"\b\d+\b", "<num>", message)
    # Remove trailing numbers in identifiers (Helper0 -> Helper)
    message = re.sub(r"([A-Za-z_])\d+\b", r"\1<num>", message)
    # Collapse whitespace
    message = re.sub(r"\s+", " ", message).strip()
    return message


def classify_error_line(line: str) -> Dict[str, Any]:
    """Classify a log line into error kind.

    Returns dict with keys: "kind", "metadata", "matched".
    """
    line = line.strip()
    # FAILED edge
    if line.startswith("FAILED:"):
        return {"kind": "failed_edge", "metadata": {"edge": line[len("FAILED:"):].strip()}, "matched": line}
    # Clang/GCC compile errors
    m = re.match(r"^(?P<file>[^:\n]+):(?P<line>\d+):(?P<column>\d+):\s*(?P<level>fatal error|error|warning|note):\s*(?P<message>.*)$", line)
    if m:
        kind = "cpp_compile_error" if m.group("level") != "fatal error" else "cpp_fatal_error"
        return {"kind": kind, "metadata": m.groupdict(), "matched": line}
    # MSVC errors
    m = re.match(r"^(?P<file>.+)\((?P<line>\d+)(,(?P<column>\d+))?\):\s*(?P<level>fatal error|error|warning)\s+(?P<code>[A-Z]+\d+):\s*(?P<message>.*)$", line)
    if m:
        return {"kind": "msvc_compile_error", "metadata": m.groupdict(), "matched": line}
    # Linker errors
    if re.search(r"\b(undefined reference|duplicate symbol|undefined symbol)\b", line):
        if "duplicate" in line.lower():
            kind = "link_duplicate_symbol"
        else:
            kind = "link_undefined_symbol"
        return {"kind": kind, "metadata": {"raw": line}, "matched": line}
    # Python traceback
    if line.startswith("Traceback (most recent call last):"):
        return {"kind": "python_traceback", "metadata": {"raw": line}, "matched": line}
    # Script errors
    if line.startswith("Error:"):
        return {"kind": "script_error", "metadata": {"msg": line[len("Error:"):].strip()}, "matched": line}
    # Unknown
    return {"kind": "unknown_error", "metadata": {"raw": line}, "matched": line}

__all__ = [
    "generate_timestamp_dir",
    "redact_secrets",
    "estimate_token_count",
    "normalize_error_message",
    "classify_error_line",
]