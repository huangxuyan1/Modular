"""Throwaway SoundFont 2 census for map ticket 09 (which part of the format the node reads).

Not part of the build. Standard library only. Reads the pdta hydra of one SoundFont 2 file
and counts, over every preset, which generators, modulators, sample types, sample modes and
loops a reader meets: the facts behind "which part must a reusable node honour". It never
reads or prints sample data.

Usage:  python -I PROTOTYPE_census.py <file.sf2>

The file is untrusted data: every read is bounds-checked, and nothing in it is executed.
"""

import collections
import struct
import sys

GEN_NAMES = [
  "startAddrsOffset", "endAddrsOffset", "startloopAddrsOffset", "endloopAddrsOffset",
  "startAddrsCoarseOffset", "modLfoToPitch", "vibLfoToPitch", "modEnvToPitch",
  "initialFilterFc", "initialFilterQ", "modLfoToFilterFc", "modEnvToFilterFc",
  "endAddrsCoarseOffset", "modLfoToVolume", "unused1", "chorusEffectsSend",
  "reverbEffectsSend", "pan", "unused2", "unused3", "unused4", "delayModLFO",
  "freqModLFO", "delayVibLFO", "freqVibLFO", "delayModEnv", "attackModEnv",
  "holdModEnv", "decayModEnv", "sustainModEnv", "releaseModEnv", "keynumToModEnvHold",
  "keynumToModEnvDecay", "delayVolEnv", "attackVolEnv", "holdVolEnv", "decayVolEnv",
  "sustainVolEnv", "releaseVolEnv", "keynumToVolEnvHold", "keynumToVolEnvDecay",
  "instrument", "reserved1", "keyRange", "velRange", "startloopAddrsCoarseOffset",
  "keynum", "velocity", "initialAttenuation", "reserved2", "endloopAddrsCoarseOffset",
  "coarseTune", "fineTune", "sampleID", "sampleModes", "reserved3", "scaleTuning",
  "exclusiveClass", "overridingRootKey", "unused5", "endOper",
]
INSTRUMENT, KEY_RANGE, VEL_RANGE, SAMPLE_ID = 41, 43, 44, 53
SAMPLE_MODES, EXCLUSIVE_CLASS, ROOT_KEY = 54, 57, 58
UNSIGNED_GENS = {41, 53, 54, 57}
# Generators the 2.04 spec allows only at the instrument level.
INSTRUMENT_ONLY = {0, 1, 2, 3, 4, 12, 45, 46, 47, 50, 54, 57, 58}
PHDR, PBAG, PMOD, PGEN = 38, 4, 10, 4
INST, IBAG, IMOD, IGEN, SHDR = 22, 4, 10, 4, 46


def fail(message):
  raise SystemExit("error: " + message)


def read_chunks(data, start, end):
  chunks, pos = [], start
  while pos + 8 <= end:
    cid = data[pos:pos + 4]
    (size,) = struct.unpack_from("<I", data, pos + 4)
    if pos + 8 + size > end:
      fail(f"chunk {cid!r} at {pos} overruns its parent")
    chunks.append((cid, pos + 8, size))
    pos += 8 + size + (size & 1)
  return chunks


def list_chunks(data, chunks, kind):
  for cid, off, size in chunks:
    if cid == b"LIST" and size >= 4 and data[off:off + 4] == kind:
      return read_chunks(data, off + 4, off + size)
  fail(f"no LIST {kind!r}")


