"""Report generation for Ninja Error Digest.

Provides functions to render parsed summaries into Markdown (and other formats)
as described in the README.
"""

from typing import Dict, Any


def report_summary(summary: Dict[str, Any]) -> str:
    """Convert a parsed summary JSON into a Markdown report.

    Follows the layout in the README.
    """
    lines = []

    # Header
    lines.append("# Ninja build failed\n")

    # Quick facts
    raw = summary.get("raw_log", {})
    bytes_raw = raw.get("bytes", 0)
    lines_raw = raw.get("lines", 0)
    summary_bytes = summary.get("summary", {}).get("bytes", 0)
    compression_ratio = summary.get("summary", {}).get("compression_ratio", 0.0)

    lines.append(f"- Failed edges: {len(summary.get('failed_edges', {}).get('sample', []))}")
    lines.append(f"- Raw log: {bytes_raw:,} bytes")
    lines.append(f"- Summary: {summary_bytes:,} bytes")
    lines.append(f"- Estimated token reduction: {compression_ratio * 100:.1f}%\n")

    # Top error clusters
    clusters = summary.get("clusters", [])
    if clusters:
        lines.append("## Top error clusters\n")
        for cluster in clusters[:10]:
            lines.append(f"### {cluster['id']} {cluster['kind']}")
            lines.append(f"- Count: {cluster['count']}")
            rep = cluster.get('representative', {})
            loc = []
            if rep.get('file'):
                loc.append(rep['file'])
            if rep.get('line'):
                loc.append(f"{rep['line']}")
            if rep.get('column'):
                loc.append(f"{rep['column']}")
            if loc:
                lines.append(f"- Representative: {':'.join(loc)}")
            if rep.get('message'):
                lines.append(f"- Message: {rep['message']}")
            if cluster.get('normalized'):
                lines.append(f"- Normalized: {cluster['normalized']}")

            action = cluster.get('action', '')
            if action:
                lines.append("\n**Suggested AI action:**")
                lines.append(f"{action}\n")

    # Unparsed samples (if any)
    unparsed = summary.get('unparsed', {})
    if unparsed.get('error_like_lines', 0) > 0:
        lines.append("## Unparsed error‑like lines")
        lines.append(f"Count: {unparsed['error_like_lines']}")
        samples = unparsed.get('sample', [])
        for s in samples[:3]:
            lines.append(f"- {s}")
        lines.append("")

    # Metrics
    metrics = summary.get('metrics', {})
    if metrics:
        lines.append("## Performance metrics")
        lines.append(f"- Parse time: {metrics.get('parse_time_ms', 0)} ms")
        lines.append(f"- Estimated raw tokens: {metrics.get('estimated_raw_tokens', 0)}")
        lines.append(f"- Estimated summary tokens: {metrics.get('estimated_summary_tokens', 0)}")
        lines.append(f"- Token reduction ratio: {metrics.get('estimated_token_reduction_ratio', 0):.4f}")

    return "\n".join(lines)