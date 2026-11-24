// The web module's smoke test (seam S2): what only JavaScript can see of the surface,
// through the shipped loader and module. The render core's own behaviour is tested in
// test_web_core.cpp. CTest runs it under the web preset:
//
//   node test_web_smoke.mjs <dist/modular.mjs> <soundfont written by web_smoke_fixture>
//
// The soundfont plays a looping sine panned full left, so a note sounds on the left
// channel and leaves the right one silent.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { pathToFileURL } from 'node:url';

const [loaderPath, soundfontPath] = process.argv.slice(2);
if (!loaderPath || !soundfontPath)
  throw new Error('usage: node test_web_smoke.mjs <modular.mjs> <soundfont>');
const { loadModular } = await import(pathToFileURL(loaderPath).href);
const soundfont = new Uint8Array(readFileSync(soundfontPath));

// --- Standard MIDI Files ---------------------------------------------------------------

// At 480 ticks per quarter and the default 500,000 us per quarter, 960 ticks make a second.
const kTicksPerSecond = 960;

function variable(out, value) {
  const groups = [value & 0x7f];
  while ((value >>>= 7) !== 0) groups.unshift((value & 0x7f) | 0x80);
  out.push(...groups);
}

// A format 0 file of [tick, status, key, velocity] events in tick order, ending at endTick.
function midiFile(events, endTick) {
  const track = [];
  let at = 0;
  for (const [tick, ...message] of events) {
    variable(track, tick - at);
    at = tick;
    track.push(...message);
  }
  variable(track, endTick - at);
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

// --- checks ----------------------------------------------------------------------------

function soundsOn(samples, frames) {
  return samples.subarray(0, frames).some((sample) => Math.abs(sample) >= 0.01);
}

const modular = await loadModular();

test('a page loads Modular, makes an instrument and renders two channels of equal length at the rate it asked for', async () => {
  const piano = await modular.createInstrument(soundfont);
  for (const rate of [44100, 48000]) {
    const rendering = await piano.render(oneNote(), rate);
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
  const piano = await modular.createInstrument(soundfont);
  const first = await piano.render(oneNote(), 48000);
  const kept = first.channels.map((samples) => samples.slice());
  // A minute of stereo floats, 23 MB, is more than the module's initial 16 MiB of memory.
  const long = await piano.render(heldNote(60), 48000);
  assert.ok(long.channels[0].length >= 60 * 48000);
  assert.ok(soundsOn(long.channels[0], 60 * 48000));
  const again = await piano.render(oneNote(), 48000);
  assert.deepEqual(again.channels, kept);
  assert.deepEqual(first.channels, kept);
});

test('bad MIDI bytes reject with an Error carrying the parser message, and the next render succeeds', async () => {
  const piano = await modular.createInstrument(soundfont);
  const before = await piano.render(oneNote(), 48000);
  const bad = new TextEncoder().encode('not a MIDI file');
  await assert.rejects(piano.render(bad, 48000),
                       (error) => error instanceof Error && error.message.startsWith('midi file:'));
  const after = await piano.render(oneNote(), 48000);
  assert.deepEqual(after.channels, before.channels);
});

test('calls made without awaiting settle in call order, across instruments', async () => {
  const piano = await modular.createInstrument(soundfont);
  const settled = [];
  const calls = [
    piano.render(heldNote(3), 48000),
    modular.createInstrument(soundfont),
    piano.render(oneNote(), 48000),
  ].map((call, index) => call.then((result) => {
    settled.push(index);
    return result;
  }));
  const [long, other, short] = await Promise.all(calls);
  assert.deepEqual(settled, [0, 1, 2]);
  assert.ok(long.channels[0].length >= 3 * 48000);
  assert.ok(short.channels[0].length < 3 * 48000);
  assert.equal(typeof other.render, 'function');
});

test("changing the page's arrays after a call changes nothing", async () => {
  const font = soundfont.slice();
  const making = modular.createInstrument(font);
  font.fill(0);
  const piano = await making;
  const midi = oneNote();
  const rendering = piano.render(midi, 48000);
  midi.fill(0);
  const result = await rendering;

  const fresh = await modular.createInstrument(soundfont);
  const reference = await fresh.render(oneNote(), 48000);
  assert.deepEqual(result.channels, reference.channels);
  assert.deepEqual((await piano.render(oneNote(), 48000)).channels, reference.channels);
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
  const piano = await located.createInstrument(soundfont);
  assert.ok(soundsOn((await piano.render(oneNote(), 48000)).channels[0], 24000));
});
