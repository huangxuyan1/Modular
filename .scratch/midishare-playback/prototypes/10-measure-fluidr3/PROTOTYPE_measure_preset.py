"""Throwaway SoundFont 2 reader for map ticket 10 (measure the FluidR3_GM piano).

Not part of the build. Standard library only. Reads the RIFF structure and the
pdta hydra of one SoundFont 2 file, and reports one preset's zones, generators,
modulators, samples and sizes. It never prints sample data.

Usage:  python -I PROTOTYPE_measure_preset.py <file.sf2> [bank] [program] [lo] [hi]
Defaults: bank 0, program 0, keys 36..95.

The file is untrusted data: every read is bounds-checked, and nothing in it is
executed.
"""

import array
import math
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
TIMECENT_GENS = {21, 23, 25, 26, 27, 28, 30, 33, 34, 35, 36, 38}
UNSIGNED_GENS = {41, 53, 54, 57}  # also the ranges 43 and 44
INSTRUMENT, KEY_RANGE, VEL_RANGE, SAMPLE_ID = 41, 43, 44, 53
SAMPLE_MODES, OVERRIDING_ROOT_KEY = 54, 58
COARSE_TUNE, FINE_TUNE, SCALE_TUNING = 51, 52, 56
LOOP_GENS = (2, 3, 45, 50)
ZERO_PAD_POINTS = 46  # SoundFont 2.04 section 6.1: at least 46 zero points follow each sample

# SoundFont 2.04 section 8.4, as (src, dest, amount, amtSrc, trans).
DEFAULT_MODULATORS = [
  (0x0502, 48, 960, 0x0000, 0),   # velocity -> attenuation
  (0x0102, 8, -2400, 0x0000, 0),  # velocity -> filter cutoff
  (0x000D, 6, 50, 0x0000, 0),     # channel pressure -> vibrato LFO pitch
  (0x0081, 6, 50, 0x0000, 0),     # CC1 -> vibrato LFO pitch
  (0x0587, 48, 960, 0x0000, 0),   # CC7 -> attenuation
  (0x028A, 17, 1000, 0x0000, 0),  # CC10 -> pan
  (0x058B, 48, 960, 0x0000, 0),   # CC11 -> attenuation
  (0x00DB, 16, 200, 0x0000, 0),   # CC91 -> reverb send
  (0x00DD, 15, 200, 0x0000, 0),   # CC93 -> chorus send
  (0x020E, -1, 12700, 0x0010, 0),  # pitch wheel x sensitivity -> initial pitch (no generator number)
]

PHDR, PBAG, PMOD, PGEN = 38, 4, 10, 4
INST, IBAG, IMOD, IGEN, SHDR = 22, 4, 10, 4, 46


def fail(message):
  raise SystemExit("error: " + message)


def read_chunks(data, start, end):
  """Return [(id, offset, size)] for the chunks in data[start:end]."""
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


def gen_value(oper, amount_u16):
  if oper in (KEY_RANGE, VEL_RANGE):
    return (amount_u16 & 0xFF, amount_u16 >> 8)
  if oper in UNSIGNED_GENS:
    return amount_u16
  return amount_u16 - 0x10000 if amount_u16 & 0x8000 else amount_u16


def gen_text(oper, value):
  label = GEN_NAMES[oper] if oper < len(GEN_NAMES) else f"gen{oper}"
  if oper in (KEY_RANGE, VEL_RANGE):
    return f"{label}={value[0]}-{value[1]}"
  extra = ""
  if oper in TIMECENT_GENS:
    extra = f" ({2 ** (value / 1200):.4g} s)"
  elif oper in (37, 48):
    extra = f" ({value / 10:g} dB)"
  elif oper == 8:
    extra = f" ({8.176 * 2 ** (value / 1200):.0f} Hz)"
  return f"{label}={value}{extra}"


