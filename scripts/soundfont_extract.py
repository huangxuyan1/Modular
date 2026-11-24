"""Copy one preset of a SoundFont 2 file into its own SoundFont 2 file.

Python 3.8 or later, standard library only, outside the CMake build. The extract
plays through Modular's soundfont node exactly as the preset plays in the full
file, so a site can ship one instrument instead of a whole General MIDI bank.

How to run it, from anywhere:

  python -I scripts/soundfont_extract.py extract SOURCE.sf2 --preset BANK:PROGRAM
      --notice NOTICE.txt --source-url URL --source-version VERSION --output OUT.sf2

It copies the preset with its zones, the instruments those zones name with
their zones, and the samples those zones play, each followed by 46 zero points.
Generators and modulators are copied verbatim; every index is relinked, a
sample link to a sample left out is cleared to 0, and nothing else of the
source is kept. The INFO list holds the source's version and sound engine, the
preset's name, a short copyright line (the source's own, else the notice's
first line) and, in the comment field, the whole notice followed by the
provenance: the source's URL, version and SHA-256, the preset, and the Modular
commit the tool ran from, marked dirty when that checkout has uncommitted
changes. The same inputs and commit give the same bytes: nothing else, no date,
is written. The tool prints the extract's size, its SHA-256 and the provenance,
which is the record to keep beside it.

It refuses, writing nothing: a SoundFont 3 file or one with a compressed
sample, 24-bit sample data (sm24), a preset the file lacks or that plays a
sample in ROM, a malformed file, a notice too long for the comment field, and
an output that already exists. A source file is untrusted data: every read is
bounds-checked, and nothing in it is executed.

Two more commands print what the tool reads, for its test and for review:

  python -I scripts/soundfont_extract.py preset FILE.sf2 BANK:PROGRAM
      One preset's zones, generators, modulators and samples, with every index
      replaced by what it names, so a source and its extract print the same.
  python -I scripts/soundfont_extract.py contents FILE.sf2
      The version, counts, INFO fields, presets, and each sample's type and link.

Its test is the soundfont_extract CTest entry (tests/test_soundfont_extract.cmake).
"""

import argparse
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

HYDRA = ("phdr", "pbag", "pmod", "pgen", "inst", "ibag", "imod", "igen", "shdr")
RECORD_SIZES = {"phdr": 38, "pbag": 4, "pmod": 10, "pgen": 4, "inst": 22,
                "ibag": 4, "imod": 10, "igen": 4, "shdr": 46}

INSTRUMENT = 41  # generator numbers, SoundFont 2.04 section 8.1.2
SAMPLE_ID = 53
COMPRESSED = 0x10  # sample type bits, section 7.10
ROM = 0x8000
ZERO_POINTS = 46  # section 6.1: at least 46 zero points follow each sample
ICOP_LIMIT = 256  # bytes, terminator included (section 5.1)
ICMT_LIMIT = 65536
DEFAULT_ENGINE = b"EMU8000"


class Refusal(Exception):
  pass


def refuse(reason):
  raise Refusal(reason)


# --- reading -----------------------------------------------------------------

def u16(data, offset):
  return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
  return struct.unpack_from("<I", data, offset)[0]


def chunk_at(data, pos, end):
  """The chunk at data[pos:end] as (tag, offset, size, next), padded to even."""
  if end - pos < 8:
    refuse("truncated chunk")
  tag = bytes(data[pos:pos + 4])
  size = u32(data, pos + 4)
  body = pos + 8
  if size > end - body:
    refuse("truncated chunk")
  following = body + size
  if size % 2:
    if following >= end:
      refuse("truncated chunk")
    following += 1
  return tag, body, size, following


def chunks(data, start, end):
  result, pos = [], start
  while pos < end:
    tag, body, size, pos = chunk_at(data, pos, end)
    result.append((tag, body, size))
  return result


def text(raw):
  """A zero-terminated string field, up to its first zero byte."""
  return bytes(raw).split(b"\0", 1)[0]


