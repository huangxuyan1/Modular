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
source's presets, instruments or samples is kept. The INFO list holds the
source's version and sound engine, the preset's name, a short copyright line
(the source's own, else the notice's first line) and, in the comment field, the
whole notice followed by the provenance: the source's URL, version and SHA-256,
the preset, and the Modular commit the tool ran from, marked dirty when that
checkout has uncommitted changes. The same inputs and commit give the same
bytes: nothing else, no date, is written. The tool prints the extract's size,
its SHA-256 and the provenance, which is the record to keep beside it.

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
from collections import namedtuple
from pathlib import Path

HYDRA = ("phdr", "pbag", "pmod", "pgen", "inst", "ibag", "imod", "igen", "shdr")
RECORD_SIZES = {"phdr": 38, "pbag": 4, "pmod": 10, "pgen": 4, "inst": 22,
                "ibag": 4, "imod": 10, "igen": 4, "shdr": 46}

INSTRUMENT = 41  # generator numbers, SoundFont 2.04 section 8.1.2
SAMPLE_ID = 53
COMPRESSED = 0x10  # sample type bits, section 7.10
ROM = 0x8000
ZERO_POINTS = 46  # section 6.1: at least 46 zero points follow each sample
SHORT_TEXT_LIMIT = 256  # isng, INAM and ICOP, in bytes with the terminator (section 5)
ICMT_LIMIT = 65536
DEFAULT_ENGINE = b"EMU8000"

# The hydra's records, with their fields as the file holds them. Every list
# keeps its terminal record last.
PresetHeader = namedtuple("PresetHeader", "name program bank bag library genre morphology")
InstrumentHeader = namedtuple("InstrumentHeader", "name bag")
SampleHeader = namedtuple(
    "SampleHeader", "name start end loop_start loop_end rate pitch correction link kind")
Bag = namedtuple("Bag", "gen mod")
Generator = namedtuple("Generator", "oper amount")
# One level of zones, a preset's or an instrument's: its bag, generator and
# modulator lists, the letter that starts their chunks' tags, and the generator
# that names a record of the level below.
Level = namedtuple("Level", "name prefix bags gens mods terminal")


class Refusal(Exception):
  pass


# --- reading -----------------------------------------------------------------

def chunk_at(data, pos, end):
  """The chunk at data[pos:end] as (tag, offset, size, next), padded to even."""
  if end - pos < 8:
    raise Refusal("truncated chunk")
  tag = bytes(data[pos:pos + 4])
  size = struct.unpack_from("<I", data, pos + 4)[0]
  body = pos + 8
  if size > end - body:
    raise Refusal("truncated chunk")
  following = body + size
  if size % 2:
    if following >= end:
      raise Refusal("truncated chunk")
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
      raise Refusal(f"{what} indexes out of order")
  if starts[-1] + 1 != count:
    raise Refusal(f"{what} indexes do not end at the list's end")


def generators(level, zones):
  """The generators of a run of a level's zones (a range of its bags)."""
  return level.gens[level.bags[zones.start].gen:level.bags[zones.stop].gen]


def modulators(level, zones):
  return level.mods[level.bags[zones.start].mod:level.bags[zones.stop].mod]


