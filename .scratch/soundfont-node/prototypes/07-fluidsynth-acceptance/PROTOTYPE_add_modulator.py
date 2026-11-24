"""PROTOTYPE, throwaway: soundfont-node ticket 07. Writes a copy of a SoundFont 2 file whose
preset's instruments carry the node's velocity-to-cutoff curve as a file modulator, so
FluidSynth plays it.

    python -I PROTOTYPE_add_modulator.py <in.sf2> <out.sf2> [bank] [program]

Standard library only. FluidSynth's fluid_mod_get_value returns 0 for any modulator identical
to its default velocity-to-cutoff one ("S. Christian Collins' mod"), so FluidSynth applies no
velocity-to-cutoff at all. A modulator with another identity is evaluated normally: this one
is note-on velocity, linear, unipolar, negative (0x0102) to initialFilterFc (8), amount -2400,
with no amount source and no transform. FluidSynth maps it to -2400 (127 - v) / 128 cents, the
node's -2400 (128 - v) / 128 less 18.75 cents. It is added to the global zone of each
instrument the preset (default bank 0, program 0) plays; every other byte is copied. The copy
is the comparison's input only: it is written outside the repository and never committed.
"""

import struct
import sys
from pathlib import Path

MODULATOR = struct.pack("<HHhHH", 0x0102, 8, -2400, 0, 0)
SAMPLE_ID, INSTRUMENT = 53, 41


def chunks(data: bytes, start: int, end: int):
    pos = start
    while pos + 8 <= end:
        tag, size = bytes(data[pos:pos + 4]), struct.unpack_from("<I", data, pos + 4)[0]
        yield tag, pos, size
        pos += 8 + size + (size & 1)


def main() -> None:
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    bank = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    program = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    data = bytearray(src.read_bytes())
    pdta = next(pos for tag, pos, size in chunks(data, 12, len(data))
                if tag == b"LIST" and data[pos + 8:pos + 12] == b"pdta")
    pdta_size = struct.unpack_from("<I", data, pdta + 4)[0]
    sub = {tag: (pos, size) for tag, pos, size in chunks(data, pdta + 12, pdta + 8 + pdta_size)}

    def records(tag, width):
        pos, size = sub[tag]
        return [data[pos + 8 + i:pos + 8 + i + width] for i in range(0, size, width)]

    phdr = records(b"phdr", 38)
    pbag = [struct.unpack("<HH", r) for r in records(b"pbag", 4)]
    pgen = [struct.unpack("<HH", r) for r in records(b"pgen", 4)]
    inst = [struct.unpack_from("<H", r, 20)[0] for r in records(b"inst", 22)]
    ibag = [list(struct.unpack("<HH", r)) for r in records(b"ibag", 4)]
    igen = [struct.unpack("<HH", r) for r in records(b"igen", 4)]

    # The instruments the preset's zones play.
    index = next(i for i, r in enumerate(phdr[:-1])
                 if struct.unpack_from("<HH", r, 20) == (program, bank))
    first, last = struct.unpack_from("<H", phdr[index], 24)[0], struct.unpack_from(
        "<H", phdr[index + 1], 24)[0]
    instruments = sorted({amount for b in range(first, last)
                          for oper, amount in pgen[pbag[b][0]:pbag[b + 1][0]]
                          if oper == INSTRUMENT})

    # Each instrument's global zone: its first, when that has no sampleID.
    targets = []
    for i in instruments:
        bag = inst[i]
        if not any(oper == SAMPLE_ID for oper, _ in igen[ibag[bag][0]:ibag[bag + 1][0]]):
            targets.append(bag)
        else:
            raise SystemExit(f"instrument {i} has no global zone; not handled")

    # Insert one modulator at the end of each global zone's modulators, last first, so the
    # indices of the bags before it stay right; every later bag's index moves up.
    imod_pos, imod_size = sub[b"imod"]
    mods = bytearray(data[imod_pos + 8:imod_pos + 8 + imod_size])
    for bag in sorted(targets, reverse=True):
        at = ibag[bag + 1][1]
        mods[at * 10:at * 10] = MODULATOR
        for later in range(bag + 1, len(ibag)):
            ibag[later][1] += 1
    ibag_pos, _ = sub[b"ibag"]
    for k, (gen, mod) in enumerate(ibag):
        struct.pack_into("<HH", data, ibag_pos + 8 + 4 * k, gen, mod)
    grow = len(mods) - imod_size
    data[imod_pos + 8:imod_pos + 8 + imod_size] = mods
    struct.pack_into("<I", data, imod_pos + 4, imod_size + grow)
    struct.pack_into("<I", data, pdta + 4, pdta_size + grow)
    struct.pack_into("<I", data, 4, struct.unpack_from("<I", data, 4)[0] + grow)
    dst.write_bytes(bytes(data))
    print(f"preset {bank}:{program}: instruments {instruments}; added the modulator to "
          f"{len(targets)} global zone(s); {len(data)} bytes -> {dst}")


if __name__ == "__main__":
    main()