def zones(bags, gens, mods, first, last, terminal):
  """Yield (is_global, {oper: value}, [gen order], [mods]) for bags first..last-1."""
  out = []
  for b in range(first, last):
    if b + 1 >= len(bags):
      fail("bag index out of range")
    g0, m0 = bags[b]
    g1, m1 = bags[b + 1]
    zone_gens = [(o, gen_value(o, a)) for o, a in gens[g0:g1]]
    zone_mods = mods[m0:m1]
    order = [o for o, _ in zone_gens]
    is_terminal = bool(order) and order[-1] == terminal
    is_global = not is_terminal and b == first and last - first > 1
    if not is_terminal and not is_global:
      continue  # a non-first zone without its terminal generator is ignored (section 7.3)
    out.append((is_global, dict(zone_gens), order, zone_mods))
  return out


def mod_text(m):
  src, dest, amount, amt_src, trans = m
  return f"src=0x{src:04X} dest={dest} amount={amount} amtSrc=0x{amt_src:04X} trans={trans}"


def intersect(a, b):
  return (max(a[0], b[0]), min(a[1], b[1]))


def main(argv):
  if len(argv) < 2:
    fail(__doc__)
  path = argv[1]
  bank = int(argv[2]) if len(argv) > 2 else 0
  program = int(argv[3]) if len(argv) > 3 else 0
  lo_key = int(argv[4]) if len(argv) > 4 else 36
  hi_key = int(argv[5]) if len(argv) > 5 else 95

  with open(path, "rb") as f:
    data = f.read()
  if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"sfbk":
    fail("not a RIFF sfbk file")
  (riff_size,) = struct.unpack_from("<I", data, 4)
  print(f"file bytes {len(data)}, RIFF size field {riff_size}")
  top = read_chunks(data, 12, min(len(data), 8 + riff_size))

  info = list_chunks(data, top, b"INFO")
  print("INFO:")
  for cid, off, size in info:
    raw = data[off:off + size]
    if cid == b"ifil" and size == 4:
      text = "%d.%02d" % struct.unpack_from("<HH", raw)
    else:
      text = name(raw)
      if len(text) > 70:
        text = text[:70].replace("\n", " ") + f"... ({size} bytes)"
    print(f"  {cid.decode('latin-1')}: {text}")

  sdta = list_chunks(data, top, b"sdta")
  sdta_ids = {cid: size for cid, _, size in sdta}
  smpl_bytes = sdta_ids.get(b"smpl", 0)
  print(f"sdta: smpl {smpl_bytes} bytes ({smpl_bytes // 2} points); "
        f"sm24 {'present, %d bytes' % sdta_ids[b'sm24'] if b'sm24' in sdta_ids else 'absent'}")

  pdta = {cid: (cid, off, size) for cid, off, size in list_chunks(data, top, b"pdta")}
  for need in (b"phdr", b"pbag", b"pmod", b"pgen", b"inst", b"ibag", b"imod", b"igen", b"shdr"):
    if need not in pdta:
      fail(f"pdta lacks {need!r}")
  phdr = records(data, pdta[b"phdr"], PHDR, "<20sHHHIII")
  pbag = records(data, pdta[b"pbag"], PBAG, "<HH")
  pmod = records(data, pdta[b"pmod"], PMOD, "<HHhHH")
  pgen = records(data, pdta[b"pgen"], PGEN, "<HH")
  inst = records(data, pdta[b"inst"], INST, "<20sH")
  ibag = records(data, pdta[b"ibag"], IBAG, "<HH")
  imod = records(data, pdta[b"imod"], IMOD, "<HHhHH")
  igen = records(data, pdta[b"igen"], IGEN, "<HH")
  shdr = records(data, pdta[b"shdr"], SHDR, "<20sIIIIIBbHH")
  print(f"pdta: {len(phdr) - 1} presets, {len(inst) - 1} instruments, {len(shdr) - 1} samples")

  matches = [i for i in range(len(phdr) - 1) if phdr[i][2] == bank and phdr[i][1] == program]
  if len(matches) != 1:
    fail(f"expected one preset {bank}:{program}, found {len(matches)}")
  p = matches[0]
  print(f"\nPRESET {bank}:{program} '{name(phdr[p][0])}' (phdr index {p})")
  pzones = zones(pbag, pgen, pmod, phdr[p][3], phdr[p + 1][3], INSTRUMENT)

  preset_global = {}
  used_gens = {"preset": set(), "instrument": set()}
  all_mods = []
  for z, (is_global, g, order, mods) in enumerate(pzones):
    print(f"  preset zone {z}{' (global)' if is_global else ''}: " +
          ", ".join(gen_text(o, g[o]) for o in order))
    used_gens["preset"].update(order)
    for m in mods:
      print(f"    modulator {mod_text(m)}")
      all_mods.append(("preset", z, m))
    if is_global:
      preset_global = g

  # Flatten to (instrument zone) entries with effective key and velocity ranges.
  layers = []  # dicts describing each sounding (preset zone x instrument zone)
  printed = set()
  for pz, (p_is_global, pg, _, _) in enumerate(pzones):
    if p_is_global:
      continue
    pmerged = dict(preset_global)
    pmerged.update(pg)
    i = pg[INSTRUMENT]
    if i + 1 >= len(inst):
      fail("instrument index out of range")
    first_time = i not in printed
    printed.add(i)
    if first_time:
      print(f"\n  INSTRUMENT {i} '{name(inst[i][0])}' (first used by preset zone {pz})")
    else:
      print(f"  (preset zone {pz} uses instrument {i} again)")
    izones = zones(ibag, igen, imod, inst[i][1], inst[i + 1][1], SAMPLE_ID)
    inst_global = {}
    for z, (is_global, g, order, mods) in enumerate(izones):
      used_gens["instrument"].update(order)
      if first_time:
        line = ", ".join(gen_text(o, g[o]) for o in order)
        print(f"    inst zone {z}{' (global)' if is_global else ''}: {line}")
        for m in mods:
          print(f"      modulator {mod_text(m)}")
          all_mods.append(("instrument", z, m))
      if is_global:
        inst_global = g
        continue
      merged = dict(inst_global)
      merged.update(g)
      keys = intersect(merged.get(KEY_RANGE, (0, 127)), pmerged.get(KEY_RANGE, (0, 127)))
      vels = intersect(merged.get(VEL_RANGE, (0, 127)), pmerged.get(VEL_RANGE, (0, 127)))
      if keys[0] > keys[1] or vels[0] > vels[1]:
        continue
      layers.append({"inst": i, "zone": z, "gens": merged, "pgens": pmerged,
                     "keys": keys, "vels": vels, "sample": merged[SAMPLE_ID]})

  print("\nSAMPLES used by the preset:")
  sample_ids = sorted({l["sample"] for l in layers})
  for s in sample_ids:
    if s + 1 >= len(shdr):
      fail("sample index out of range")
    n, start, end, sl, el, rate, pitch, corr, link, stype = shdr[s]
    print(f"  {s:4d} '{name(n)}': points {end - start}, loop {el - sl} "
          f"(start+{sl - start}), rate {rate}, root {pitch}, corr {corr}, "
          f"type 0x{stype:04X}, link {link}")

  def sample_points(s):
    return shdr[s][2] - shdr[s][1]

  def bytes_for(ids):
    pts = sum(sample_points(s) for s in ids)
    return pts * 2, (pts + ZERO_PAD_POINTS * len(ids)) * 2, pts

  def in_range(l):
    return l["keys"][0] <= hi_key and l["keys"][1] >= lo_key

  case_all = set(sample_ids)
  case_range = {l["sample"] for l in layers if in_range(l)}

  # Stereo pairs. wSampleLink names the partner when the file sets it; when it
  # does not point back, pair a left and a right sample that one instrument plays
  # over the same key and velocity ranges.
  def mutual_link(s):
    t = shdr[s][8]
    return t if (shdr[s][9] & 0x6) and t + 1 < len(shdr) and shdr[t][8] == s and t != s else None

  partner, how = {}, {}
  for s in case_all:
    t = mutual_link(s)
    if t is not None:
      partner[s], how[s] = t, "wSampleLink"
  for a in layers:
    for b in layers:
      sa, sb = a["sample"], b["sample"]
      if (sa not in partner and shdr[sa][9] & 0x4 and shdr[sb][9] & 0x2 and a["inst"] == b["inst"]
          and a["keys"] == b["keys"] and a["vels"] == b["vels"]):
        partner[sa], partner[sb] = sb, sa
        how[sa] = how[sb] = "same zone ranges"
  print(f"\nSTEREO: {len(partner) // 2} left/right pairs "
        f"({sorted(set(how.values()))}); wSampleLink values on the preset's samples: "
        f"{sorted({shdr[s][8] for s in case_all})}; sample 0 is '{name(shdr[0][0])}'")
  lr_total = sum(1 for s in range(len(shdr) - 1) if shdr[s][9] & 0x6)
  lr_linked = sum(1 for s in range(len(shdr) - 1) if mutual_link(s) is not None)
  print(f"  whole file: {lr_total} samples typed left or right, {lr_linked} with a mutual wSampleLink")

  smpl_off = next(off for cid, off, size in sdta if cid == b"smpl")
  def pcm(s):
    start, end = shdr[s][1], shdr[s][2]
    if not 0 <= start <= end <= smpl_bytes // 2:
      fail(f"sample {s} lies outside smpl")
    a = array.array("h")
    a.frombytes(data[smpl_off + 2 * start:smpl_off + 2 * end])
    if sys.byteorder != "little":
      a.byteswap()
    return a

  for s in sorted(x for x in partner if shdr[x][9] & 0x4):
    left, right = pcm(s), pcm(partner[s])
    n = min(len(left), len(right))
    sll = sum(v * v for v in left[:n])
    srr = sum(v * v for v in right[:n])
    slr = sum(x * y for x, y in zip(left[:n], right[:n]))
    corr = slr / math.sqrt(sll * srr) if sll and srr else float("nan")
    side = sum((x - y) ** 2 for x, y in zip(left[:n], right[:n]))
    mid = sum((x + y) ** 2 for x, y in zip(left[:n], right[:n]))
    side_db = 10 * math.log10(side / mid) if side and mid else float("-inf")
    print(f"  pair {s}/{partner[s]}: identical={bytes(left) == bytes(right)}, "
          f"correlation {corr:.3f}, side/mid energy {side_db:.1f} dB")

  # Stereo kept as one channel: one sample per pair, the longer side.
  mono, seen = set(), set()
  for s in sorted(case_range):
    if s in seen:
      continue
    t = partner.get(s)
    if t is not None and t in case_range:
      keep = s if sample_points(s) >= sample_points(t) else t
      mono.add(keep)
      seen.update((s, t))
    else:
      mono.add(s)
      seen.add(s)

  # Other presets that play this preset's instruments or samples.
  my_insts = {l["inst"] for l in layers}
  sharing = []
  for q in range(len(phdr) - 1):
    if q == p:
      continue
    q_insts = set()
    for is_global, g, _, _ in zones(pbag, pgen, pmod, phdr[q][3], phdr[q + 1][3], INSTRUMENT):
      if not is_global:
        q_insts.add(g[INSTRUMENT])
    q_samples = set()
    for i in q_insts:
      for is_global, g, _, _ in zones(ibag, igen, imod, inst[i][1], inst[i + 1][1], SAMPLE_ID):
        if not is_global:
          q_samples.add(g[SAMPLE_ID])
    shared = q_samples & case_all
    if shared or q_insts & my_insts:
      sharing.append(f"{phdr[q][2]}:{phdr[q][1]} '{name(phdr[q][0])}' "
                     f"(same instrument: {bool(q_insts & my_insts)}, shares {len(shared)} of "
                     f"{len(case_all)} samples, plays {len(q_samples)} samples)")
  print("PRESETS SHARING ITS INSTRUMENT OR SAMPLES:", sharing if sharing else "none")

  print("\nSIZES (16-bit smpl data; 'padded' adds the 46 zero points each sample needs):")
  for label, ids in (("every zone", case_all), (f"zones covering {lo_key}-{hi_key}", case_range),
                     (f"{lo_key}-{hi_key}, stereo pair as one channel", mono)):
    raw, padded, pts = bytes_for(ids)
    sm24 = f", sm24 would add {pts + ZERO_PAD_POINTS * len(ids)}" if b"sm24" in sdta_ids else ""
    print(f"  {label}: {len(ids)} samples, {raw} bytes raw, {padded} bytes padded "
          f"({padded / 1048576:.2f} MiB){sm24}")

  print(f"\nKEY COVERAGE {lo_key}-{hi_key}:")
  worst_up, worst_down = (0, None), (0, None)
  roots = set()
  for l in layers:
    if not in_range(l):
      continue
    g = l["gens"]
    pg = l["pgens"]
    s = l["sample"]
    root = g.get(OVERRIDING_ROOT_KEY, -1)
    if not 0 <= root <= 127:
      root = shdr[s][6] if shdr[s][6] <= 127 else 60
    roots.add((s, root))
    scale = g.get(SCALE_TUNING, 100) + pg.get(SCALE_TUNING, 0)
    tune = 100 * (g.get(COARSE_TUNE, 0) + pg.get(COARSE_TUNE, 0)) \
        + g.get(FINE_TUNE, 0) + pg.get(FINE_TUNE, 0) + shdr[s][7]
    for k in range(max(l["keys"][0], lo_key), min(l["keys"][1], hi_key) + 1):
      cents = scale * (k - root) + tune
      if cents > worst_up[0]:
        worst_up = (cents, (k, s, root, l["vels"]))
      if cents < worst_down[0]:
        worst_down = (cents, (k, s, root, l["vels"]))
  print(f"  distinct samples covering the range: {len({s for s, _ in roots})}; "
        f"distinct root keys: {sorted({r for _, r in roots})}")
  print(f"  largest repitch up: {worst_up[0] / 100:+.2f} semitones (key, sample, root, vel) {worst_up[1]}")
  print(f"  largest repitch down: {worst_down[0] / 100:+.2f} semitones (key, sample, root, vel) {worst_down[1]}")
  for k in range(lo_key, hi_key + 1):
    hits = [l for l in layers if l["keys"][0] <= k <= l["keys"][1]]
    vel_cover = sorted({l["vels"] for l in hits})
    if k in (lo_key, hi_key) or k % 12 == 0:
      print(f"  key {k}: {len(hits)} zones sound, velocity ranges {vel_cover}")
  uncovered = [k for k in range(lo_key, hi_key + 1)
               if not any(l["keys"][0] <= k <= l["keys"][1] for l in layers)]
  print(f"  keys with no zone: {uncovered}")

  print("\nVELOCITY LAYERS:", sorted({l["vels"] for l in layers}))
  print("SAMPLE TYPES:", sorted({f"0x{shdr[l['sample']][9]:04X}" for l in layers}))
  print("SAMPLE RATES:", sorted({shdr[l["sample"]][5] for l in layers}))

  print("\nSAMPLE MODES AND LOOPS (per sounding zone):")
  modes = {}
  for l in layers:
    g = l["gens"]
    mode = g.get(SAMPLE_MODES, 0)
    s = l["sample"]
    _, start, end, sl, el, rate, *_ = shdr[s]
    sl2 = sl + g.get(2, 0) + 32768 * g.get(45, 0)
    el2 = el + g.get(3, 0) + 32768 * g.get(50, 0)
    modes.setdefault(mode, []).append((el2 - sl2, rate, end - start, l["keys"]))
  for mode, rows in sorted(modes.items()):
    lens = [r[0] for r in rows]
    ms = [1000 * r[0] / r[1] for r in rows if r[1]]
    print(f"  sampleModes {mode}: {len(rows)} zones; loop points min {min(lens)} max {max(lens)}; "
          f"loop ms min {min(ms):.1f} max {max(ms):.1f}")
  offsets = sorted({o for l in layers for o in l["gens"] if o in (0, 1, 2, 3, 4, 12, 45, 50)})
  print(f"  address-offset generators used: {[GEN_NAMES[o] for o in offsets]}")

  print("\nGENERATORS USED:")
  for level in ("preset", "instrument"):
    print(f"  {level}: {[GEN_NAMES[o] for o in sorted(used_gens[level])]}")
  print("  value ranges at the instrument level (merged with its global zone):")
  for o in sorted({o for l in layers for o in l["gens"]}):
    if o in (SAMPLE_ID,):
      continue
    vals = sorted({l["gens"][o] for l in layers if o in l["gens"]})
    shown = vals if len(vals) <= 8 else vals[:3] + ["..."] + vals[-3:]
    print(f"    {GEN_NAMES[o]}: {len(vals)} distinct, {shown}")
  missing = [l for l in layers if not all(o in l["gens"] for o in sorted({o for l in layers for o in l["gens"]}))]
  print(f"  zones missing some generator that others set: {len(missing)} of {len(layers)}")

  print("\nZONE TABLE (instrument zones by key range, both sides of a pair merged; "
        "hold/decay in timecents, '-' = unset):")
  print("  | keys | root | samples | hold | decay | points | seconds | loop points | loop start |")
  rows = {}
  for l in layers:
    if l["vels"] != max(x["vels"] for x in layers):
      continue  # every velocity zone plays the same instrument zones
    rows.setdefault(l["keys"], []).append(l)
  for keys in sorted(rows):
    ls = sorted(rows[keys], key=lambda x: x["sample"])
    g = ls[0]["gens"]
    s = ls[0]["sample"]
    _, start, end, sl, el, rate, *_ = shdr[s]
    holds = sorted({str(x["gens"].get(35, "-")) for x in ls})
    decays = sorted({str(x["gens"].get(36, "-")) for x in ls})
    root = g.get(OVERRIDING_ROOT_KEY, shdr[s][6])
    print(f"  | {keys[0]}-{keys[1]} | {root} | {'/'.join(str(x['sample']) for x in ls)} | "
          f"{'/'.join(holds)} | {'/'.join(decays)} | {end - start} | {(end - start) / rate:.2f} | "
          f"{el - sl} | {(sl - start) / (end - start):.0%} |")

  print("\nMODULATORS in the preset's zones:")
  if not all_mods:
    print("  none (only the default modulators apply)")
  for level, z, m in all_mods:
    tag = "same as a default" if m in DEFAULT_MODULATORS else "not a default"
    print(f"  {level} zone {z}: {mod_text(m)} ({tag})")

  # Extract size: a well-formed single-preset file with the kept zones only.
  print("\nEXTRACT SIZE (RIFF sfbk with INFO ifil/isng/INAM/ICOP/ICMT, sdta smpl, pdta hydra):")
  notice_bytes = 1200  # room for the MIT copyright and permission notice in ICMT
  for label, keep in (("every zone", case_all), (f"{lo_key}-{hi_key}", case_range),
                      (f"{lo_key}-{hi_key} mono", mono)):
    kept_layers = [l for l in layers if l["sample"] in keep]
    insts = sorted({l["inst"] for l in kept_layers})
    # Zone and record counts; global zones are kept as they are.
    p_zone_count = len(pzones)
    p_gen_count = sum(len(order) for _, _, order, _ in pzones)
    p_mod_count = sum(len(mods) for _, _, _, mods in pzones)
    i_zone_count = i_gen_count = i_mod_count = 0
    for i in insts:
      for is_global, g, order, mods in zones(ibag, igen, imod, inst[i][1], inst[i + 1][1], SAMPLE_ID):
        if is_global or g[SAMPLE_ID] in keep:
          i_zone_count += 1
          i_gen_count += len(order)
          i_mod_count += len(mods)
    raw, padded, _ = bytes_for(keep)
    info_size = 12 + (8 + 4) + (8 + 8) + (8 + 32) + (8 + 64) + (8 + notice_bytes)
    sdta_size = 12 + 8 + padded
    pdta_size = 12 + 9 * 8 + (2 * PHDR) + (p_zone_count + 1) * PBAG \
        + (p_mod_count + 1) * PMOD + (p_gen_count + 1) * PGEN + (len(insts) + 1) * INST \
        + (i_zone_count + 1) * IBAG + (i_mod_count + 1) * IMOD + (i_gen_count + 1) * IGEN \
        + (len(keep) + 1) * SHDR
    total = 12 + info_size + sdta_size + pdta_size
    print(f"  {label}: {len(keep)} samples, {i_zone_count} instrument zones, {total} bytes "
          f"({total / 1048576:.2f} MiB; {'fits' if total <= 26214400 else 'over'} 25 MiB), "
          f"of which pdta {pdta_size}, INFO {info_size}, sample data {padded}")


if __name__ == "__main__":
  main(sys.argv)
