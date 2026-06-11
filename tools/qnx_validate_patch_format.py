#!/usr/bin/env python3
# Copyright (c) 2026 The Chromium Embedded Framework Authors.
# Use of this source code is governed by a BSD-style license that can be found
# in the LICENSE file.

from __future__ import annotations

import argparse
import glob
import os
import re
import sys


def find_offenders(patch_path: str):
  offenders = []
  with open(patch_path, 'r', encoding='utf-8', errors='replace') as f:
    for lineno, line in enumerate(f, 1):
      if re.match(r'^diff --git a/.* b/.*$', line):
        offenders.append((lineno, line.rstrip('\n')))
      elif re.match(r'^--- a/.*$', line):
        offenders.append((lineno, line.rstrip('\n')))
      elif re.match(r'^\+\+\+ b/.*$', line):
        offenders.append((lineno, line.rstrip('\n')))
      elif re.match(r'^(rename|copy) (from|to) [ab]/.*$', line):
        offenders.append((lineno, line.rstrip('\n')))
  return offenders


def main():
  parser = argparse.ArgumentParser(
      description='Validate that CEF patch files use git diff --no-prefix format.')
  parser.add_argument(
      '--root',
      default=os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir)),
      help='CEF root directory (default: repository root)')
  args = parser.parse_args()

  patch_root = os.path.join(args.root, 'patch', 'patches')
  patch_files = sorted(glob.glob(os.path.join(patch_root, '**', '*.patch'), recursive=True))

  failures = []
  for patch_path in patch_files:
    offenders = find_offenders(patch_path)
    if offenders:
      failures.append((patch_path, offenders))

  if not failures:
    print(f'OK: {len(patch_files)} CEF patch files use no-prefix git diff format')
    return 0

  print('ERROR: prefix-form patch headers found; regenerate with git diff --no-prefix --relative', file=sys.stderr)
  for patch_path, offenders in failures:
    rel = os.path.relpath(patch_path, args.root)
    print(f'  {rel}', file=sys.stderr)
    for lineno, text in offenders[:5]:
      print(f'    L{lineno}: {text}', file=sys.stderr)
    if len(offenders) > 5:
      print(f'    ... {len(offenders) - 5} more', file=sys.stderr)
  return 1


if __name__ == '__main__':
  raise SystemExit(main())