def check_runs(starts, count, what):
  """Each record's run starts in the next list. The starts never fall, and the
  terminal record's start is that list's terminal record, as the node checks."""
  for previous, start in zip(starts, starts[1:]):
    if start < previous:
      refuse(f"{what} indexes out of order")
  if starts[-1] + 1 != count:
    refuse(f"{what} indexes do not end at the list's end")


class SoundFont:
  """A SoundFont 2 file, read and checked whole. Records keep their raw fields."""

  def __init__(self, data):
    self.data = data
    tag, body, size, _ = chunk_at(data, 0, len(data))
    # Bytes after the RIFF chunk are not part of the file, as the node reads it.
    if tag != b"RIFF":
      refuse("missing RIFF header")
    if size < 4:
      refuse("truncated chunk")
    if data[body:body + 4] != b"sfbk":
      refuse("not a soundfont")
    lists = {}
    for tag, offset, length in chunks(data, body + 4, body + size):
      if tag != b"LIST":
        continue
      if length < 4:
        refuse("truncated chunk")
      kind = bytes(data[offset:offset + 4])
      if kind not in (b"INFO", b"sdta", b"pdta"):
        continue
      if kind in lists:
        refuse(f"duplicate {kind.decode()} list")
      lists[kind] = chunks(data, offset + 4, offset + length)
    for kind in (b"INFO", b"sdta", b"pdta"):
      if kind not in lists:
        refuse(f"missing {kind.decode()} list")

    # INFO: a later chunk of the same tag replaces an earlier one, as in the node.
    self.info = {}
    for tag, offset, length in lists[b"INFO"]:
      self.info[tag] = bytes(data[offset:offset + length])
    if b"ifil" not in self.info:
      refuse("missing ifil version")
    if len(self.info[b"ifil"]) != 4:
      refuse("ifil version of the wrong size")
    self.version = struct.unpack("<HH", self.info[b"ifil"])
    if self.version[0] == 3:
      refuse("a SoundFont 3 file: compressed samples are not supported")
    if self.version[0] != 2:
      refuse("only version 2 files are supported")

    self.smpl_offset, self.points = 0, 0
    for tag, offset, length in lists[b"sdta"]:
      if tag == b"sm24":
        refuse("24-bit sample data (sm24) is not supported")
      if tag == b"smpl":
        if length % 2:
          refuse("sample data of odd size")
        self.smpl_offset, self.points = offset, length // 2

    found = {}
    for tag, offset, length in lists[b"pdta"]:
      name = tag.decode("latin-1")
      if name not in RECORD_SIZES:
        continue
      if name in found:
        refuse(f"duplicate {name} list")
      found[name] = (offset, length)
    raw = {}
    for name in HYDRA:
      if name not in found:
        refuse(f"missing {name} list")
      offset, length = found[name]
      width = RECORD_SIZES[name]
      if length % width or length < width:
        refuse(f"{name} list of the wrong size")
      raw[name] = [bytes(data[offset + i:offset + i + width])
                   for i in range(0, length, width)]

    # Field tuples; every list keeps its terminal record last.
    self.phdr = [(r[0:20],) + struct.unpack_from("<HHHIII", r, 20) for r in raw["phdr"]]
    self.inst = [(r[0:20], u16(r, 20)) for r in raw["inst"]]
    self.shdr = [(r[0:20],) + struct.unpack_from("<IIIIIBbHH", r, 20) for r in raw["shdr"]]
    self.pbag = [struct.unpack("<HH", r) for r in raw["pbag"]]
    self.ibag = [struct.unpack("<HH", r) for r in raw["ibag"]]
    self.pgen = [struct.unpack("<HH", r) for r in raw["pgen"]]
    self.igen = [struct.unpack("<HH", r) for r in raw["igen"]]
    self.pmod = raw["pmod"]  # modulators are only ever copied, never read
    self.imod = raw["imod"]

    for name, start, end, _, _, _, _, _, _, kind in self.shdr[:-1]:
      if kind & COMPRESSED:
        refuse("a compressed sample: SoundFont 3 samples are not supported")
      if not kind & ROM and not start <= end <= self.points:
        refuse("a sample lies outside the sample data")
    for level, bags, gens, mods in (("preset", self.pbag, self.pgen, self.pmod),
                                    ("instrument", self.ibag, self.igen, self.imod)):
      check_runs([g for g, _ in bags], len(gens), f"{level} generator")
      check_runs([m for _, m in bags], len(mods), f"{level} modulator")
    check_runs([h[3] for h in self.phdr], len(self.pbag), "preset bag")
    check_runs([h[1] for h in self.inst], len(self.ibag), "instrument bag")
    if len(self.phdr) < 2:
      refuse("no preset")
    # Every index the copy relinks must name a record of its list.
    for oper, amount in self.pgen[:-1]:
      if oper == INSTRUMENT and amount >= len(self.inst) - 1:
        refuse("instrument index out of range")
    for oper, amount in self.igen[:-1]:
      if oper == SAMPLE_ID and amount >= len(self.shdr) - 1:
        refuse("sample index out of range")

  def find(self, bank, program):
    """The first preset with this number, the one the node plays."""
    for index, header in enumerate(self.phdr[:-1]):
      if header[2] == bank and header[1] == program:
        return index
    refuse(f"no preset at bank {bank} program {program}")

  def preset_runs(self, index):
    """A preset's bags, generators and modulators, as ranges of their lists."""
    first, last = self.phdr[index][3], self.phdr[index + 1][3]
    return (range(first, last), range(self.pbag[first][0], self.pbag[last][0]),
            range(self.pbag[first][1], self.pbag[last][1]))

  def instrument_runs(self, index):
    first, last = self.inst[index][1], self.inst[index + 1][1]
    return (range(first, last), range(self.ibag[first][0], self.ibag[last][0]),
            range(self.ibag[first][1], self.ibag[last][1]))

  def sample_points(self, index):
    _, start, end = self.shdr[index][:3]
    return bytes(self.data[self.smpl_offset + 2 * start:self.smpl_offset + 2 * end])

  def preset_instruments(self, index):
    """The instruments the preset's zones name, in order of first mention."""
    _, gens, _ = self.preset_runs(index)
    return list(dict.fromkeys(a for o, a in (self.pgen[g] for g in gens) if o == INSTRUMENT))

  def instrument_samples(self, instruments):
    result = []
    for i in instruments:
      _, gens, _ = self.instrument_runs(i)
      result += [a for o, a in (self.igen[g] for g in gens) if o == SAMPLE_ID]
    return list(dict.fromkeys(result))


