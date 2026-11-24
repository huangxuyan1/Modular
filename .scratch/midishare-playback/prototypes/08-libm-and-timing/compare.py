"""PROTOTYPE, throwaway: midishare-playback ticket 08. Compares the runs build_and_run.ps1
made: the library sweep (MSVC, MSVC with FMA3 off, WebAssembly) value by value, and the MIDI
frame probe (MSVC against WebAssembly) event by event.

    python -I compare.py <out-folder>
"""
import array
import os
import re
import struct
import sys


def read_bits(path):
    """The file's doubles as their 64-bit patterns."""
    values = array.array("q")
    with open(path, "rb") as f:
        values.frombytes(f.read())
    return values


def ordered(bits):
    """A double's bits as an integer that orders like the value, so ulps subtract."""
    return bits if bits >= 0 else -(bits & 0x7FFFFFFFFFFFFFFF)


def as_float(bits):
    (x,) = struct.unpack("<d", struct.pack("<q", bits))
    return struct.unpack("<f", struct.pack("<f", x))[0]


def lines(path):
    """A run's text output; Windows PowerShell 5.1 redirects as UTF-16 with a BOM."""
    with open(path, "rb") as f:
        raw = f.read()
    text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode()
    return text.splitlines()


def sweep_meta(path):
    meta = {}
    for line in lines(path):
        m = re.match(r"(\S+) inputs=(\d+) input_hash=(\S+) ns_per_call=(\S+)", line)
        if m:
            meta[m.group(1)] = (int(m.group(2)), m.group(3), float(m.group(4)))
    return meta


def compare_sweep(out, a, b):
    meta_a = sweep_meta(os.path.join(out, a, "sweep.txt"))
    meta_b = sweep_meta(os.path.join(out, b, "sweep.txt"))
    print(f"\n## {a} against {b}\n")
    print("| test | values | differ | % | max ulps | float casts differ |")
    print("| --- | --- | --- | --- | --- | --- |")
    for name in meta_a:
        if meta_a[name][1] != meta_b[name][1]:
            print(f"| {name} | INPUTS DIFFER | | | | |")
            continue
        va = read_bits(os.path.join(out, a, name + ".bin"))
        vb = read_bits(os.path.join(out, b, name + ".bin"))
        differ = max_ulps = float_differ = 0
        for x, y in zip(va, vb):
            if x != y:
                differ += 1
                max_ulps = max(max_ulps, abs(ordered(x) - ordered(y)))
                if as_float(x) != as_float(y):
                    float_differ += 1
        pct = 100.0 * differ / len(va)
        print(f"| {name} | {len(va):,} | {differ:,} | {pct:.3f} | {max_ulps} | {float_differ:,} |")


def timings(out, runs):
    metas = {run: sweep_meta(os.path.join(out, run, "sweep.txt")) for run in runs}
    print("\n## ns per call\n")
    print("| test | " + " | ".join(runs) + " |")
    print("| --- |" + " --- |" * len(runs))
    for name in metas[runs[0]]:
        print(f"| {name} | " + " | ".join(f"{metas[r][name][2]:.1f}" for r in runs) + " |")


def frame_lines(path):
    rows, totals = {}, []
    pattern = (r"(\S+) grid=(\d+) bpm=(\d+) rate=(\d+) tick=(\d+) frame=(\d+) "
               r"exact=(\d+) tie=(\d)")
    for line in lines(path):
        if line.startswith("TOTAL") or line.startswith("sizeof"):
            totals.append(line.strip())
            continue
        m = re.match(pattern, line)
        if m:
            key = (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5))
            rows[key] = (int(m.group(6)), int(m.group(7)), int(m.group(8)))
    return rows, totals


def compare_frames(out):
    msvc, msvc_totals = frame_lines(os.path.join(out, "msvc", "frames.txt"))
    wasm, wasm_totals = frame_lines(os.path.join(out, "wasm", "frames.txt"))
    print("\n## MIDI event frames\n")
    for line in msvc_totals:
        print("msvc", line)
    for line in wasm_totals:
        print("wasm", line)
    for label in ("long_double", "double"):
        for grid in ("40", "1"):
            keys = {k for k in set(msvc) | set(wasm) if k[0] == label and k[1] == grid}
            # A build lists only events off the exact frame; an unlisted one is on it.
            def frame(rows, k):
                other = msvc.get(k) or wasm.get(k)
                return rows[k][0] if k in rows else other[1]
            differ = [k for k in keys if frame(msvc, k) != frame(wasm, k)]
            ties = sum(1 for k in differ if (msvc.get(k) or wasm.get(k))[2] == 1)
            bpms = sorted({int(k[2]) for k in differ})
            print(f"{label} grid={grid}: events on different frames in msvc and wasm: "
                  f"{len(differ)} ({ties} exact ties); tempos affected: {len(bpms)}"
                  + (f", e.g. BPM {bpms[:12]}" if bpms else ""))
            for k in sorted(differ, key=lambda k: (int(k[3]), int(k[2]), int(k[4])))[:6]:
                print("   ", "bpm", k[2], "rate", k[3], "tick", k[4], "msvc", frame(msvc, k),
                      "wasm", frame(wasm, k), "exact", (msvc.get(k) or wasm.get(k))[1])
            # The part a midishare clip can reach: sixteenth positions at the page's rates.
            reach = [k for k in differ if int(k[4]) % 120 == 0 and k[3] in ("44100", "48000")]
            print(f"    on sixteenth positions at 44.1/48 kHz: {len(reach)} events, "
                  f"{len({k[2] for k in reach})} tempos")
            shown = set()
            for k in sorted(reach, key=lambda k: (int(k[2]), int(k[3]), int(k[4]))):
                if k[2] in ("90", "116", "132") and k[2] not in shown:
                    shown.add(k[2])
                    print("     ", "bpm", k[2], "rate", k[3], "tick", k[4], "msvc",
                          frame(msvc, k), "wasm", frame(wasm, k))


def portable_accuracy(out):
    """How far the portable routines are from musl's, which aims below one ulp."""
    print("\n## portable routines against musl (wasm run)\n")
    pairs = [("portable_exp2_int_cents", "exp2_int_cents"), ("portable_exp2_rand", "exp2_rand"),
             ("portable_sin_pan", "sin_pan"), ("portable_sin_rand", "sin_rand")]
    for port, ref in pairs:
        a = read_bits(os.path.join(out, "wasm", port + ".bin"))
        b = read_bits(os.path.join(out, "wasm", ref + ".bin"))
        ulps = [abs(ordered(x) - ordered(y)) for x, y in zip(a, b)]
        off = sum(1 for u in ulps if u)
        print(f"{port}: max {max(ulps)} ulp, {off:,} of {len(ulps):,} values off by any")


def main():
    out = sys.argv[1]
    compare_sweep(out, "msvc", "wasm")
    compare_sweep(out, "msvc", "msvc-nofma3")
    compare_sweep(out, "msvc-nofma3", "wasm")
    timings(out, ["msvc", "msvc-nofma3", "wasm"])
    portable_accuracy(out)
    compare_frames(out)


if __name__ == "__main__":
    main()
