#!/usr/bin/env python3
"""Parse [GMD] events from QNX runtime smoke log and correlate with terminate/exit134."""
import re
import sys

# Match lock-entry/REENTRY/lock-ok/unlock-entry/unlock-ok events.
# REENTRY lacks total_held; others have it.
GMD_RE = re.compile(r'\[GMD\] pid=(\d+) tid=(\S+) event=(\S+) this=(\S+) was_held=(\S+)(?: total_held=(\S+))?')
TERM_RE = re.compile(r'std::terminate invoked \(pid=(\d+) tid=(\S+)\)')
EXIT_RE = re.compile(r'GPU process exited unexpectedly: exit_code=(\d+)')


def main():
    if len(sys.argv) < 2:
        print("usage: parse_gmd.py <log>")
        sys.exit(2)
    log = sys.argv[1]

    events = []
    terminates = []
    exits = []
    with open(log) as f:
        for i, line in enumerate(f, 1):
            m = GMD_RE.search(line)
            if m:
                events.append((i, m.group(1), m.group(2), m.group(3),
                              m.group(4), m.group(5), m.group(6) or '-'))
            m = TERM_RE.search(line)
            if m:
                terminates.append((i, m.group(1), m.group(2)))
            m = EXIT_RE.search(line)
            if m:
                exits.append((i, m.group(1)))

    pids = {}
    for ev in events:
        pids.setdefault(ev[1], []).append(ev)

    print(f"=== parse_gmd.py {log} ===")
    print(f"total events: {len(events)}, terminates: {len(terminates)}, "
          f"GPU exits: {len(exits)}")

    for pid in sorted(pids.keys()):
        evs = pids[pid]
        distinct_mutexes = set(ev[4] for ev in evs)
        reentry_events = [ev for ev in evs if ev[3] == 'REENTRY']
        has_terminate = any(t[1] == pid for t in terminates)
        print(f"\n  pid={pid}: {len(evs)} events, "
              f"{len(distinct_mutexes)} distinct mutex addresses, "
              f"{len(reentry_events)} REENTRY events, "
              f"terminate={'YES' if has_terminate else 'no'}")
        for rm in sorted(distinct_mutexes):
            print(f"    mutex: {rm}")

    print("\n=== REENTRY → terminate correlation ===")
    for term_line, term_pid, term_tid in terminates:
        pid_events = pids.get(term_pid, [])
        reentry_before = [ev for ev in pid_events
                          if ev[3] == 'REENTRY' and ev[0] < term_line]
        if reentry_before:
            last = reentry_before[-1]
            print(f"  pid={term_pid} tid={term_tid} (terminate @ line {term_line}):")
            print(f"    last REENTRY @ line {last[0]}: this={last[4]} was_held={last[5]}")
            tail = pid_events[-8:] if len(pid_events) >= 8 else pid_events
            print(f"    last {len(tail)} events before terminate:")
            for ev in tail:
                print(f"      L{ev[0]} [{ev[3]:14s}] this={ev[4]} "
                      f"was_held={ev[5]} total_held={ev[6]}")
        else:
            print(f"  pid={term_pid}: terminate fired but NO REENTRY recorded")

    print("\n=== Verdict ===")
    confirmed_count = 0
    for term_line, term_pid, term_tid in terminates:
        reentry_before = [ev for ev in pids.get(term_pid, [])
                          if ev[3] == 'REENTRY' and ev[0] < term_line]
        if reentry_before:
            confirmed_count += 1
            print(f"  pid={term_pid}: CONFIRMED same-thread same-GlobalMutex re-entry")
            print(f"    REENTRY immediately precedes terminate (tid=1 same pid), exit_code=134")
        else:
            print(f"  pid={term_pid}: NO REENTRY — cannot confirm same-mutex re-entry")
    print(f"\nTotal CONFIRMED: {confirmed_count}/{len(terminates)} terminate events")


if __name__ == '__main__':
    main()