class SoundFont:
  """A SoundFont 2 file, read and checked whole. Records keep their raw fields."""

  def __init__(self, data):
    self.data = data
    tag, body, size, _ = chunk_at(data, 0, len(data))
    # Bytes after the RIFF chunk are not part of the file, as the node reads it.
    if tag != b"RIFF":
      raise Refusal("missing RIFF header")
    if size < 4:
      raise Refusal("truncated chunk")
    if data[body:body + 4] != b"sfbk":
      raise Refusal("not a soundfont")
    lists = {}
    for tag, offset, length in chunks(data, body + 4, body + size):
      if tag != b"LIST":
        continue
      if length < 4:
        raise Refusal("truncated chunk")
      kind = bytes(data[offset:offset + 4])
      if kind not in (b"INFO", b"sdta", b"pdta"):
        continue
      if kind in lists:
        raise Refusal(f"duplicate {kind.decode()} list")
      lists[kind] = chunks(data, offset + 4, offset + length)
    for kind in (b"INFO", b"sdta", b"pdta"):
      if kind not in lists:
        raise Refusal(f"missing {kind.decode()} list")

    # INFO: a later chunk of the same tag replaces an earlier one, as in the node.
    self.info = {}
    for tag, offset, length in lists[b"INFO"]:
      self.info[tag] = bytes(data[offset:offset + length])
    if b"ifil" not in self.info:
      raise Refusal("missing ifil version")
    if len(self.info[b"ifil"]) != 4:
      raise Refusal("ifil version of the wrong size")
    self.version = struct.unpack("<HH", self.info[b"ifil"])
    if self.version[0] == 3:
      raise Refusal("a SoundFont 3 file: compressed samples are not supported")
    if self.version[0] != 2:
      raise Refusal("only version 2 files are supported")

    self.smpl_offset, self.points = 0, 0
    for tag, offset, length in lists[b"sdta"]:
      if tag == b"sm24":
        raise Refusal("24-bit sample data (sm24) is not supported")
      if tag == b"smpl":
        if length % 2:
          raise Refusal("sample data of odd size")
        self.smpl_offset, self.points = offset, length // 2

    found = {}
    for tag, offset, length in lists[b"pdta"]:
      name = tag.decode("latin-1")
      if name not in RECORD_SIZES:
        continue
      if name in found:
        raise Refusal(f"duplicate {name} list")
      found[name] = (offset, length)
    raw = {}
    for name in HYDRA:
      if name not in found:
        raise Refusal(f"missing {name} list")
      offset, length = found[name]
      width = RECORD_SIZES[name]
      if length % width or length < width:
        raise Refusal(f"{name} list of the wrong size")
      raw[name] = [bytes(data[offset + i:offset + i + width])
                   for i in range(0, length, width)]

    self.presets = [PresetHeader(r[0:20], *struct.unpack_from("<HHHIII", r, 20))
                    for r in raw["phdr"]]
    self.instruments = [InstrumentHeader(r[0:20], *struct.unpack_from("<H", r, 20))
                        for r in raw["inst"]]
    self.samples = [SampleHeader(r[0:20], *struct.unpack_from("<IIIIIBbHH", r, 20))
                    for r in raw["shdr"]]
    # Modulators are only ever copied, never read, so they stay raw records.
    self.preset_level, self.instrument_level = (
        Level(name, prefix, [Bag(*struct.unpack("<HH", r)) for r in raw[prefix + "bag"]],
              [Generator(*struct.unpack("<HH", r)) for r in raw[prefix + "gen"]],
              raw[prefix + "mod"], terminal)
        for name, prefix, terminal in (("preset", "p", INSTRUMENT),
                                       ("instrument", "i", SAMPLE_ID)))

    for sample in self.samples[:-1]:
      if sample.kind & COMPRESSED:
        raise Refusal("a compressed sample: SoundFont 3 samples are not supported")
      if not sample.kind & ROM and not sample.start <= sample.end <= self.points:
        raise Refusal("a sample lies outside the sample data")
    for level in (self.preset_level, self.instrument_level):
      check_runs([b.gen for b in level.bags], len(level.gens), f"{level.name} generator")
      check_runs([b.mod for b in level.bags], len(level.mods), f"{level.name} modulator")
    check_runs([h.bag for h in self.presets], len(self.preset_level.bags), "preset bag")
    check_runs([h.bag for h in self.instruments], len(self.instrument_level.bags),
               "instrument bag")
    if len(self.presets) < 2:
      raise Refusal("no preset")
    # Every index the copy relinks must name a record of its list. A generator
    # after a zone's terminal one breaks the specification and counts too.
    for level, below, what in ((self.preset_level, self.instruments, "instrument"),
                               (self.instrument_level, self.samples, "sample")):
      for oper, amount in level.gens[:-1]:
        if oper == level.terminal and amount >= len(below) - 1:
          raise Refusal(f"{what} index out of range")

  def find(self, bank, program):
    """The first preset with this number, the one the node plays."""
    for index, header in enumerate(self.presets[:-1]):
      if header.bank == bank and header.program == program:
        return index
    raise Refusal(f"no preset at bank {bank} program {program}")

  def preset_bags(self, index):
    """A preset's zones, as a range of the preset bag list."""
    return range(self.presets[index].bag, self.presets[index + 1].bag)

  def instrument_bags(self, index):
    return range(self.instruments[index].bag, self.instruments[index + 1].bag)

  def sample_points(self, index):
    sample = self.samples[index]
    return bytes(self.data[self.smpl_offset + 2 * sample.start:
                           self.smpl_offset + 2 * sample.end])

  def preset_instruments(self, index):
    """The instruments the preset's zones name, in order of first mention."""
    gens = generators(self.preset_level, self.preset_bags(index))
    return list(dict.fromkeys(g.amount for g in gens if g.oper == INSTRUMENT))

  def instrument_samples(self, instruments):
    result = []
    for i in instruments:
      gens = generators(self.instrument_level, self.instrument_bags(i))
      result += [g.amount for g in gens if g.oper == SAMPLE_ID]
    return list(dict.fromkeys(result))