def records(data, chunk, width, fmt):
  _, off, size = chunk
  if size % width:
    fail(f"chunk size {size} is not a multiple of {width}")
  return [struct.unpack_from(fmt, data, off + i * width) for i in range(size // width)]


def name(raw):
  return raw.split(b"\0", 1)[0].decode("latin-1")


def gen_value(oper, amount):
  if oper in (KEY_RANGE, VEL_RANGE):
    return (amount & 0xFF, amount >> 8)
  if oper in UNSIGNED_GENS:
    return amount
  return amount - 0x10000 if amount & 0x8000 else amount


def zones(bags, gens, mods, first, last, terminal):
  """[(is_global, ordered [(oper, value)], [mods])] for bags first..last-1."""
  out = []
  for b in range(first, last):
    if b + 1 >= len(bags):
      fail("bag index out of range")
    g0, m0 = bags[b]
    g1, m1 = bags[b + 1]
    if not (g0 <= g1 <= len(gens) and m0 <= m1 <= len(mods)):
      fail("bag points outside its generator or modulator list")
    zone_gens = [(o, gen_value(o, a)) for o, a in gens[g0:g1]]
    order = [o for o, _ in zone_gens]
    is_terminal = bool(order) and order[-1] == terminal
    is_global = not is_terminal and b == first and last - first > 1
    out.append((is_terminal, is_global, zone_gens, mods[m0:m1]))
  return out


def mod_text(m):
  src, dest, amount, amt_src, trans = m
  dest_name = GEN_NAMES[dest] if dest < len(GEN_NAMES) else f"dest{dest}"
  return (f"src=0x{src:04X} dest={dest_name} amount={amount} "
          f"amtSrc=0x{amt_src:04X} trans={trans}")


def main(argv):
  if len(argv) != 2:
    fail(__doc__)
  with open(argv[1], "rb") as f:
    data = f.read()
  if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"sfbk":
    fail("not a RIFF sfbk file")
  (riff_size,) = struct.unpack_from("<I", data, 4)
  top = read_chunks(data, 12, min(len(data), 8 + riff_size))
  info = {cid: data[off:off + size] for cid, off, size in list_chunks(data, top, b"INFO")}
  ifil = struct.unpack_from("<HH", info[b"ifil"]) if b"ifil" in info else None
  sdta = {cid: size for cid, _, size in list_chunks(data, top, b"sdta")}
  smpl_points = sdta.get(b"smpl", 0) // 2
  pdta = {cid: (cid, off, size) for cid, off, size in list_chunks(data, top, b"pdta")}
  phdr = records(data, pdta[b"phdr"], PHDR, "<20sHHHIII")
  pbag = records(data, pdta[b"pbag"], PBAG, "<HH")
  pmod = records(data, pdta[b"pmod"], PMOD, "<HHhHH")
  pgen = records(data, pdta[b"pgen"], PGEN, "<HH")
  inst = records(data, pdta[b"inst"], INST, "<20sH")
  ibag = records(data, pdta[b"ibag"], IBAG, "<HH")
  imod = records(data, pdta[b"imod"], IMOD, "<HHhHH")
  igen = records(data, pdta[b"igen"], IGEN, "<HH")
  shdr = records(data, pdta[b"shdr"], SHDR, "<20sIIIIIBbHH")
  print(f"ifil {ifil}, sm24 {'present' if b'sm24' in sdta else 'absent'}, "
        f"{len(phdr) - 1} presets, {len(inst) - 1} instruments, {len(shdr) - 1} samples")
  print(f"pmod records {len(pmod) - 1}, imod records {len(imod) - 1} (each list ends with a "
        f"terminal record)")

  # Group presets: General MIDI melodic (bank 0), variation banks (1-127), percussion (128).
  groups = collections.OrderedDict(
    [("bank 0", []), ("banks 1-127", []), ("bank 128", [])])
  for p in range(len(phdr) - 1):
    bank = phdr[p][2]
    key = "bank 0" if bank == 0 else ("bank 128" if bank == 128 else "banks 1-127")
    groups[key].append(p)
  for key, ps in groups.items():
    print(f"{key}: {len(ps)} presets")

  wrong_level = collections.Counter()
  order_violations = 0
  for key, ps in groups.items():
    preset_gen = collections.Counter()
    inst_gen = collections.Counter()
    values = collections.defaultdict(collections.Counter)
    insts_used = set()
    presets_using = collections.defaultdict(set)
    real_vel_layers = 0
    mods_seen = collections.Counter()
    for p in ps:
      pz = zones(pbag, pgen, pmod, phdr[p][3], phdr[p + 1][3], INSTRUMENT)
      vel_to_samples = collections.defaultdict(set)
      for is_terminal, is_global, gens, mods in pz:
        if not (is_terminal or is_global):
          continue
        for i, (o, v) in enumerate(gens):
          preset_gen[o] += 1
          presets_using[o].add(p)
          if o in INSTRUMENT_ONLY:
            wrong_level[GEN_NAMES[o] if o < len(GEN_NAMES) else o] += 1
          if (o == KEY_RANGE and i != 0) or (o == VEL_RANGE and i not in (0, 1)):
            order_violations += 1
        for m in mods[:]:
          mods_seen[("preset", m)] += 1
        if is_terminal:
          i = dict(gens)[INSTRUMENT]
          if i + 1 >= len(inst):
            fail("instrument index out of range")
          insts_used.add(i)
          pvel = dict(gens).get(VEL_RANGE, (0, 127))
          for t, ig, igens, imods in zones(ibag, igen, imod, inst[i][1], inst[i + 1][1],
                                           SAMPLE_ID):
            if t:
              d = dict(igens)
              vel_to_samples[(pvel, d.get(VEL_RANGE, (0, 127)))].add(d[SAMPLE_ID])
      distinct = {frozenset(s) for s in vel_to_samples.values()}
      if len(distinct) > 1:
        real_vel_layers += 1
    for i in sorted(insts_used):
      for is_terminal, is_global, gens, mods in zones(ibag, igen, imod, inst[i][1],
                                                     inst[i + 1][1], SAMPLE_ID):
        if not (is_terminal or is_global):
          continue
        for idx, (o, v) in enumerate(gens):
          inst_gen[o] += 1
          if o not in (SAMPLE_ID, KEY_RANGE, VEL_RANGE):
            values[o][v] += 1
          if (o == KEY_RANGE and idx != 0) or (o == VEL_RANGE and idx not in (0, 1)):
            order_violations += 1
        for m in mods:
          mods_seen[("instrument", m)] += 1
    print(f"\n== {key}: {len(ps)} presets, {len(insts_used)} instruments; presets whose "
          f"velocity ranges pick different samples: {real_vel_layers}")
    print("  generator: preset zones / instrument zones setting it")
    for o in sorted(set(preset_gen) | set(inst_gen)):
      label = GEN_NAMES[o] if o < len(GEN_NAMES) else f"gen{o}"
      vals = values.get(o)
      shown = ""
      if vals and o not in (ROOT_KEY,):
        ks = sorted(vals)
        shown = f"; values {ks if len(ks) <= 6 else ks[:3] + ['...'] + ks[-3:]}"
      print(f"    {o:2d} {label}: {preset_gen[o]} / {inst_gen[o]}{shown}")
    if mods_seen:
      print("  modulators:")
      for (level, m), n in sorted(mods_seen.items()):
        print(f"    {level}: {mod_text(m)} x{n}")
    else:
      print("  modulators: none")

  print(f"\ninstrument-only generators found at the preset level: {dict(wrong_level) or 'none'}")
  print(f"keyRange/velRange order violations: {order_violations}")

  # Samples.
  types = collections.Counter()
  rates = collections.Counter()
  pitch255 = corr_nonzero = out_of_bounds = 0
  for s in range(len(shdr) - 1):
    _, start, end, sl, el, rate, pitch, corr, link, stype = shdr[s]
    types[f"0x{stype:04X}"] += 1
    rates[rate] += 1
    pitch255 += pitch > 127
    corr_nonzero += corr != 0
    out_of_bounds += not (0 <= start <= end <= smpl_points)
  print(f"\nsample types: {dict(types)} (1 mono, 2 right, 4 left, 8 linked, 0x8000 ROM, "
        f"0x10 compressed)")
  print(f"sample rates: {dict(sorted(rates.items()))}")
  print(f"byOriginalPitch above 127: {pitch255}; chPitchCorrection non-zero: {corr_nonzero}; "
        f"samples outside smpl: {out_of_bounds}")

  # Loops as zones play them (header points plus the zone's loop offsets).
  short = guard = outside = 0
  modes = collections.Counter()
  pair_pitch_mismatch = 0
  zone_count = 0
  for i in range(len(inst) - 1):
    izs = zones(ibag, igen, imod, inst[i][1], inst[i + 1][1], SAMPLE_ID)
    glob = {}
    sided = collections.defaultdict(list)
    for is_terminal, is_global, gens, mods in izs:
      if is_global:
        glob = dict(gens)
        continue
      if not is_terminal:
        continue
      zone_count += 1
      g = dict(glob)
      g.update(dict(gens))
      mode = g.get(SAMPLE_MODES, 0)
      modes[mode] += 1
      s = g[SAMPLE_ID]
      _, start, end, sl, el, rate, pitch, corr, link, stype = shdr[s]
      if mode in (1, 3):
        sl2 = sl + g.get(2, 0) + 32768 * g.get(45, 0)
        el2 = el + g.get(3, 0) + 32768 * g.get(50, 0)
        if not (start <= sl2 < el2 <= end):
          outside += 1
        elif el2 - sl2 < 32:
          short += 1
        elif sl2 - start < 8 or end - el2 < 8:
          guard += 1
      if stype & 0x6:
        root = g.get(ROOT_KEY, -1)
        root = root if 0 <= root <= 127 else (pitch if pitch <= 127 else 60)
        tune = (root, g.get(51, 0), g.get(52, 0), g.get(56, 100), corr, rate)
        sided[(g.get(KEY_RANGE, (0, 127)), g.get(VEL_RANGE, (0, 127)))].append(
          (stype & 0x6, tune))
    for side in sided.values():
      lefts = {t for k, t in side if k == 4}
      rights = {t for k, t in side if k == 2}
      if lefts and rights and lefts != rights:
        pair_pitch_mismatch += 1
  print(f"\ninstrument zones: {zone_count}; sampleModes: {dict(sorted(modes.items()))}")
  print(f"looping zones with the loop outside its sample: {outside}; loop under 32 points: "
        f"{short}; fewer than 8 points outside the loop: {guard}")
  print(f"left/right zones over the same ranges whose pitch inputs differ (would drift if "
        f"played as independent voices): {pair_pitch_mismatch}")


if __name__ == "__main__":
  main(sys.argv)
