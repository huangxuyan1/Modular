"""PROTOTYPE, throwaway: soundfont-node ticket 07. Compares the node's renders of the clip
with FluidSynth's, note by note, against the spec's thresholds.

    python -I PROTOTYPE_compare.py <clip.json> <node prefix> <fluidsynth prefix>

Reads <prefix>-44100.wav and <prefix>-48000.wav (32-bit float, stereo) and prints Markdown.
Needs numpy. For every note of the clip, at each rate:

- Onset: the first frame of the note's slot where either channel is non-zero, in each render.
  The threshold is FluidSynth's 64-frame control block: |node - FluidSynth| < 64 frames.
- Pitch: both renders play the same sample points, so a pitch difference shows as a lag that
  grows with time. While the note is held, windows of 2,048 frames every 25 ms are
  cross-correlated with FluidSynth (both channels summed), each peak refined by a parabola,
  and a line fitted to the lags: a slope b means the node's increment is (1 + b) times
  FluidSynth's, 1200 log2(1 + b) cents. Threshold: within +-1 cent. The first window holds
  the attack and is searched within 8 frames of the onset difference; each later one within
  5 frames of the last, so a periodic waveform cannot slip by a period (key 106's is under
  12 frames). The fitted lag at the onset is printed too.
- Level: the RMS of both channels in 10 ms windows from each render's own onset, in dB. The
  windows counted are those where FluidSynth's is within 60 dB of its peak window. One global
  offset, the median of every counted window's difference over all notes and both rates, is
  removed. Threshold: every counted window within +-1 dB.

It also prints, to explain a failure: each note's level difference early in the note (20 to
100 ms), and the slopes, in dB per second, of both renders' decay while held and of their
release, fitted over the counted windows.
"""

import json
import math
import sys
from pathlib import Path

import numpy as np

RATES = (44100, 48000)
BLOCK = 64
PITCH_CENTS = 1.0
LEVEL_DB = 1.0
FLOOR_DB = 60.0


def read_wav(path: Path):
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    pos, fmt, samples = 12, None, None
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], int.from_bytes(data[pos + 4:pos + 8], "little")
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = (int.from_bytes(body[0:2], "little"), int.from_bytes(body[2:4], "little"),
                   int.from_bytes(body[4:8], "little"), int.from_bytes(body[14:16], "little"))
        elif tag == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, bits = fmt
    if tag == 0xFFFE:  # WAVE_FORMAT_EXTENSIBLE: the subformat's first two bytes
        tag = 3
    if tag != 3 or bits != 32 or channels != 2:
        raise ValueError(f"{path}: expected stereo 32-bit float, got {fmt}")
    frames = np.frombuffer(samples, dtype="<f4").reshape(-1, 2).astype(np.float64)
    return rate, frames


def onset(x: np.ndarray, start: int, end: int) -> int:
    nz = np.flatnonzero(np.any(x[start:end] != 0, axis=1))
    return start + int(nz[0]) if nz.size else -1


def lag_at(node, fluid, at, guess, width, reach):
    """How many frames FluidSynth's waveform lags the node's at node frame `at`, searched
    within `reach` frames of `guess`, with the peak's normalised correlation."""
    x = node[at:at + width]
    base = at + int(round(guess)) - reach
    y = fluid[base:base + width + 2 * reach]
    corr = sum(np.correlate(y[:, c], x[:, c], "valid") for c in (0, 1))
    k = int(np.argmax(corr))
    shift = 0.0
    if 0 < k < len(corr) - 1:
        a, b, c = corr[k - 1], corr[k], corr[k + 1]
        shift = 0.5 * (a - c) / (a - 2 * b + c)
    norm = math.sqrt(float(np.sum(x * x)) * float(np.sum(y[k:k + width] ** 2)))
    return base + k + shift - at, float(corr[k]) / norm if norm > 0 else 0.0


def rms_db(x: np.ndarray, start: int, count: int, width: int) -> np.ndarray:
    seg = x[start:start + count * width]
    energy = np.mean((seg * seg).reshape(count, width, 2), axis=(1, 2))
    return 10 * np.log10(np.maximum(energy, 1e-30))


def slope(t: np.ndarray, db: np.ndarray):
    return float(np.polyfit(t, db, 1)[0]) if len(t) >= 5 else float("nan")


