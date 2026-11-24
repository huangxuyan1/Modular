"""PROTOTYPE, throwaway: soundfont-node ticket 07. Writes the comparison's MIDI clips.

    python -I PROTOTYPE_make_clips.py <output folder>

Standard library only. Writes, into the output folder:

- clip.mid / clip.json  The comparison clip. One note per key range of FluidR3_GM's
                        "Yamaha Grand Piano" (bank 0, program 0) at each of nine velocities,
                        one in each velocity zone from 64 up: 20 x 9 = 180 notes on channel 1,
                        one at a time. Each is held 1.28 s, long enough to reach its decay,
                        then released for 1.28 s, longer than any of the preset's releases.
                        The track opens with bank select 0 (CC0 and CC32) and program change 0,
                        so FluidSynth selects the preset from the MIDI bytes; the harness
                        selects it in the node, which ignores both messages.
                        Time runs in 1 ms ticks, and every event falls on a multiple of 640 ms,
                        which is a whole number of FluidSynth's 64-frame blocks at both 44.1
                        and 48 kHz, so its player starts and releases each note on the frame
                        the node does (see the ticket). clip.json lists the notes.
- low.mid / low.json    The same layout at velocities 16, 32, 48 and 63, all in the preset's
                        lowest velocity zone: 80 notes.
- preview.mid           The note preview, as midishare's contract defines it: C5 (MIDI 72) at
                        velocity 105 from the first frame for 0.25 s, then its release.

The worst-case clip comes from map ticket 02's PROTOTYPE_make_clips.py (worst.mid).
"""

import json
import struct
import sys
from pathlib import Path

DIVISION = 500        # ticks per quarter
TEMPO = 500_000       # microseconds per quarter, so a tick is 1 ms
GRID_MS = 640         # 28,224 frames at 44.1 kHz and 30,720 at 48 kHz: both multiples of 64
HOLD_MS = 2 * GRID_MS
SLOT_MS = 4 * GRID_MS

# The preset's key ranges (ticket 10's zone table): low key, high key, root.
RANGES = [(0, 26, 26), (27, 30, 30), (31, 34, 34), (35, 38, 38), (39, 42, 42), (43, 46, 46),
          (47, 50, 50), (51, 54, 54), (55, 58, 58), (59, 62, 62), (63, 66, 66), (67, 70, 70),
          (71, 74, 74), (75, 78, 78), (79, 84, 84), (85, 90, 90), (91, 94, 94), (95, 99, 99),
          (100, 104, 104), (105, 108, 108)]
# One velocity in each of the preset's velocity zones from 64 up: 0-64, 65-72, 73-80, 81-88,
# 89-96, 97-104, 105-112, 113-120 and 121-127.
VELOCITIES = [64, 72, 80, 88, 96, 104, 112, 120, 127]
# Below 64, where the node and FluidSynth differ by design: compared only against a copy of
# the soundfont that makes FluidSynth apply the node's velocity-to-cutoff curve.
LOW_VELOCITIES = [16, 32, 48, 63]


def key_in(low: int, high: int) -> int:
    """The middle of the range, but never below A0 (MIDI 21), the piano's lowest key."""
    return max(21, low + (high - low) // 2)


def vlq(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def write_midi(path: Path, events, end_tick: int) -> None:
    """events: (tick, order, bytes). A type 0 file at 1 ms per tick."""
    body = bytearray(b"\x00\xff\x51\x03" + TEMPO.to_bytes(3, "big"))
    last = 0
    for tick, _, data in sorted(events, key=lambda e: (e[0], e[1])):
        body += vlq(tick - last) + data
        last = tick
    body += vlq(end_tick - last) + b"\xff\x2f\x00"
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, DIVISION)
    path.write_bytes(header + b"MTrk" + struct.pack(">I", len(body)) + bytes(body))


def preset_select():
    # Bank select MSB and LSB, then program 0, on channel 1, at tick 0.
    return [(0, 0, bytes([0xB0, 0, 0])), (0, 0, bytes([0xB0, 32, 0])),
            (0, 0, bytes([0xC0, 0]))]


def write_clip(out: Path, name: str, velocities) -> int:
    events, notes = preset_select(), []
    start = GRID_MS
    for low, high, root in RANGES:
        key = key_in(low, high)
        for velocity in velocities:
            on, off = start, start + HOLD_MS
            events.append((on, 2, bytes([0x90, key, velocity])))
            events.append((off, 1, bytes([0x80, key, 0])))
            notes.append({"index": len(notes), "key": key, "velocity": velocity,
                          "range": [low, high], "root": root, "on_ms": on, "off_ms": off})
            start += SLOT_MS
    end = start + GRID_MS
    write_midi(out / f"{name}.mid", events, end)
    (out / f"{name}.json").write_text(json.dumps(
        {"slot_ms": SLOT_MS, "hold_ms": HOLD_MS, "end_ms": end, "notes": notes}, indent=1),
        encoding="utf-8")
    return len(notes)


def main() -> None:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    count = write_clip(out, "clip", VELOCITIES)
    low = write_clip(out, "low", LOW_VELOCITIES)

    events = preset_select() + [(0, 2, bytes([0x90, 72, 105])), (250, 1, bytes([0x80, 72, 0]))]
    write_midi(out / "preview.mid", events, 250)
    print(f"clip: {count} notes; low: {low} notes; preview: 0.25 s")


if __name__ == "__main__":
    main()
