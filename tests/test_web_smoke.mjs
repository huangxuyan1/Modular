// The web module's smoke test (seam S2): what only JavaScript can see of the surface,
// through the shipped loader and module. The render core's own behaviour is tested in
// test_web_core.cpp. CTest runs it under the web preset:
//
//   node test_web_smoke.mjs <dist/modular.mjs> <soundfont written by web_smoke_fixture>
//                           <web-test-module/modular.mjs>
//                           <web-check/, the golden directory web_smoke_fixture writes>
//
// The build runs web_smoke_fixture before any test.
//
// The soundfont plays a looping sine panned full left, so a note sounds on the left
// channel and leaves the right one silent.
//
// The third path is the same loader beside the test module, a variant of the module built
// only for this test and never shipped (tests/web_test_module.cpp). Any allocation of
// 64 MiB or more traps in it, and it hands each instance it makes to the test, through
// globalThis.modularTestInstances, so the test can read its memory.
//
// The golden directory is the browser check page's folder, which holds the render core's
// golden preview (tests/WebGoldenClips.h): its soundfont, its MIDI bytes and its golden
// values at each rate. The test checks 48 kHz, with the golden hash ported to JavaScript
// in tests/web/golden_hash.mjs, which the check page shares.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { pathToFileURL } from 'node:url';
import { runInNewContext } from 'node:vm';
import { difference, measure } from './web/golden_hash.mjs';

const [loaderPath, soundfontPath, testLoaderPath, goldenPath] = process.argv.slice(2);
if (!loaderPath || !soundfontPath || !testLoaderPath || !goldenPath)
  throw new Error('usage: node test_web_smoke.mjs <modular.mjs> <soundfont> ' +
                  '<test modular.mjs> <golden dir>');
const { loadModular } = await import(pathToFileURL(loaderPath).href);
const { loadModular: loadTestModular } = await import(pathToFileURL(testLoaderPath).href);
const soundfont = new Uint8Array(readFileSync(soundfontPath));
const testInstances = [];
globalThis.modularTestInstances = testInstances;

// --- Standard MIDI Files ---------------------------------------------------------------

// At 480 ticks per quarter and the default 500,000 us per quarter, 960 ticks make a second.
const kTicksPerSecond = 960;

// A MIDI variable-length quantity: seven bits a byte, most significant first.
function variableLength(out, value) {
  const groups = [value & 0x7f];
  while ((value >>>= 7) !== 0) groups.unshift((value & 0x7f) | 0x80);
  out.push(...groups);
}

// A format 0 file of [tick, status, key, velocity] events in tick order, ending at endTick.
function midiFile(events, endTick) {
  const track = [];
  let at = 0;
  for (const [tick, ...message] of events) {
    variableLength(track, tick - at);
    at = tick;
    track.push(...message);
  }
  variableLength(track, endTick - at);
  track.push(0xff, 0x2f, 0);
  const length = [24, 16, 8, 0].map((shift) => (track.length >>> shift) & 0xff);
  return Uint8Array.from([0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0,
                          0x4d, 0x54, 0x72, 0x6b, ...length, ...track]);
}

// Middle C, held for `seconds`, and the file ends as it is let go.
function heldNote(seconds) {
  const end = seconds * kTicksPerSecond;
  return midiFile([[0, 0x90, 60, 100], [end, 0x80, 60, 0]], end);
}

const oneNote = () => heldNote(0.5);

const notMidi = () => new TextEncoder().encode('not a MIDI file');

// --- soundfonts ------------------------------------------------------------------------

// A RIFF file of another form, a WAVE, which no soundfont reader takes.
const notSoundfont = () => Uint8Array.from([0x52, 0x49, 0x46, 0x46, 4, 0, 0, 0,
                                            0x57, 0x41, 0x56, 0x45]);

// The test's soundfont marked as SoundFont 3: its ifil chunk's major version, the two
// bytes after the chunk's tag and size, becomes 3.
function soundfont3() {
  const bytes = soundfont.slice();
  const ifil = Buffer.from(bytes.buffer).indexOf('ifil');
  assert.ok(ifil > 0);
  bytes[ifil + 8] = 3;
  bytes[ifil + 9] = 0;
  return bytes;
}

