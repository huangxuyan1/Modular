// tests/GoldenRender.h's golden values, ported to JavaScript for the module's smoke test and
// the browser check page. The hash is FNV-1a, 64-bit, over each sample's IEEE 754 bits, their
// four bytes least significant first, frames in order and left before right, started afresh
// for every block of 4,096 frames. BigInt keeps it exact.
export const kBlockFrames = 4096;

function blockHash(channels, from, to) {
  const views = channels.map((samples) =>
    new DataView(samples.buffer, samples.byteOffset, samples.byteLength));
  let hash = 14695981039346656037n;
  for (let frame = from; frame < to; ++frame)
    for (const view of views) {
      const bits = view.getUint32(4 * frame, true);
      for (let shift = 0; shift < 32; shift += 8) {
        hash ^= BigInt((bits >>> shift) & 0xff);
        hash = BigInt.asUintN(64, hash * 1099511628211n);
      }
    }
  return hash;
}

// A rendering's golden values: its rate, its frame count and its block hashes, each as a
// hexadecimal string like the fixture's, since a JSON number cannot hold 64 bits exactly.
export function measure({ sampleRate, channels }) {
  const frames = channels[0].length;
  const blocks = [];
  for (let from = 0; from < frames; from += kBlockFrames) {
    const hash = blockHash(channels, from, Math.min(from + kBlockFrames, frames));
    blocks.push(`0x${hash.toString(16).padStart(16, '0')}`);
  }
  return { rate: sampleRate, frames, blocks };
}

// The first block at which `actual` differs from `golden`: as many as `actual` has when every
// one of them matches.
export function firstDifferingBlock(golden, actual) {
  const common = Math.min(golden.blocks.length, actual.blocks.length);
  let block = 0;
  while (block < common && actual.blocks[block] === golden.blocks[block]) ++block;
  return block;
}

// '' when `actual` matches its golden values; otherwise its frame count against the golden
// one, and its first differing block, as GoldenRender.h words it.
export function difference(golden, actual) {
  const block = firstDifferingBlock(golden, actual);
  if (actual.frames === golden.frames && block === actual.blocks.length) return '';
  return `${actual.rate} Hz: ${actual.frames} frames (golden ${golden.frames}), first ` +
         `differing block ${block} (frames ${block * kBlockFrames} on)`;
}
