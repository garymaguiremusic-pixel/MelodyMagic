#!/usr/bin/env python3
"""Compare two MIDI files note for note (A/B test against the original plugin).

usage: diff_midi.py reference.mid candidate.mid [--tolerance TICKS] [--ignore-velocity] [--ppq 96]

Both files are converted to the same resolution (--ppq, default 96) and their first note is
aligned to tick 0 unless --no-align is given (recordings rarely start exactly on the bar).
Notes are matched by pitch and start time; the report lists notes that are missing, extra,
or differ in end time or velocity. The exit code is 0 when the files match.
No third-party packages are needed.
"""
import argparse
import struct
import sys
from collections import defaultdict, deque


def read_var_len(data, pos):
    value = 0
    while True:
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos


def read_midi(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"MThd":
        raise ValueError(f"{path}: not a standard MIDI file")
    header_len = struct.unpack(">I", data[4:8])[0]
    _fmt, ntracks, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise ValueError(f"{path}: SMPTE time division is not supported")
    pos = 8 + header_len
    notes = []
    for _ in range(ntracks):
        if pos + 8 > len(data):
            break
        chunk_len = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        if data[pos:pos + 4] != b"MTrk":
            pos += 8 + chunk_len
            continue
        end = pos + 8 + chunk_len
        i = pos + 8
        tick = 0
        status = 0
        open_notes = defaultdict(deque)
        while i < end:
            delta, i = read_var_len(data, i)
            tick += delta
            if data[i] & 0x80:
                status = data[i]
                i += 1
            if status == 0xFF:
                meta_type = data[i]
                length, i = read_var_len(data, i + 1)
                i += length
                if meta_type == 0x2F:
                    break
                continue
            if status in (0xF0, 0xF7):
                length, i = read_var_len(data, i)
                i += length
                continue
            hi, ch = status & 0xF0, status & 0x0F
            d1 = data[i]
            i += 1
            d2 = 0
            if hi not in (0xC0, 0xD0):
                d2 = data[i]
                i += 1
            if hi == 0x90 and d2 > 0:
                open_notes[(ch, d1)].append((tick, d2))
            elif hi == 0x80 or (hi == 0x90 and d2 == 0):
                if open_notes[(ch, d1)]:
                    start, vel = open_notes[(ch, d1)].popleft()
                    notes.append({"note": d1, "ch": ch + 1, "vel": vel, "start": start, "end": tick})
        pos = end
    return division, sorted(notes, key=lambda n: (n["start"], n["note"]))


def normalise(notes, src_ppq, dst_ppq, align):
    scale = dst_ppq / src_ppq
    out = [dict(n, start=n["start"] * scale, end=n["end"] * scale) for n in notes]
    if align and out:
        first = min(n["start"] for n in out)
        for n in out:
            n["start"] -= first
            n["end"] -= first
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("reference")
    ap.add_argument("candidate")
    ap.add_argument("--tolerance", type=float, default=1.0, help="start/end tolerance in ticks (default 1)")
    ap.add_argument("--ignore-velocity", action="store_true")
    ap.add_argument("--ppq", type=int, default=96)
    ap.add_argument("--no-align", action="store_true", help="do not move the first note to tick 0")
    args = ap.parse_args()

    ref_ppq, ref = read_midi(args.reference)
    cand_ppq, cand = read_midi(args.candidate)
    ref = normalise(ref, ref_ppq, args.ppq, not args.no_align)
    cand = normalise(cand, cand_ppq, args.ppq, not args.no_align)

    unmatched = list(cand)
    problems = []
    matched = 0
    for r in ref:
        best = None
        for c in unmatched:
            if c["note"] == r["note"] and abs(c["start"] - r["start"]) <= args.tolerance:
                if best is None or abs(c["start"] - r["start"]) < abs(best["start"] - r["start"]):
                    best = c
        if best is None:
            problems.append(f"MISSING  note {r['note']:3d} start {r['start']:9.2f} end {r['end']:9.2f} vel {r['vel']:3d}")
            continue
        unmatched.remove(best)
        matched += 1
        if abs(best["end"] - r["end"]) > args.tolerance:
            problems.append(f"END      note {r['note']:3d} start {r['start']:9.2f}: end {r['end']:.2f} vs {best['end']:.2f}")
        if not args.ignore_velocity and best["vel"] != r["vel"]:
            problems.append(f"VELOCITY note {r['note']:3d} start {r['start']:9.2f}: {r['vel']} vs {best['vel']}")
    for c in unmatched:
        problems.append(f"EXTRA    note {c['note']:3d} start {c['start']:9.2f} end {c['end']:9.2f} vel {c['vel']:3d}")

    print(f"reference {len(ref)} notes, candidate {len(cand)} notes, {matched} matched by pitch and start")
    for p in problems:
        print(p)
    print("MATCH" if not problems else f"{len(problems)} difference(s)")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