// --- checks ----------------------------------------------------------------------------

function soundsOn(samples, frames) {
  return samples.subarray(0, frames).some((sample) => Math.abs(sample) >= 0.01);
}

const modular = await loadModular();

test('a page loads Modular, makes an instrument and renders two channels of equal length at the rate it asked for', async () => {
  const instrument = await modular.createInstrument(soundfont);
  for (const rate of [44100, 48000]) {
    const rendering = await instrument.render(oneNote(), rate);
    assert.equal(rendering.sampleRate, rate);
    assert.equal(rendering.channels.length, 2);
    const [left, right] = rendering.channels;
    assert.ok(left instanceof Float32Array && right instanceof Float32Array);
    assert.equal(left.length, right.length);
    // The file's half second, then at most two seconds of its release, at this rate.
    const fileFrames = rate / 2;
    assert.ok(left.length >= fileFrames && left.length <= fileFrames + 2 * rate);
    // Left comes first: the note sounds there, and the right channel is silent.
    assert.ok(soundsOn(left, fileFrames));
    assert.ok(right.every((sample) => sample === 0));
    // Each channel is a copy with its own buffer, not a view of the module's memory.
    assert.equal(left.buffer.byteLength, left.byteLength);
    assert.equal(right.buffer.byteLength, right.byteLength);
  }
});

test('the same bytes render to equal arrays, and a rendering keeps its samples after a longer render grows the module memory', async () => {
  const instrument = await modular.createInstrument(soundfont);
  const first = await instrument.render(oneNote(), 48000);
  const kept = first.channels.map((samples) => samples.slice());
  // A minute of stereo floats, 23 MB, is more than the module's initial 16 MiB of memory.
  const long = await instrument.render(heldNote(60), 48000);
  assert.ok(long.channels[0].length >= 60 * 48000);
  assert.ok(soundsOn(long.channels[0], 60 * 48000));
  const again = await instrument.render(oneNote(), 48000);
  assert.deepEqual(again.channels, kept);
  assert.deepEqual(first.channels, kept);
});

test('bad MIDI bytes reject with an Error carrying the parser message, and the next render succeeds', async () => {
  const instrument = await modular.createInstrument(soundfont);
  const before = await instrument.render(oneNote(), 48000);
  const bad = new TextEncoder().encode('not a MIDI file');
  await assert.rejects(instrument.render(bad, 48000),
                       (error) => error instanceof Error && error.message.startsWith('midi file:'));
  const after = await instrument.render(oneNote(), 48000);
  assert.deepEqual(after.channels, before.channels);
});

// Awaits each of `failures`, pairs of a call and a message, and checks that the call rejected
// with an Error carrying exactly that message.
async function rejectWith(failures) {
  for (const [call, message] of failures)
    await assert.rejects(call, (error) => error instanceof Error && error.message === message,
                         message);
}

// Records the order in which `calls` settle, then waits for every one of them.
async function settleInOrder(calls) {
  const settled = [];
  const results = await Promise.allSettled(
    calls.map((call, index) => call.finally(() => settled.push(index))));
  return { settled, results };
}

test('calls made without awaiting settle in call order across two instruments, and a failure rejects only its own call', async () => {
  const first = await modular.createInstrument(soundfont);
  const second = await modular.createInstrument(soundfont);
  const { settled, results } = await settleInOrder([
    first.render(heldNote(3), 48000),
    modular.createInstrument(soundfont),
    second.render(notMidi(), 48000),
    first.render(oneNote(), '48000'),
    modular.createInstrument(notSoundfont()),
    second.render(oneNote(), 48000),
  ]);
  assert.deepEqual(settled, [0, 1, 2, 3, 4, 5]);
  const [long, other, badMidi, badRate, badFont, short] = results;
  assert.ok(long.value.channels[0].length >= 3 * 48000);
  assert.equal(typeof other.value.render, 'function');
  assert.equal(badMidi.reason.message, 'midi file: missing MThd header');
  assert.equal(badRate.reason.message, 'render: sample rate must be a number');
  assert.equal(badFont.reason.message, 'soundfont: not a soundfont');
  assert.ok(short.value.channels[0].length < 3 * 48000);
});