def analyse(note, rate, node, fluid, clip):
    ms = rate / 1000
    on = round(note["on_ms"] * ms)
    off = round(note["off_ms"] * ms)
    slot_end = on + round(clip["slot_ms"] * ms)
    lo, hi = on - 512, slot_end - 512
    on_n, on_f = onset(node, lo, hi), onset(fluid, lo, hi)
    silent_before = not np.any(node[lo:on_n]) and not np.any(fluid[lo:on_f])

    # Pitch: the lag while held, every 25 ms from the onset to before the note-off. The
    # first window holds the attack and searches near the onset difference; each later one
    # searches within a few frames of the last, so a periodic waveform cannot slip a period.
    width = 2048
    taus = [round(0.025 * j * rate) for j in range(60)]
    taus = [t for t in taus if on_n + t + width < off - round(0.02 * rate)]
    fits, guess = [], on_f - on_n
    for j, t in enumerate(taus):
        fits.append(lag_at(node, fluid, on_n + t, guess, width, 8 if j == 0 else 5))
        guess = fits[-1][0]
    lags = np.array([f[0] for f in fits])
    b, a = np.polyfit(np.array(taus, dtype=float), lags, 1)
    cents = 1200 * math.log2(1 + b)
    similarity = min(f[1] for f in fits)

    # Level: 10 ms windows from each render's own onset, to the end of the slot.
    w = round(0.010 * rate)
    count = (hi - max(on_n, on_f)) // w
    n_db, f_db = rms_db(node, on_n, count, w), rms_db(fluid, on_f, count, w)
    counted = f_db >= f_db.max() - FLOOR_DB
    t = (np.arange(count) + 0.5) * w / rate  # window centres, seconds from onset
    held = t < (off - on) / rate
    return {
        "key": note["key"], "velocity": note["velocity"], "rate": rate,
        "onset": on_f - on_n, "node_onset": on_n - on, "silent_before": silent_before,
        "cents": cents, "lag0": a, "similarity": similarity,
        "diff": (n_db - f_db)[counted], "t": t[counted], "held": held[counted],
        "n_db": n_db[counted], "f_db": f_db[counted],
        "peak_f": float(f_db.max()), "early": float(np.mean((n_db - f_db)[2:10])),
    }


def fmt(x, digits=2):
    return "nan" if x != x else f"{x:+.{digits}f}"


def per_key(results, rate):
    """One row per key: the extremes over its velocities."""
    rs = [r for r in results if r["rate"] == rate]
    print(f"### Per key, {rate} Hz\n")
    print("Each row covers the key's velocities. \"Early\" is at the lowest and the highest "
          "velocity; the release fall is the median of node / FluidSynth.\n")
    print("| Key | Onset (frames) | Pitch (cents) | Early, low / high (dB) | Worst (dB) "
          "| Windows over 1 dB | Release fall |")
    print("| --- | --- | --- | --- | --- | --- | --- |")
    for key in sorted({r["key"] for r in rs}):
        k = sorted((r for r in rs if r["key"] == key), key=lambda r: r["velocity"])
        worst = max((r["worst"] for r in k), key=abs)
        ratios = [r["release"][0] / r["release"][1] for r in k
                  if r["release"][1] == r["release"][1] and r["release"][1] < -20]
        fall = f"{float(np.median(ratios)):.4f}" if ratios else "n/a"
        print(f"| {key} | {', '.join(str(o) for o in sorted({r['onset'] for r in k}))} "
              f"| {min(r['cents'] for r in k):+.4f} to {max(r['cents'] for r in k):+.4f} "
              f"| {k[0]['early_rel']:+.2f} / {k[-1]['early_rel']:+.2f} | {worst:+.2f} "
              f"| {sum(r['fails'] for r in k)} of {sum(r['windows'] for r in k)} | {fall} |")
    print()


