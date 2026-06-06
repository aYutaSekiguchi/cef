#!/usr/bin/env python3
"""Create a normalized build-breakage note from a small set of fields."""

from __future__ import annotations

import argparse
import datetime as dt
import re
from pathlib import Path


def slugify(value: str) -> str:
    slug = value.strip().lower()
    slug = re.sub(r"[^a-z0-9]+", "-", slug)
    slug = re.sub(r"-{2,}", "-", slug).strip("-")
    return slug or "untitled-breakage"


def default_repo_root() -> Path:
    return Path(__file__).resolve().parents[5]


def render_bullets(items: list[str], placeholder: str) -> str:
    if not items:
        return f"- {placeholder}"
    return "\n".join(f"- {item}" for item in items)


def render_note(
    title: str,
    stage: str,
    category: str,
    scope: str,
    signature: str,
    files_touched: list[str],
    related_notes: list[str],
) -> str:
    today = dt.date.today().isoformat()
    return f"""# {title}

- Date: {today}
- Signature: {signature}
- Stage: {stage}
- Category: {category}
- Scope: {scope or "[TODO]"}

## Symptoms

- [TODO]

## Root cause

- [TODO]

## Fix pattern

- [TODO]

## Applied change

- [TODO]

## Verification

- [TODO]

## Files touched

{render_bullets(files_touched, "[TODO]")}

## Related notes

{render_bullets(related_notes, "[TODO]")}
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", required=True, help="Stage such as compile, link, or test")
    parser.add_argument("--category", required=True, help="Cause class such as platform-api-gap")
    parser.add_argument("--title", required=True, help="Human-readable title")
    parser.add_argument("--signature", required=True, help="Short grep-friendly failure signature")
    parser.add_argument("--scope", default="", help="Subsystem or target, for example base or net")
    parser.add_argument(
        "--file",
        dest="files_touched",
        action="append",
        default=[],
        help="File touched by the fix. Repeat for multiple files.",
    )
    parser.add_argument(
        "--related",
        dest="related_notes",
        action="append",
        default=[],
        help="Related note or summary path. Repeat for multiple items.",
    )
    parser.add_argument(
        "--root",
        default="docs/qnx/history/build-errors",
        help="Catalog root relative to the repository root",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite an existing note if the path already exists",
    )
    args = parser.parse_args()

    repo_root = default_repo_root()
    note_root = repo_root / args.root
    note_dir = note_root / slugify(args.stage) / slugify(args.category)
    note_path = note_dir / f"{slugify(args.title)}.md"

    if note_path.exists() and not args.force:
        print(note_path)
        print("Note already exists. Re-run with --force to overwrite.")
        return 1

    note_dir.mkdir(parents=True, exist_ok=True)
    note_path.write_text(
        render_note(
            title=args.title.strip(),
            stage=slugify(args.stage),
            category=slugify(args.category),
            scope=args.scope.strip(),
            signature=args.signature.strip(),
            files_touched=args.files_touched,
            related_notes=args.related_notes,
        ),
        encoding="utf-8",
    )
    print(note_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