test("each malformed input rejects with an Error carrying Modular's message, and the module and its instrument work on", async () => {
  const instrument = await modular.createInstrument(soundfont);
  const before = await instrument.render(oneNote(), 48000);
  const finiteAndPositive = 'render: sample rate must be finite and positive';
  await rejectWith([
    [modular.createInstrument(notSoundfont()), 'soundfont: not a soundfont'],
    [modular.createInstrument(new Uint8Array(0)), 'soundfont: truncated chunk'],
    [modular.createInstrument(soundfont3()),
     'soundfont: compressed samples (sf3) are not supported'],
    [instrument.render(notMidi(), 48000), 'midi file: missing MThd header'],
    ...[0, -48000, NaN, Infinity, -Infinity].map(
      (rate) => [instrument.render(oneNote(), rate), finiteAndPositive]),
    // A minute at 10 MHz: each channel would take 2.5 GB, past the module's 2 GiB.
    [instrument.render(heldNote(60), 1e7), 'out of memory'],
    // At 1 GHz a channel would need more floats than a 32-bit vector can hold.
    [instrument.render(midiFile([], 1), 1e9), 'render: too many frames'],
    [instrument.render(midiFile([], 0), 1e300), 'render: too many frames'],
    // One tick past ten minutes. Under Node only the refusal is checked; the native
    // render core's tests render a file of exactly ten minutes.
    [instrument.render(midiFile([], 600 * kTicksPerSecond + 1), 48000),
     'render: file is longer than 10 minutes'],
  ]);
  assert.deepEqual((await instrument.render(oneNote(), 48000)).channels, before.channels);
  const fresh = await modular.createInstrument(soundfont);
  assert.deepEqual((await fresh.render(oneNote(), 48000)).channels, before.channels);
});

test('a file of zero duration is no error: it renders two empty channels', async () => {
  const instrument = await modular.createInstrument(soundfont);
  const rendering = await instrument.render(midiFile([[0, 0x90, 60, 100]], 0), 48000);
  assert.equal(rendering.sampleRate, 48000);
  assert.equal(rendering.channels.length, 2);
  for (const samples of rendering.channels) {
    assert.ok(samples instanceof Float32Array);
    assert.equal(samples.length, 0);
  }
});

test('the loader refuses an argument of the wrong type with an Error, before the module sees it', async () => {
  const instrument = await modular.createInstrument(soundfont);
  const midi = oneNote();
  const notBytes = 'soundfont: bytes must be a Uint8Array';
  const notMidiBytes = 'midi file: bytes must be a Uint8Array';
  const notANumber = 'render: sample rate must be a number';
  // The module itself would take each of these: as a view, a copy or a coerced number.
  await rejectWith([
    [modular.createInstrument(soundfont.slice().buffer), notBytes],
    [modular.createInstrument(Array.from(soundfont)), notBytes],
    [modular.createInstrument(), notBytes],
    [instrument.render(midi.slice().buffer, 48000), notMidiBytes],
    [instrument.render(Array.from(midi), 48000), notMidiBytes],
    [instrument.render(midi, '48000'), notANumber],
    [instrument.render(midi, 48000n), notANumber],
    [instrument.render(midi), notANumber],
  ]);
  const rendering = await instrument.render(midi, 48000);
  assert.ok(soundsOn(rendering.channels[0], 24000));

  // A Uint8Array from another realm, such as an iframe's, is still one.
  const foreign = (bytes) => runInNewContext('Uint8Array.from(bytes)', { bytes });
  const foreignFont = foreign(soundfont);
  assert.ok(!(foreignFont instanceof Uint8Array));
  const foreignInstrument = await modular.createInstrument(foreignFont);
  assert.deepEqual((await foreignInstrument.render(foreign(midi), 48000)).channels,
                   rendering.channels);
});