def read(path):
  with open(path, "rb") as f:
    data = f.read()
  return data, SoundFont(memoryview(data))


# --- writing -----------------------------------------------------------------

def chunk(tag, data):
  if len(data) > 0xFFFFFFFE:
    raise Refusal("the extract is too large for a RIFF file")
  return tag + struct.pack("<I", len(data)) + data + (b"\0" if len(data) % 2 else b"")


def riff_list(kind, parts):
  return chunk(b"LIST", kind + b"".join(parts))


def zstr(value):
  """A zero-terminated string field of even size."""
  data = value + b"\0"
  return data + b"\0" if len(data) % 2 else data


class ZoneLists:
  """One level's bag, generator and modulator lists, written a run of zones at a
  time. The runs are parts of the source's lists, so every count fits its field."""

  def __init__(self, level):
    self.level = level
    self.bags, self.gens, self.mods = bytearray(), bytearray(), bytearray()

  def add(self, zones, mapping):
    """Copies a run of zones verbatim but for their indexes, `mapping` giving the
    level below's new indexes; returns the run's first zone."""
    first = self.zone_count()
    gen_shift = len(self.gens) // 4 - self.level.bags[zones.start].gen
    mod_shift = len(self.mods) // 10 - self.level.bags[zones.start].mod
    for bag in self.level.bags[zones.start:zones.stop]:
      self.bags += struct.pack("<HH", bag.gen + gen_shift, bag.mod + mod_shift)
    for oper, amount in generators(self.level, zones):
      if oper == self.level.terminal:
        amount = mapping[amount]
      self.gens += struct.pack("<HH", oper, amount)
    self.mods += b"".join(modulators(self.level, zones))
    return first

  def zone_count(self):
    return len(self.bags) // 4

  def chunks(self):
    """The bag, modulator and generator chunks, each closed by its terminal record."""
    prefix = self.level.prefix.encode()
    terminal_bag = struct.pack("<HH", len(self.gens) // 4, len(self.mods) // 10)
    return [chunk(prefix + b"bag", bytes(self.bags) + terminal_bag),
            chunk(prefix + b"mod", bytes(self.mods) + bytes(10)),
            chunk(prefix + b"gen", bytes(self.gens) + bytes(4))]


def extract_bytes(font, preset, info):
  """The extract's bytes: `info` is the INFO list's chunks, in order."""
  instruments = sorted(font.preset_instruments(preset))
  samples = sorted(font.instrument_samples(instruments))
  if any(font.samples[s].kind & ROM for s in samples):
    raise Refusal("the preset plays a sample in rom, which an extract cannot carry")
  instrument_map = {old: new for new, old in enumerate(instruments)}
  sample_map = {old: new for new, old in enumerate(samples)}

  # Sample data and headers: each sample's points, then 46 zero points.
  points, headers, at = [], [], 0
  for s in samples:
    sample = font.samples[s]
    shift = at - sample.start
    loop_start, loop_end = sample.loop_start + shift, sample.loop_end + shift
    if not (0 <= loop_start < 2 ** 32 and 0 <= loop_end < 2 ** 32):
      raise Refusal("a sample's loop lies too far outside its sample to be relinked")
    points += [font.sample_points(s), bytes(2 * ZERO_POINTS)]
    headers.append(sample.name + struct.pack(
        "<IIIIIBbHH", at, sample.end + shift, loop_start, loop_end, sample.rate,
        sample.pitch, sample.correction, sample_map.get(sample.link, 0), sample.kind))
    at += sample.end - sample.start + ZERO_POINTS
  headers.append(b"EOS".ljust(46, b"\0"))

  # The preset: its header, then its zones with instrument indexes relinked.
  preset_zones = ZoneLists(font.preset_level)
  preset_zones.add(font.preset_bags(preset), instrument_map)
  header = font.presets[preset]
  phdr = (header.name
          + struct.pack("<HHHIII", header.program, header.bank, 0, header.library,
                        header.genre, header.morphology)
          + b"EOP".ljust(20, b"\0")
          + struct.pack("<HHHIII", 0, 0, preset_zones.zone_count(), 0, 0, 0))

  # The instruments, in their source order, with sample indexes relinked.
  instrument_zones = ZoneLists(font.instrument_level)
  inst = b""
  for i in instruments:
    first = instrument_zones.add(font.instrument_bags(i), sample_map)
    inst += font.instruments[i].name + struct.pack("<H", first)
  inst += b"EOI".ljust(20, b"\0") + struct.pack("<H", instrument_zones.zone_count())

  pdta = ([chunk(b"phdr", phdr)] + preset_zones.chunks() + [chunk(b"inst", inst)]
          + instrument_zones.chunks() + [chunk(b"shdr", b"".join(headers))])
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
    raise Refusal("cannot read the Modular commit: run the tool from a Modular checkout")
  return head + (" (dirty)" if status.strip() else "")


def one_line(value, what):
  if not value or any(c in value for c in "\r\n\0"):
    raise Refusal(f"the {what} must be one non-empty line")
  return value


def short_text(value, field):
  if len(zstr(value)) > SHORT_TEXT_LIMIT:
    raise Refusal(f"the {field} field would pass its {SHORT_TEXT_LIMIT} bytes")
  return zstr(value)


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
    raise Refusal("output already exists")
  url = one_line(args.source_url, "source URL")
  version = one_line(args.source_version, "source version")
  try:
    notice = Path(args.notice).read_bytes()
  except OSError:
    raise Refusal("cannot read the notice file")
  try:
    notice.decode("utf-8")
  except UnicodeDecodeError:
    raise Refusal("the notice is not UTF-8 text")
  if not notice.strip() or b"\0" in notice:
    raise Refusal("the notice must be text, not empty")
  commit = modular_commit()

  try:
    data, font = read(args.source)
  except OSError:
    raise Refusal("cannot read the source file")
  bank, program = args.preset
  preset = font.find(bank, program)
  preset_name = text(font.presets[preset].name)

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
    raise Refusal(f"the notice is too long for the comment field (ICMT, at most "
                  f"{ICMT_LIMIT:,} bytes with the provenance)")
  copyright_line = text(font.info.get(b"ICOP", b""))
  if not copyright_line.strip():
    copyright_line = next(line for line in notice.splitlines() if line.strip()).strip()
  engine = text(font.info.get(b"isng", b"")) or DEFAULT_ENGINE

  info = [(b"ifil", struct.pack("<HH", *font.version)),
          (b"isng", short_text(engine, "sound engine (isng)")),
          (b"INAM", short_text(preset_name, "name (INAM)")),
          (b"ICOP", short_text(copyright_line, "copyright (ICOP)")),
          (b"ICMT", zstr(comment))]
  result = extract_bytes(font, preset, info)
  try:
    with open(output, "xb") as f:
      f.write(result)
  except FileExistsError:
    raise Refusal("output already exists")
  except OSError:
    output.unlink(missing_ok=True)
    raise Refusal("cannot write the output")
  print(f"Extract: {len(result)} bytes, SHA-256 {hashlib.sha256(result).hexdigest()}")
  print("Its comment field ends with this provenance:")
  print(provenance.decode("utf-8"), end="")


# --- the preset and contents commands ------------------------------------------

def name_of(header):
  return text(header.name).decode("latin-1")


def preset_command(args):
  _, font = read(args.file)
  index = font.find(*args.preset)
  instruments = font.preset_instruments(index)
  samples = font.instrument_samples(instruments)
  # Each instrument and sample is numbered in order of first mention.
  instrument_numbers = {old: new for new, old in enumerate(instruments)}
  sample_numbers = {old: new for new, old in enumerate(samples)}

  def print_zones(level, bags, numbers, label):
    for z, bag in enumerate(bags):
      zone = range(bag, bag + 1)
      gens = " ".join(f"{o}={label}[{numbers[a]}]" if o == level.terminal else f"{o}=0x{a:04x}"
                      for o, a in generators(level, zone))
      mods = " ".join(m.hex() for m in modulators(level, zone))
      print(f"  zone {z}: generators {gens or '-'}; modulators {mods or '-'}")

  header = font.presets[index]
  print(f"preset {header.bank}:{header.program} \"{name_of(header)}\" "
        f"library={header.library} genre={header.genre} morphology={header.morphology}")
  print_zones(font.preset_level, font.preset_bags(index), instrument_numbers, "instrument")
  for i in instruments:
    print(f"instrument[{instrument_numbers[i]}] \"{name_of(font.instruments[i])}\"")
    print_zones(font.instrument_level, font.instrument_bags(i), sample_numbers, "sample")
  for s in samples:
    sample = font.samples[s]
    content = "rom" if sample.kind & ROM else hashlib.sha256(font.sample_points(s)).hexdigest()
    print(f"sample[{sample_numbers[s]}] \"{name_of(sample)}\" "
          f"points={sample.end - sample.start} sha256={content} "
          f"loop={sample.loop_start - sample.start:+d}..{sample.loop_end - sample.start:+d} "
          f"rate={sample.rate} pitch={sample.pitch} correction={sample.correction} "
          f"type=0x{sample.kind:04x}")


def contents_command(args):
  _, font = read(args.file)
  print(f"version {font.version[0]}.{font.version[1]:02d}")
  print(f"presets {len(font.presets) - 1}, instruments {len(font.instruments) - 1}, "
        f"samples {len(font.samples) - 1}, sample points {font.points}")
  for tag, data in font.info.items():
    if tag != b"ifil":
      print(f"{tag.decode('latin-1')} ({len(data)} bytes):")
      print(text(data).decode("utf-8", errors="replace"))
  for header in font.presets[:-1]:
    print(f"preset {header.bank}:{header.program} \"{name_of(header)}\"")
  for s, sample in enumerate(font.samples[:-1]):
    print(f"sample {s} \"{name_of(sample)}\" type=0x{sample.kind:04x} link={sample.link}")


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
  command.set_defaults(handler=extract)
  command = commands.add_parser("preset", help="print one preset's structure")
  command.add_argument("file")
  command.add_argument("preset", type=parse_preset, metavar="BANK:PROGRAM")
  command.set_defaults(handler=preset_command)
  command = commands.add_parser("contents", help="print a file's INFO, presets and samples")
  command.add_argument("file")
  command.set_defaults(handler=contents_command)
  args = parser.parse_args(argv)
  try:
    args.handler(args)
  except Refusal as refusal:
    print(f"soundfont_extract: {refusal}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