def read(path):
  with open(path, "rb") as f:
    data = f.read()
  return data, SoundFont(memoryview(data))


# --- writing -----------------------------------------------------------------

def chunk(tag, data):
  if len(data) > 0xFFFFFFFE:
    refuse("the extract is too large for a RIFF file")
  return tag + struct.pack("<I", len(data)) + data + (b"\0" if len(data) % 2 else b"")


def riff_list(kind, parts):
  return chunk(b"LIST", kind + b"".join(parts))


def zstr(value):
  """A zero-terminated string field of even size."""
  data = value + b"\0"
  return data + b"\0" if len(data) % 2 else data


def relink(bags, gens, mods, font_bags, font_gens, font_mods, oper, mapping):
  """One run of zones, its indexes moved to start at 0 and `oper`'s amounts
  mapped; returns bag, generator and modulator bytes without terminals."""
  g0, m0 = font_bags[bags.start][0], font_bags[bags.start][1]
  bag_bytes = b"".join(struct.pack("<HH", font_bags[b][0] - g0, font_bags[b][1] - m0)
                       for b in bags)
  gen_bytes = b"".join(
      struct.pack("<HH", o, mapping[a] if o == oper else a)
      for o, a in (font_gens[g] for g in gens))
  return bag_bytes, gen_bytes, b"".join(font_mods[m] for m in mods)


