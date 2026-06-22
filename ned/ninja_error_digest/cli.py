#!/usr/bin/env python3
"""Command‑line interface for the Ninja Error Digest tool."""

import argparse
import sys
from pathlib import Path
from .parser import parse_log
from .utils import generate_timestamp_dir
from .reporter import report_summary
from .benchmark import benchmark_parser

def version():
    from . import __version__
    print(f"ninja-error-digest {__version__}")

def cmd_parse(args):
    """Execute the `ned parse` subcommand."""
    log_path = Path(args.log_file)
    if not log_path.is_file():
        sys.stderr.write(f"Error: log file not found: {log_path}\n")
        sys.exit(1)

    summary = parse_log(log_path, format=args.format)
    if args.format == "json":
        import json
        print(json.dumps(summary, indent=2, default=str))
    elif args.format == "yaml":
        import yaml
        print(yaml.dump(summary, sort_keys=False))
    else:
        # Default to pretty‑printed dict
        import pprint
        pprint.pprint(summary)

def cmd_build(args):
    """Execute the `ned build` subcommand – run ninja and parse output."""
    # For MVP we stub the functionality.
    raise NotImplementedError("`ned build` is not yet implemented")

def cmd_report(args):
    """Execute the `ned report` subcommand."""
    report_path = Path(args.path)
    if not report_path.is_dir():
        sys.stderr.write(f"Error: path is not a directory: {report_path}\n")
        sys.exit(1)

    # Load summary JSON from the directory (expects a summary.json inside)
    summary_file = report_path / "summary.json"
    if not summary_file.is_file():
        sys.stderr.write(f"Error: summary.json not found in {report_path}\n")
        sys.exit(1)

    import json
    with open(summary_file, "r", encoding="utf-8") as f:
        summary = json.load(f)

    output_format = getattr(args, "format", "markdown")
    if output_format == "markdown":
        markdown = report_summary(summary)
        print(markdown)
    elif output_format == "json":
        print(json.dumps(summary, indent=2, default=str))
    else:
        print(json.dumps(summary, indent=2, default=str))

def cmd_bench(args):
    """Execute the `ned bench` subcommand."""
    log_dir = Path(args.log_dir)
    if not log_dir.is_dir():
        sys.stderr.write(f"Error: log directory not found: {log_dir}\n")
        sys.exit(1)

    results = benchmark_parser(log_dir)
    import json
    print(json.dumps(results, indent=2, default=str))

def main():
    parser = argparse.ArgumentParser(prog="ned", description="Ninja build‑log digest tool")
    parser.add_argument("--version", action="store_true", help="Print version and exit")

    subparsers = parser.add_subparsers(dest="command", required=True)

    # parse subcommand
    p_parse = subparsers.add_parser("parse", help="Parse a saved Ninja log file")
    p_parse.add_argument("log_file", help="Path to the Ninja build log (e.g., build.log)")
    p_parse.add_argument("--format", choices=["json", "yaml", "text"], default="json", help="Output format")

    # build subcommand (stub)
    p_build = subparsers.add_parser("build", help="Run Ninja and parse its output (Phase 4)")
    p_build.add_argument("-C", dest="build_dir", default=".", help="Ninja build directory")
    p_build.add_argument("target", nargs="?", help="Ninja target to build")
    p_build.add_argument("--keep-going", type=int, default=20, help="Ninja -k option")
    p_build.add_argument("--jobs", type=int, default=None, help="Ninja -j option")
    p_build.add_argument("--timeout-sec", type=int, default=None, help="Timeout for the build in seconds")
    p_build.add_argument("--save-raw-log", action="store_true", help="Save raw ninja output")
    p_build.add_argument("--max-clusters", type=int, default=10, help="Maximum clusters to report")

    # report subcommand
    p_report = subparsers.add_parser("report", help="Generate a human‑readable report from a parsed run")
    p_report.add_argument("path", help="Directory containing summary.json (e.g., .ned/runs/latest)")
    p_report.add_argument("--format", choices=["markdown", "json"], default="markdown", help="Report format")

    # bench subcommand
    p_bench = subparsers.add_parser("bench", help="Benchmark the parser on a set of fixture logs")
    p_bench.add_argument("log_dir", help="Directory containing fixture logs (e.g., tests/fixtures/logs)")
    p_bench.add_argument("--format", choices=["json", "yaml"], default="json", help="Benchmark output format")

    args = parser.parse_args()

    if args.version:
        version()
        return

    try:
        if args.command == "parse":
            cmd_parse(args)
        elif args.command == "build":
            cmd_build(args)
        elif args.command == "report":
            cmd_report(args)
        elif args.command == "bench":
            cmd_bench(args)
    except NotImplementedError as e:
        sys.stderr.write(str(e) + "\n")
        sys.exit(1)

if __name__ == "__main__":
    main()