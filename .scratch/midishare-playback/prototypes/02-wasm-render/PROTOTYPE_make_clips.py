"""PROTOTYPE, throwaway: writes the probe's test clips. midishare-playback ticket 02.

    python PROTOTYPE_make_clips.py <output folder>

Standard library only. Writes, into the output folder:

- worst.abc / worst.mid  The worst case a midishare clip can hold, by assumption: a busy
                         two-voice tune in the shape midishare's MIDI import writes
                         (L:1/8, M:4/4, K:C), at its example tempo Q:1/4=90, filled bar by bar
                         until one more bar would pass 5000 characters of ABC. Voice 1 is a
                         melody of eighths, sixteenth pairs, quarters, dyads and ties across
                         bar lines; voice 2 is chords. Every 16th bar is a climax that holds
                         a six-note chord under melody triads, so nine notes sound and the
                         voice allocator (eight voices) must steal.
- short.abc / short.mid  An ordinary clip: midishare's example tune (four bars, Dm Bb C A),
                         its chord symbols played as abcjs plays them (bass on beats 1 and 3,
                         the chord on 2 and 4).
- long.abc / long.mid    The longest a clip gets: one whole-note triad per bar, the fewest
                         characters per second of music. 5000 characters hold about 23 minutes
                         at Q:1/4=90; the MIDI keeps only the bars under midi_cli's ten-minute
                         limit, which this probe's render keeps too.
- preview.mid            One note preview: an eighth note (C5) at 120 BPM, 0.25 s.

The MIDI is written from the same note list as the ABC text, not by parsing the ABC, so
the ABC file is the evidence for the character count only. Pitches stay in midishare's piano
roll range (MIDI 36 to 95).
"""

import random
import struct
import sys
from pathlib import Path

PPQ = 480
EIGHTH = PPQ // 2
LIMIT = 5000

NAMES = {0: "C", 2: "D", 4: "E", 5: "F", 7: "G", 9: "A", 11: "B"}
DIATONIC = [p for p in range(36, 96) if p % 12 in NAMES]


def abc_pitch(pitch: int) -> str:
    """Diatonic MIDI pitch to ABC in K:C (no accidentals, so none carry across a bar)."""
    name, octave = NAMES[pitch % 12], pitch // 12 - 5  # octave 0 is C4 (60) to B4
    if octave >= 1:
        return name.lower() + "'" * (octave - 1)
    return name + "," * (-octave)


def abc_length(eighths: float) -> str:
    return {0.5: "/", 1: "", 2: "2", 3: "3", 4: "4", 6: "6", 8: "8"}[eighths]


def chord_text(pitches, eighths) -> str:
    body = "".join(abc_pitch(p) for p in pitches)
    return ("[" + body + "]" if len(pitches) > 1 else body) + abc_length(eighths)


# I vi IV V, voiced low; the first pitch is the bass.
PROGRESSION = [[48, 55, 64, 67], [45, 52, 60, 64], [41, 53, 57, 60], [43, 50, 59, 62]]


def render_abc(header, bars):
    """Four bars to a line, each line written once per voice, as abcjs needs voices in step."""
    text = header
    for i in range(0, len(bars), 4):
        group = bars[i:i + 4]
        text += "[V:1] " + " ".join(m for m, _ in group) + "\n"
        text += "[V:2] " + " ".join(a for _, a in group) + "\n"
    return text


