#!/usr/bin/env python3
"""Correlate throw-time absolute RAs with the latest MAP snapshot for the
same pid (at or before the throw).  Outputs DSO basename + file offset per
RA.  Then symbolize via QNX addr2line on the unstripped binary in
out/qnx_release/.

Usage: correlate.py <smoke.log>
"""
import re
import subprocess
import sys
import os
from collections import defaultdict

MAP_RE = re.compile(r'\[MAP\] snap=(\d+) pid=(\d+) tid=(\S+)')
SEG_RE = re.compile(r'\[MAP\] snap=(\d+) pid=(\d+) tid=(\S+)\s+seg base=(\S+) end=(\S+) filesz=(\S+)')
NAME_RE = re.compile(r'\[MAP\] snap=(\d+) pid=(\d+) tid=(\S+)\s+name=(\S+)\s+base=(\S+)')
THROW_RE = re.compile(r'\[TRACER\] snap=0 pid=(\d+) tid=(\S+) __cxa_throw depth=(\d+) call=(\S+)')
RA_RE = re.compile(r'\[TRACER\] snap=0 pid=(\d+) tid=(\S+) ra\[(\d+)\]=(\S+)')

ADDR2LINE = '/home/yuta/qnx800/host/linux/x86_64/usr/bin/x86_64-pc-nto-qnx8.0.0-addr2line'

def main():
    if len(sys.argv) < 2:
        print("usage: correlate.py <log>")
        sys.exit(2)
    log = sys.argv[1]

    pid_segs = {}  # pid -> {snap_id: [(seg_base, seg_end, dso_name), ...]}
    last_dso = {}   # (pid, snap_id) -> dso_name (set by NAME line, used by subsequent SEG lines)

    # Pass 1: MAP snapshots
    with open(log) as f:
        for line in f:
            line = line.strip()
            m = MAP_RE.search(line)
            if not m: continue
            snap_id, pid, tid = m.group(1), m.group(2), m.group(3)
            if 'BEGIN' in line:
                pid_segs.setdefault(pid, {}).setdefault(snap_id, [])
                last_dso[(pid, snap_id)] = '?'
                continue
            if 'END' in line:
                continue
            if 'name=' in line and 'base=' in line:
                mm = NAME_RE.search(line)
                if mm:
                    last_dso[(pid, snap_id)] = mm.group(4)
                continue
            ms = SEG_RE.search(line)
            if ms:
                seg_base = int(ms.group(4), 16)
                seg_end = int(ms.group(5), 16)
                dso = last_dso.get((pid, snap_id), '?')
                pid_segs[pid][snap_id].append((seg_base, seg_end, dso))
                continue

    # Pass 2: throw events
    throws = []
    current = None
    with open(log) as f:
        for line in f:
            line = line.strip()
            mt = THROW_RE.search(line)
            if mt:
                current = (mt.group(1), mt.group(2), mt.group(3), mt.group(4), [])
                throws.append(current)
                continue
            mr = RA_RE.search(line)
            if mr and current and mr.group(1) == current[0]:
                current[4].append((int(mr.group(3)), mr.group(4)))
                continue

    # Output
    print('=' * 80)
    print('correlate.py', log)
    print('=' * 80)
    print('throws:', len(throws), 'snapshots:', sum(len(s) for s in pid_segs.values()))

    for th in throws:
        pid, tid, depth, call, frames = th
        print('')
        print('[THROW] pid={} tid={} depth={} call={}'.format(pid, tid, depth, call))
        snaps = pid_segs.get(pid, {})
        if not snaps:
            print('  NO MAP snapshot for pid={}'.format(pid))
            continue
        latest = max(snaps.keys(), key=int)
        segs = snaps[latest]
        for idx, ra_str in frames:
            ra = int(ra_str, 16)
            matched = None
            for seg_base, seg_end, dso in segs:
                if seg_base <= ra < seg_end:
                    matched = (dso, ra - seg_base)
                    break
            if matched:
                dso, off = matched
                print('  ra[{}]={}  snap={}  DSO={}  offset=0x{:x}'.format(
                    idx, ra_str, latest, dso, off))
                # Try addr2line
                bin_path = find_binary(dso)
                if bin_path:
                    try:
                        r = subprocess.run([ADDR2LINE, '-e', bin_path, '-f', '-C',
                                           '0x{:x}'.format(off)],
                                          capture_output=True, text=True, timeout=5)
                        if r.stdout.strip():
                            print('    addr2line: ' + r.stdout.strip().replace('\n', ' | '))
                    except Exception as e:
                        print('    addr2line error:', e)
            else:
                print('  ra[{}]={}  snap={}  DSO=?  (not in MAP)'.format(idx, ra_str, latest))

def find_binary(dso):
    """Map DSO basename to local file path in out/qnx_release/."""
    base = os.path.basename(dso) if '/' in dso else dso
    # QNX system libs
    qnx = '/home/yuta/qnx800/target/qnx/x86_64/usr/lib'
    for d in [qnx, '/home/yuta/qnx800/target/qnx/x86_64/lib',
              '/home/yuta/qnx800/target/qnx/x86_64/usr/lib/dlopen']:
        cand = os.path.join(d, base)
        if os.path.exists(cand): return cand
        # Try with .2 / .2.0 suffix
        for suf in ['.2', '.2.0', '.so.2', '']:
            c = os.path.join(d, base + suf)
            if os.path.exists(c): return c
    # Chromium-built
    out = '/home/yuta/chromium/src/out/qnx_release'
    cand = os.path.join(out, base)
    if os.path.exists(cand): return cand
    return None

if __name__ == '__main__':
    main()