// PROTOTYPE, throwaway: web build ticket 08's Node half. Renders the default piano's extract
// through the shipped loader and module, at the rates a native render (PROTOTYPE_piano_native)
// wrote, and compares the golden values of tests/GoldenRender.h:
//
//   node PROTOTYPE_piano_node.mjs <dist/modular.mjs> <soundfont> <clip.mid> <native.json>
//
// Each rate renders through a fresh instrument, as the native harness does. Prints each
// rate's frame count, block count, wall time and verdict; exits 1 on any difference.
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';
import { pathToFileURL } from 'node:url';
import { difference, measure } from '../../../../tests/web/golden_hash.mjs';

const [loaderPath, soundfontPath, clipPath, nativePath] = process.argv.slice(2);
if (!nativePath) {
  console.error('usage: node PROTOTYPE_piano_node.mjs <modular.mjs> <soundfont> <clip.mid> ' +
                '<native.json>');
  process.exit(2);
}
const { loadModular } = await import(pathToFileURL(loaderPath).href);
const soundfont = new Uint8Array(readFileSync(soundfontPath));
const midi = new Uint8Array(readFileSync(clipPath));
const native = JSON.parse(readFileSync(nativePath, 'utf8'));

const modular = await loadModular();
console.log(`module ${modular.version.commit}${modular.version.dirty ? ' (dirty)' : ''}, ` +
            `Emscripten ${modular.version.emscripten}, Node ${process.version}`);
let failed = false;
for (const golden of native.renders) {
  const instrument = await modular.createInstrument(soundfont);
  const start = performance.now();
  const rendering = await instrument.render(midi, golden.rate);
  const renderMs = performance.now() - start;
  const actual = measure(rendering);
  const problem = difference(golden, actual);
  failed ||= problem !== '';
  console.log(`${golden.rate} Hz: ${actual.frames} frames, ${actual.blocks.length} blocks, ` +
              `${renderMs.toFixed(1)} ms under Node (native ${golden.renderMs} ms): ` +
              (problem === '' ? 'every block equal' : problem));
}
process.exit(failed ? 1 : 0);