def main() -> None:
    clip = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    node_prefix, fluid_prefix = sys.argv[2], sys.argv[3]
    results = []
    for rate in RATES:
        r_n, node = read_wav(Path(f"{node_prefix}-{rate}.wav"))
        r_f, fluid = read_wav(Path(f"{fluid_prefix}-{rate}.wav"))
        if r_n != rate or r_f != rate:
            raise ValueError(f"rate mismatch: {r_n}, {r_f}, expected {rate}")
        length = min(len(node), len(fluid))
        print(f"<!-- {rate} Hz: node {len(node)} frames, FluidSynth {len(fluid)} frames -->")
        node, fluid = node[:length], fluid[:length]
        for note in clip["notes"]:
            results.append(analyse(note, rate, node, fluid, clip))

    offset = float(np.median(np.concatenate([r["diff"] for r in results])))
    print(f"# Node against FluidSynth: {len(clip['notes'])} notes at {len(RATES)} rates\n")
    print(f"Global gain offset (node minus FluidSynth, median of every counted window): "
          f"**{offset:+.3f} dB**\n")
    for r in results:
        err = r["diff"] - offset
        i = int(np.argmax(np.abs(err)))
        r["worst"], r["worst_t"] = float(err[i]), float(r["t"][i])
        r["fails"] = int(np.sum(np.abs(err) > LEVEL_DB))
        r["windows"] = len(err)
        r["early_rel"] = r["early"] - offset
        # Fall rates in dB/s over the counted windows, while held after 0.2 s, and released.
        hold = r["held"] & (r["t"] > 0.2)
        rel = ~r["held"] & (r["t"] > r["t"][r["held"]].max() + 0.03)
        r["decay"] = (slope(r["t"][hold], r["n_db"][hold]), slope(r["t"][hold], r["f_db"][hold]))
        r["release"] = (slope(r["t"][rel], r["n_db"][rel]), slope(r["t"][rel], r["f_db"][rel]))
        r["release_end"] = float(r["t"][rel].max() - r["t"][r["held"]].max()) if rel.any() else 0

    print("| Rate | Notes | Onset within 64 frames | Pitch within 1 cent | Level within 1 dB |")
    print("| --- | --- | --- | --- | --- |")
    for rate in RATES:
        rs = [r for r in results if r["rate"] == rate]
        on_ok = sum(abs(r["onset"]) < BLOCK and r["silent_before"] for r in rs)
        p_ok = sum(abs(r["cents"]) <= PITCH_CENTS for r in rs)
        l_ok = sum(r["fails"] == 0 for r in rs)
        print(f"| {rate} | {len(rs)} | {on_ok} | {p_ok} | {l_ok} |")
    print()
    for rate in RATES:
        rs = [r for r in results if r["rate"] == rate]
        print(f"- {rate} Hz: the node's first sound {sorted(set(r['node_onset'] for r in rs))} "
              f"frames after its event; onset differences (FluidSynth later) "
              f"{sorted(set(r['onset'] for r in rs))} frames; waveform lag at the onset "
              f"{min(r['lag0'] for r in rs):.2f} to {max(r['lag0'] for r in rs):.2f} frames; "
              f"pitch {min(r['cents'] for r in rs):+.4f} to {max(r['cents'] for r in rs):+.4f} "
              f"cents; waveform similarity at least {min(r['similarity'] for r in rs):.4f}; "
              f"level error {min(r['worst'] for r in rs):+.2f} to "
              f"{max(r['worst'] for r in rs):+.2f} dB; windows over 1 dB: "
              f"{sum(r['fails'] for r in rs)} of {sum(r['windows'] for r in rs)}")
    ratios = [r["release"][0] / r["release"][1] for r in results
              if r["release"][1] == r["release"][1] and r["release"][1] < -20]
    decays = [r["decay"][0] / r["decay"][1] for r in results
              if r["decay"][1] == r["decay"][1] and r["decay"][1] < -5]
    if ratios:
        print(f"- Release fall rate, node over FluidSynth: median {np.median(ratios):.4f} "
              f"({min(ratios):.4f} to {max(ratios):.4f}, {len(ratios)} notes)")
    if decays:
        print(f"- Held decay fall rate, node over FluidSynth (notes falling over 5 dB/s): median "
              f"{np.median(decays):.4f} ({min(decays):.4f} to {max(decays):.4f}, "
              f"{len(decays)} notes)")
    print()

    for rate in RATES:
        per_key(results, rate)
    for rate in RATES:
        print(f"## {rate} Hz\n")
        print("Level errors are after the global offset; \"early\" is the node's level against "
              "FluidSynth's, offset removed, 20 to 100 ms after the onset; the worst window's "
              "time is from the onset (the note-off is at 1.28 s). Fall rates are node / "
              "FluidSynth in dB/s.\n")
        print("| Key | Vel | Onset (frames) | Lag (frames) | Pitch (cents) | Early (dB) "
              "| Worst (dB) | at (s) | Windows over 1 dB | Held fall | Release fall |")
        print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
        for r in (r for r in results if r["rate"] == rate):
            print(f"| {r['key']} | {r['velocity']} | {r['onset']} | {r['lag0']:.2f} "
                  f"| {fmt(r['cents'], 4)} "
                  f"| {fmt(r['early_rel'])} | {fmt(r['worst'])} | {r['worst_t']:.2f} "
                  f"| {r['fails']} of {r['windows']} "
                  f"| {r['decay'][0]:.1f} / {r['decay'][1]:.1f} "
                  f"| {r['release'][0]:.1f} / {r['release'][1]:.1f} |")
        print()


if __name__ == "__main__":
    main()