def worst_case():
    rng = random.Random(20261124)
    header = "X:1\nT:Worst case\nM:4/4\nL:1/8\nQ:1/4=90\nK:C\n"
    bars, notes = [], []  # bars: (melody text, chords text); notes: (tick, ticks, pitch, channel)
    tie = None  # (pitch, start tick) of a melody note tied into the next bar
    while True:
        bar = len(bars)
        start = bar * 8 * EIGHTH
        climax = bar % 16 == 15
        bar_notes, melody, t, held, new_tie = [], [], 0, tie, None
        # Voice 1: the melody.
        while t < 8:
            roll = rng.random()
            if held is not None:  # finish the tied note
                p, began = held
                melody.append(abc_pitch(p) + "2")
                bar_notes.append((began, start + 2 * EIGHTH - began, p, 0))
                held, t = None, 2
            elif climax or roll < 0.15:  # a dyad, one eighth; a triad in a climax
                lo = rng.choice([p for p in DIATONIC if 67 <= p <= 79])
                at = DIATONIC.index(lo)
                stack = [DIATONIC[at + step] for step in ((0, 2, 4) if climax else (0, 2))]
                melody.append(chord_text(stack, 1))
                for p in stack:
                    bar_notes.append((start + t * EIGHTH, EIGHTH, p, 0))
                t += 1
                if climax and t == 6:  # tie the climax's last quarter into the next bar
                    p = rng.choice([p for p in DIATONIC if 72 <= p <= 84])
                    melody.append(abc_pitch(p) + "2-")
                    new_tie, t = (p, start + 6 * EIGHTH), 8
            elif roll < 0.30:  # two sixteenths
                for half in (0, EIGHTH // 2):
                    p = rng.choice([p for p in DIATONIC if 60 <= p <= 84])
                    melody.append(abc_pitch(p) + "/")
                    bar_notes.append((start + t * EIGHTH + half, EIGHTH // 2, p, 0))
                t += 1
            elif roll < 0.40 and t <= 6:  # a quarter, sometimes tied over the bar line
                p = rng.choice([p for p in DIATONIC if 60 <= p <= 84])
                if t == 6 and rng.random() < 0.5:
                    melody.append(abc_pitch(p) + "2-")
                    new_tie = (p, start + t * EIGHTH)
                else:
                    melody.append(abc_pitch(p) + "2")
                    bar_notes.append((start + t * EIGHTH, 2 * EIGHTH, p, 0))
                t += 2
            else:  # an eighth
                p = rng.choice([p for p in DIATONIC if 60 <= p <= 84])
                melody.append(abc_pitch(p))
                bar_notes.append((start + t * EIGHTH, EIGHTH, p, 0))
                t += 1
        # Voice 2: chords over the bass of I vi IV V.
        chord = PROGRESSION[bar % 4]
        if climax:  # six notes held for the whole bar
            pitches = sorted({*chord, chord[2] + 12, chord[3] + 12})
            accomp = [chord_text(pitches, 8)]
            bar_notes += [(start, 8 * EIGHTH, p, 1) for p in pitches]
        elif bar % 2 == 0:  # two half-note triads
            accomp = [chord_text(chord[1:], 4)] * 2
            bar_notes += [(start + h * EIGHTH, 4 * EIGHTH, p, 1) for h in (0, 4) for p in chord[1:]]
        else:  # bass on beats 1 and 3, the triad on 2 and 4
            accomp = []
            for beat in range(4):
                pitches = [chord[0]] if beat % 2 == 0 else chord[1:]
                accomp.append(chord_text(pitches, 2))
                bar_notes += [(start + beat * 2 * EIGHTH, 2 * EIGHTH, p, 1) for p in pitches]
        candidate = bars + [(" ".join(melody) + "|", " ".join(accomp) + "|")]
        if len(render_abc(header, candidate)) > LIMIT:
            break
        bars, tie = candidate, new_tie
        notes += bar_notes
    if tie is not None:  # a tie left open by the last bar ends at its bar line
        p, began = tie
        notes.append((began, len(bars) * 8 * EIGHTH - began, p, 0))
    return render_abc(header, bars), notes, 666667, len(bars)


def short_clip():
    # midishare's example: "Dm"D2 F2 A2 d2 | "Bb"B,2 D2 F2 B2 | "C"C2 E2 G2 c2 | "A"A,2 ^C2 E2 A2 |]
    text = ('X:1\nT:Example\nM:4/4\nL:1/8\nQ:1/4=90\nK:Dm\n'
            '"Dm"D2 F2 A2 d2 | "Bb"B,2 D2 F2 B2 | "C"C2 E2 G2 c2 | "A"A,2 ^C2 E2 A2 |]')
    melody = [[62, 65, 69, 74], [58, 62, 65, 70], [60, 64, 67, 72], [57, 61, 64, 69]]
    chords = [(38, [50, 53, 57]), (34 + 12, [46, 50, 53]), (36, [48, 52, 55]), (33 + 12, [45, 49, 52])]
    notes = []
    for bar in range(4):
        start = bar * 4 * PPQ
        for beat, p in enumerate(melody[bar]):
            notes.append((start + beat * PPQ, PPQ, p, 0))
        bass, triad = chords[bar]
        for beat in range(4):
            for p in ([bass] if beat % 2 == 0 else triad):
                notes.append((start + beat * PPQ, PPQ, max(p, 36), 1))
    return text, notes, 666667


def long_clip():
    """The longest a clip gets: one whole-note triad per bar, the fewest characters per second."""
    header = "X:1\nT:Long\nM:4/4\nL:1/8\nQ:1/4=90\nK:C\n"
    seconds_per_bar = 8 * EIGHTH * 666667 / (PPQ * 1_000_000)
    bars, kept = [], header
    while True:
        candidate = bars + [chord_text(PROGRESSION[len(bars) % 4][1:], 8) + "|"]
        text = header + "".join(" ".join(candidate[i:i + 4]) + "\n"
                                for i in range(0, len(candidate), 4))
        if len(text) > LIMIT:
            break
        bars, kept = candidate, text
    # midi_cli refuses more than ten minutes; keep the bars that fit under that limit.
    keep = int(600 // seconds_per_bar)
    while keep * seconds_per_bar > 600:
        keep -= 1
    notes = [(bar * 8 * EIGHTH, 8 * EIGHTH, p, 0)
             for bar in range(keep) for p in PROGRESSION[bar % 4][1:]]
    return kept, notes, 666667, len(bars), keep


def preview_clip():
    return [(0, EIGHTH, 72, 0)], 500000


def vlq(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def track(events, end_tick) -> bytes:
    """events: (tick, order, bytes); note-offs sort before note-ons at one tick."""
    body, now = b"", 0
    for tick, _, data in sorted(events, key=lambda e: (e[0], e[1])):
        body += vlq(tick - now) + data
        now = tick
    body += vlq(end_tick - now) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(body)) + body


def write_midi(path: Path, notes, tempo: int) -> None:
    end = max(start + length for start, length, _, _ in notes)
    conductor = [(0, 0, b"\xff\x51\x03" + tempo.to_bytes(3, "big")),
                 (0, 0, b"\xff\x58\x04\x04\x02\x18\x08")]
    tracks = [track(conductor, end)]
    for channel in sorted({n[3] for n in notes}):
        events = []
        for start, length, pitch, ch in notes:
            if ch != channel:
                continue
            events.append((start, 1, bytes([0x90 | ch, pitch, 100])))
            events.append((start + length, 0, bytes([0x80 | ch, pitch, 0])))
        tracks.append(track(events, end))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), PPQ)
    path.write_bytes(header + b"".join(tracks))


def describe(name, notes, tempo, text=None):
    end = max(s + l for s, l, _, _ in notes)
    seconds = end * tempo / (PPQ * 1_000_000)
    # The most notes sounding at once, from the note list.
    edges = sorted([(s, 1) for s, l, _, _ in notes] + [(s + l, -1) for s, l, _, _ in notes],
                   key=lambda e: (e[0], e[1]))
    now = peak = 0
    for _, step in edges:
        now += step
        peak = max(peak, now)
    chars = f", {len(text)} characters of ABC" if text is not None else ""
    print(f"{name}: {len(notes)} notes, {seconds:.3f} s, at most {peak} sounding{chars}")


def main() -> None:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    text, notes, tempo, bars = worst_case()
    (out / "worst.abc").write_text(text, encoding="utf-8", newline="\n")
    write_midi(out / "worst.mid", notes, tempo)
    describe(f"worst ({bars} bars)", notes, tempo, text)
    text, notes, tempo = short_clip()
    (out / "short.abc").write_text(text, encoding="utf-8", newline="\n")
    write_midi(out / "short.mid", notes, tempo)
    describe("short", notes, tempo, text)
    text, notes, tempo, bars, keep = long_clip()
    (out / "long.abc").write_text(text, encoding="utf-8", newline="\n")
    write_midi(out / "long.mid", notes, tempo)
    whole = bars * 8 * EIGHTH * tempo / (PPQ * 1_000_000)
    print(f"long: {len(text)} characters of ABC hold {bars} bars, {whole:.1f} s; "
          f"the MIDI keeps the first {keep}")
    describe("long", notes, tempo)
    notes, tempo = preview_clip()
    write_midi(out / "preview.mid", notes, tempo)
    describe("preview", notes, tempo)


if __name__ == "__main__":
    main()