test("changing the page's arrays after a call changes nothing", async () => {
  const font = soundfont.slice();
  const making = modular.createInstrument(font);
  font.fill(0);
  const instrument = await making;
  const midi = oneNote();
  const rendering = instrument.render(midi, 48000);
  midi.fill(0);
  const result = await rendering;

  const fresh = await modular.createInstrument(soundfont);
  const reference = await fresh.render(oneNote(), 48000);
  assert.deepEqual(result.channels, reference.channels);
  assert.deepEqual((await instrument.render(oneNote(), 48000)).channels, reference.channels);
});

test('loadModular passes locateFile through to the module', async () => {
  const asked = [];
  const located = await loadModular({
    locateFile: (path, prefix) => {
      asked.push(path);
      return prefix + path;
    },
  });
  assert.deepEqual(asked, ['modular_web.wasm']);
  const instrument = await located.createInstrument(soundfont);
  assert.ok(soundsOn((await instrument.render(oneNote(), 48000)).channels[0], 24000));
});

test('the module reports the commit and Emscripten version that built it, as the manifest beside it does', async () => {
  const { version } = modular;
  assert.match(version.commit, /^[0-9a-f]{40}$/);
  assert.equal(typeof version.dirty, 'boolean');
  assert.match(version.emscripten, /^\d+\.\d+\.\d+$/);

  const shipped = (name) => readFileSync(join(dirname(loaderPath), name));
  const manifest = JSON.parse(shipped('modular.manifest.json').toString('utf8'));
  assert.equal(manifest.commit, version.commit);
  assert.equal(manifest.dirty, version.dirty);
  assert.equal(manifest.emscripten, version.emscripten);
  assert.match(manifest.flags.compile, /-fwasm-exceptions/);
  assert.match(manifest.flags.link, /-fwasm-exceptions/);
  // Every shipped file, as it is on disk. The files and the manifest are all the folder
  // holds, so CI uploads the folder as it is.
  const names = ['modular.d.mts', 'modular.mjs', 'modular_web.mjs', 'modular_web.wasm'];
  assert.deepEqual(manifest.files.map((file) => file.name), names);
  assert.deepEqual(readdirSync(dirname(loaderPath)).sort(),
                   [...names, 'modular.manifest.json'].sort());
  for (const file of manifest.files) {
    const bytes = shipped(file.name);
    assert.equal(file.size, bytes.length, file.name);
    assert.equal(file.sha256, createHash('sha256').update(bytes).digest('hex'), file.name);
  }
  // The test module is built from the same sources, its version included.
  assert.deepEqual((await loadTestModular()).version, version);
});

// --- the test module ---------------------------------------------------------------------

// Six minutes at 48 kHz: each channel of the rendering takes 66 MiB, so the test module
// traps while the render sizes it.
const trapping = () => heldNote(360);

test('a trap rejects only its own call, and the next call runs on a fresh instance where every instrument renders as before', async () => {
  testInstances.length = 0;
  const asked = [];
  const modular = await loadTestModular({
    locateFile: (path, prefix) => {
      asked.push(path);
      return prefix + path;
    },
  });
  const first = await modular.createInstrument(soundfont);
  // The page's array is transferred away once the instrument is made, so an instrument
  // made again from the page's array would have no bytes at all.
  const pageBytes = soundfont.slice();
  const second = await modular.createInstrument(pageBytes);
  pageBytes.buffer.transfer();
  const before = await first.render(oneNote(), 48000);

  // Made without awaiting. The trap stops the instance, and the call after it loads a fresh
  // one, asynchronously, while the calls after that wait their turns.
  const { settled, results } = await settleInOrder([
    first.render(oneNote(), 48000),
    second.render(trapping(), 48000),
    first.render(oneNote(), '48000'),
    second.render(notMidi(), 48000),
    modular.createInstrument(soundfont),
    first.render(oneNote(), 48000),
    second.render(oneNote(), 48000),
  ]);
  assert.deepEqual(settled, [0, 1, 2, 3, 4, 5, 6]);
  assert.deepEqual(results.map((result) => result.status),
                   ['fulfilled', 'rejected', 'rejected', 'rejected', 'fulfilled', 'fulfilled',
                    'fulfilled']);
  const [early, trapped, badRate, badMidi, made, afterFirst, afterSecond] = results;
  assert.ok(trapped.reason instanceof Error);
  assert.ok(trapped.reason.cause instanceof WebAssembly.RuntimeError);
  assert.equal(trapped.reason.message, `module stopped: ${trapped.reason.cause.message}`);
  assert.equal(badRate.reason.message, 'render: sample rate must be a number');
  assert.equal(badMidi.reason.message, 'midi file: missing MThd header');
  for (const rendering of [early.value, afterFirst.value, afterSecond.value])
    assert.deepEqual(rendering.channels, before.channels);
  // Within the calls made together, the first that needed the module after the trap loaded
  // one fresh instance, found with the same locateFile, and the calls after it waited for it.
  assert.equal(testInstances.length, 2);
  assert.deepEqual(asked, ['modular_web.wasm', 'modular_web.wasm']);
  assert.deepEqual((await made.value.render(oneNote(), 48000)).channels, before.channels);
  assert.equal(testInstances.length, 2);
});