def append_zones(lists, zones):
  """Appends a run of zones to (bags, gens, mods, gen count, mod count)."""
  bags, gens, mods, gen_count, mod_count = lists
  bag_bytes, gen_bytes, mod_bytes = zones
  for i in range(0, len(bag_bytes), 4):
    g, m = struct.unpack_from("<HH", bag_bytes, i)
    bags += struct.pack("<HH", g + gen_count, m + mod_count)
  return (bags, gens + gen_bytes, mods + mod_bytes,
          gen_count + len(gen_bytes) // 4, mod_count + len(mod_bytes) // 10)


def extract_bytes(font, preset, info):
  """The extract's bytes: `info` is the INFO list's chunks, in order."""
  instruments = sorted(font.preset_instruments(preset))
  samples = sorted(font.instrument_samples(instruments))
  for s in samples:
    if font.shdr[s][9] & ROM:
      refuse("the preset plays a sample in rom, which an extract cannot carry")
  instrument_map = {old: new for new, old in enumerate(instruments)}
  sample_map = {old: new for new, old in enumerate(samples)}

  # Sample data and headers: each sample's points, then 46 zero points.
  points, headers, at = [], [], 0
  for s in samples:
    name, start, end, loop_start, loop_end, rate, pitch, correction, link, kind = font.shdr[s]
    shift = at - start
    loop_start, loop_end = loop_start + shift, loop_end + shift
    if not (0 <= loop_start < 2 ** 32 and 0 <= loop_end < 2 ** 32):
      refuse("a sample's loop lies too far outside its sample to be relinked")
    points.append(font.sample_points(s))
    points.append(bytes(2 * ZERO_POINTS))
    headers.append(name + struct.pack("<IIIIIBbHH", at, end + shift, loop_start, loop_end,
                                      rate, pitch, correction, sample_map.get(link, 0), kind))
    at += end - start + ZERO_POINTS
  headers.append(b"EOS".ljust(46, b"\0"))

  # The preset: its header, then its zones with instrument indexes relinked.
  bags, gens, mods = font.preset_runs(preset)
  preset_lists = append_zones(
      (b"", b"", b"", 0, 0),
      relink(bags, gens, mods, font.pbag, font.pgen, font.pmod, INSTRUMENT, instrument_map))
  name, program, bank, _, library, genre, morphology = font.phdr[preset]
  phdr = (name + struct.pack("<HHHIII", program, bank, 0, library, genre, morphology)
          + b"EOP".ljust(20, b"\0") + struct.pack("<HHHIII", 0, 0, len(bags), 0, 0, 0))

  # The instruments, in their source order, with sample indexes relinked.
  instrument_lists, inst = (b"", b"", b"", 0, 0), b""
  for i in instruments:
    bags, gens, mods = font.instrument_runs(i)
    inst += font.inst[i][0] + struct.pack("<H", len(instrument_lists[0]) // 4)
    instrument_lists = append_zones(
        instrument_lists,
        relink(bags, gens, mods, font.ibag, font.igen, font.imod, SAMPLE_ID, sample_map))
  inst += b"EOI".ljust(20, b"\0") + struct.pack("<H", len(instrument_lists[0]) // 4)

  # The runs are parts of the source's lists, so every count fits its field.
  def hydra_lists(lists):
    bags, gens, mods, gen_count, mod_count = lists
    return (bags + struct.pack("<HH", gen_count, mod_count), mods + bytes(10), gens + bytes(4))

  pbag, pmod, pgen = hydra_lists(preset_lists)
  ibag, imod, igen = hydra_lists(instrument_lists)
  pdta = [chunk(b"phdr", phdr), chunk(b"pbag", pbag), chunk(b"pmod", pmod),
          chunk(b"pgen", pgen), chunk(b"inst", inst), chunk(b"ibag", ibag),
          chunk(b"imod", imod), chunk(b"igen", igen), chunk(b"shdr", b"".join(headers))]
  form = (b"sfbk" + riff_list(b"INFO", [chunk(tag, data) for tag, data in info])
          + riff_list(b"sdta", [chunk(b"smpl", b"".join(points))])
          + riff_list(b"pdta", pdta))
  return chunk(b"RIFF", form)


# --- the extract command -------------------------------------------------------

def modular_commit():
  """The commit of the checkout this script is in, and whether it is dirty."""
  here = str(Path(__file__).resolve().parent)
  try:
    head = subprocess.run(["git", "-C", here, "rev-parse", "--verify", "HEAD"],
                          capture_output=True, text=True, check=True).stdout.strip()
    status = subprocess.run(["git", "-C", here, "status", "--porcelain",
                             "--untracked-files=no"],
                            capture_output=True, text=True, check=True).stdout
  except (OSError, subprocess.CalledProcessError):
    refuse("cannot read the Modular commit: run the tool from a Modular checkout")
  return head + (" (dirty)" if status.strip() else "")


def one_line(value, what):
  if not value or any(c in value for c in "\r\n\0"):
    refuse(f"the {what} must be one non-empty line")
  return value


def parse_preset(value):
  try:
    bank, program = (int(part, 10) for part in value.split(":"))
  except ValueError:
    raise argparse.ArgumentTypeError("expected BANK:PROGRAM, for example 0:0")
  if not (0 <= bank <= 0xFFFF and 0 <= program <= 0xFFFF):
    raise argparse.ArgumentTypeError("bank and program run from 0 to 65535")
  return bank, program


def extract(args):
  output = Path(args.output)
  if output.exists():
    refuse("output already exists")
  url = one_line(args.source_url, "source URL")
  version = one_line(args.source_version, "source version")
  try:
    notice = Path(args.notice).read_bytes()
  except OSError:
    refuse("cannot read the notice file")
  try:
    notice.decode("utf-8")
  except UnicodeDecodeError:
    refuse("the notice is not UTF-8 text")
  if not notice.strip() or b"\0" in notice:
    refuse("the notice must be text, not empty")
  commit = modular_commit()

  try:
    data, font = read(args.source)
  except OSError:
    refuse("cannot read the source file")
  bank, program = args.preset
  preset = font.find(bank, program)
  preset_name = text(font.phdr[preset][0])

  provenance = (
      "This file holds one preset of the source below, copied by Modular's\n"
      "scripts/soundfont_extract.py.\n"
      f"Source URL: {url}\n"
      f"Source version: {version}\n"
      f"Source SHA-256: {hashlib.sha256(data).hexdigest()}\n"
      f"Preset: bank {bank}, program {program}, \"{preset_name.decode('latin-1')}\"\n"
      f"Modular commit: {commit}\n").encode("utf-8")
  comment = notice + (b"" if notice.endswith(b"\n") else b"\n") + b"\n" + provenance
  if len(zstr(comment)) > ICMT_LIMIT:
    refuse(f"the notice is too long for the comment field (ICMT, at most {ICMT_LIMIT:,} "
           "bytes with the provenance)")
  copyright_line = text(font.info.get(b"ICOP", b""))
  if not copyright_line.strip():
    copyright_line = next(line for line in notice.splitlines() if line.strip()).strip()
  if len(zstr(copyright_line)) > ICOP_LIMIT:
    refuse(f"the copyright line is too long for its field (ICOP, at most {ICOP_LIMIT} bytes)")
  engine = text(font.info.get(b"isng", b"")) or DEFAULT_ENGINE
  if len(zstr(engine)) > 256:
    refuse("the source's sound engine name (isng) is too long")

  info = [(b"ifil", struct.pack("<HH", *font.version)), (b"isng", zstr(engine)),
          (b"INAM", zstr(preset_name)), (b"ICOP", zstr(copyright_line)),
          (b"ICMT", zstr(comment))]
  result = extract_bytes(font, preset, info)
  try:
    with open(output, "xb") as f:
      f.write(result)
  except FileExistsError:
    refuse("output already exists")
  except OSError:
    output.unlink(missing_ok=True)
    refuse("cannot write the output")
  print(f"Extract: {len(result)} bytes, SHA-256 {hashlib.sha256(result).hexdigest()}")
  print("Its comment field ends with this provenance:")
  print(provenance.decode("utf-8"), end="")


# --- the preset and contents commands ------------------------------------------

def preset_command(args):
  _, font = read(args.file)
  bank, program = args.preset
  index = font.find(bank, program)
  instruments = font.preset_instruments(index)
  samples = font.instrument_samples(instruments)
  inst_no = {old: new for new, old in enumerate(instruments)}
  sample_no = {old: new for new, old in enumerate(samples)}

  def zones(bags, font_bags, font_gens, font_mods, oper, names, label):
    for z, b in enumerate(bags):
      (g0, m0), (g1, m1) = font_bags[b], font_bags[b + 1]
      gens = " ".join(f"{o}={label}[{names[a]}]" if o == oper else f"{o}=0x{a:04x}"
                      for o, a in font_gens[g0:g1])
      mods = " ".join(m.hex() for m in font_mods[m0:m1])
      print(f"  zone {z}: generators {gens or '-'}; modulators {mods or '-'}")

  name, program, bank, _, library, genre, morphology = font.phdr[index]
  print(f"preset {bank}:{program} \"{text(name).decode('latin-1')}\" library={library} "
        f"genre={genre} morphology={morphology}")
  zones(font.preset_runs(index)[0], font.pbag, font.pgen, font.pmod, INSTRUMENT, inst_no,
        "instrument")
  for i in instruments:
    print(f"instrument[{inst_no[i]}] \"{text(font.inst[i][0]).decode('latin-1')}\"")
    zones(font.instrument_runs(i)[0], font.ibag, font.igen, font.imod, SAMPLE_ID, sample_no,
          "sample")
  for s in samples:
    name, start, end, loop_start, loop_end, rate, pitch, correction, _, kind = font.shdr[s]
    content = "rom" if kind & ROM else hashlib.sha256(font.sample_points(s)).hexdigest()
    print(f"sample[{sample_no[s]}] \"{text(name).decode('latin-1')}\" points={end - start} "
          f"sha256={content} loop={loop_start - start:+d}..{loop_end - start:+d} "
          f"rate={rate} pitch={pitch} correction={correction} type=0x{kind:04x}")


def contents_command(args):
  _, font = read(args.file)
  print(f"version {font.version[0]}.{font.version[1]:02d}")
  print(f"presets {len(font.phdr) - 1}, instruments {len(font.inst) - 1}, "
        f"samples {len(font.shdr) - 1}, sample points {font.points}")
  for tag, data in font.info.items():
    if tag == b"ifil":
      continue
    value = text(data).decode("utf-8", errors="replace")
    print(f"{tag.decode('latin-1')} ({len(data)} bytes):")
    print(value)
  for name, program, bank, *_ in font.phdr[:-1]:
    print(f"preset {bank}:{program} \"{text(name).decode('latin-1')}\"")
  for s, (name, *_, link, kind) in enumerate(font.shdr[:-1]):
    print(f"sample {s} \"{text(name).decode('latin-1')}\" type=0x{kind:04x} link={link}")


def main(argv):
  # The notice and provenance print as written, LF and UTF-8, on every platform.
  sys.stdout.reconfigure(encoding="utf-8", newline="\n")
  parser = argparse.ArgumentParser(
      prog="soundfont_extract.py",
      description="Copy one preset of a SoundFont 2 file into its own SoundFont 2 file.")
  commands = parser.add_subparsers(dest="command", required=True)
  command = commands.add_parser("extract", help="write a one-preset extract")
  command.add_argument("source", help="the SoundFont 2 file to copy from")
  command.add_argument("--preset", required=True, type=parse_preset, metavar="BANK:PROGRAM")
  command.add_argument("--notice", required=True, help="the licence notice, a text file")
  command.add_argument("--source-url", required=True)
  command.add_argument("--source-version", required=True)
  command.add_argument("--output", required=True, help="the extract; must not exist")
  command.set_defaults(run=extract)
  command = commands.add_parser("preset", help="print one preset's structure")
  command.add_argument("file")
  command.add_argument("preset", type=parse_preset, metavar="BANK:PROGRAM")
  command.set_defaults(run=preset_command)
  command = commands.add_parser("contents", help="print a file's INFO, presets and samples")
  command.add_argument("file")
  command.set_defaults(run=contents_command)
  args = parser.parse_args(argv)
  try:
    args.run(args)
  except Refusal as refusal:
    print(f"soundfont_extract: {refusal}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
