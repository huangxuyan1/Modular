// The web check page's script (index.html). It renders the golden preview through the
// shipped loader and module, at each rate whose golden values the build wrote beside it,
// and shows the frame count and every block's hash beside the golden ones. The preview,
// its soundfont and its values come from tests/WebGoldenClips.h, which the render core's
// tests check natively and under Node, so a browser that matches every hash renders the
// preview bit for bit as the native build does.
//
// The folder holds, as the build writes it (build/web/web-check/):
//   index.html, check.mjs, golden_hash.mjs    this page
//   modular/                                  the module's files, as dist/ holds them
//   preview.sf2, preview.mid, preview.json    the golden preview and its values
//
// globalThis.modularCheck is a promise of the outcome, for a page that drives this one.
import { difference, firstDifferingBlock, measure } from './golden_hash.mjs';

const verdict = document.getElementById('verdict');
const about = document.getElementById('about');
const renders = document.getElementById('renders');

function element(tag, text, className) {
  const made = document.createElement(tag);
  if (text !== undefined) made.textContent = text;
  if (className !== undefined) made.className = className;
  return made;
}

function describe(term, detail) {
  about.append(element('dt', term), element('dd', detail));
}

async function fetchBytes(name) {
  const response = await fetch(name, { cache: 'no-store' });
  if (!response.ok) throw new Error(`${name}: ${response.status} ${response.statusText}`);
  return new Uint8Array(await response.arrayBuffer());
}

// A table of one rate's frame count and blocks, the rendered beside the golden.
function showRender(golden, actual, problem) {
  const matched = problem === '';
  const section = element('section');
  section.append(element('h2', `${golden.rate} Hz: ${matched ? 'every hash matched' : 'mismatch'}`,
                         matched ? 'match' : 'mismatch'));
  if (!matched) section.append(element('p', problem, 'mismatch'));
  const table = element('table');
  const head = element('tr');
  for (const title of ['', 'Golden', 'Rendered', ''])
    head.append(element('th', title));
  table.append(head);
  const row = (label, expected, rendered) => {
    const equal = expected === rendered;
    const line = element('tr');
    line.append(element('th', label), element('td', String(expected ?? '')),
                element('td', String(rendered ?? '')),
                element('td', equal ? '✓' : '✗', equal ? 'match' : 'mismatch'));
    table.append(line);
  };
  row('Frames', golden.frames, actual.frames);
  const blocks = Math.max(golden.blocks.length, actual.blocks.length);
  for (let block = 0; block < blocks; ++block)
    row(`Block ${block}`, golden.blocks[block], actual.blocks[block]);
  section.append(table);
  renders.append(section);
}

async function check() {
  describe('Browser', navigator.userAgent);
  const { loadModular } = await import('./modular/modular.mjs');
  const [soundfont, midi, values] = await Promise.all(
    ['preview.sf2', 'preview.mid', 'preview.json'].map(fetchBytes));
  const { renders: goldens } = JSON.parse(new TextDecoder().decode(values));
  const modular = await loadModular();
  const { commit, dirty, emscripten } = modular.version;
  describe('Module', `${commit}${dirty ? ', built from uncommitted changes' : ''}`);
  describe('Emscripten', emscripten);
  const instrument = await modular.createInstrument(soundfont);
  const outcome = { matched: true, userAgent: navigator.userAgent, version: modular.version,
                    renders: [] };
  for (const golden of goldens) {
    const actual = measure(await instrument.render(midi, golden.rate));
    const problem = difference(golden, actual);
    showRender(golden, actual, problem);
    outcome.matched &&= problem === '';
    outcome.renders.push({ rate: golden.rate, frames: actual.frames, blocks: actual.blocks.length,
                           matchingBlocks: firstDifferingBlock(golden, actual), problem });
  }
  const rates = goldens.map((golden) => golden.rate).join(', ');
  verdict.textContent = outcome.matched
    ? `Every hash matched, at ${rates} Hz: this browser renders the golden preview bit for ` +
      'bit as the native build does.'
    : 'Mismatch: this browser does not render the golden preview as the native build does.';
  verdict.className = outcome.matched ? 'match' : 'mismatch';
  return outcome;
}

globalThis.modularCheck = check().catch((error) => {
  verdict.textContent = `The check failed: ${error?.message ?? error}`;
  verdict.className = 'mismatch';
  return { matched: false, userAgent: navigator.userAgent, error: String(error?.message ?? error) };
});