test('a fresh instance that cannot load rejects the call that needed it, and the next call loads one again', async () => {
  let asks = 0;
  // The second instance looks for a .wasm that is not there.
  const modular = await loadTestModular({
    locateFile: (path, prefix) => prefix + (++asks === 2 ? 'missing.wasm' : path),
  });
  const instrument = await modular.createInstrument(soundfont);
  const before = await instrument.render(oneNote(), 48000);
  await assert.rejects(instrument.render(trapping(), 48000),
                       (error) => error.message.startsWith('module stopped: '));
  // Emscripten reports the failed load on the console as well; the test keeps it quiet.
  const report = console.error;
  console.error = () => {};
  try {
    await assert.rejects(instrument.render(oneNote(), 48000),
                         (error) => error instanceof Error && error.message.includes('missing.wasm'));
  } finally {
    console.error = report;
  }
  assert.deepEqual((await instrument.render(oneNote(), 48000)).channels, before.channels);
  assert.equal(asks, 3);
});

test('rendering a clip again and again does not grow the module memory, and each rendering is released once copied out', async (t) => {
  testInstances.length = 0;
  const modular = await loadTestModular();
  const [module] = testInstances;
  const measure = () => ({
    memory: module.HEAPU8.buffer.byteLength,
    heap: module._modular_test_heap_bytes(),
  });
  const instrument = await modular.createInstrument(soundfont);
  // A minute: a 23 MB rendering, more than the initial 16 MiB, so the first render grows
  // the memory.
  const clip = heldNote(60);
  const made = measure();
  const first = await instrument.render(clip, 48000);
  const rendered = measure();
  const renderingBytes = first.channels[0].byteLength + first.channels[1].byteLength;
  const repeats = 20;
  for (let i = 0; i < repeats; ++i) {
    const again = await instrument.render(clip, 48000);
    assert.equal(again.channels[0].length, first.channels[0].length);
  }
  const repeated = measure();
  t.diagnostic(`rendering: ${renderingBytes} bytes`);
  t.diagnostic(`instrument made: memory ${made.memory}, heap in use ${made.heap}`);
  t.diagnostic(`first render: memory ${rendered.memory}, heap in use ${rendered.heap}`);
  t.diagnostic(`${repeats} more: memory ${repeated.memory}, heap in use ${repeated.heap}`);
  assert.ok(rendered.memory > made.memory);
  assert.equal(repeated.memory, rendered.memory);
  assert.equal(repeated.heap, rendered.heap);
  // What the first render left in use is far less than its rendering: the kept MIDI file
  // and the nodes' state, not the samples.
  assert.ok(rendered.heap - made.heap < renderingBytes / 10);
});

// --- golden renders --------------------------------------------------------------------

test("the golden preview renders through the loader to the render core's golden frame count and block hashes", async () => {
  const read = (name) => new Uint8Array(readFileSync(join(goldenPath, name)));
  const { renders } = JSON.parse(Buffer.from(read('preview.json')).toString('utf8'));
  const golden = renders.find((render) => render.rate === 48000);
  const instrument = await modular.createInstrument(read('preview.sf2'));
  const actual = measure(await instrument.render(read('preview.mid'), golden.rate));
  const problem = difference(golden, actual);
  assert.equal(problem, '', `${problem}\nthe render's values: ${JSON.stringify(actual)}`);
});
