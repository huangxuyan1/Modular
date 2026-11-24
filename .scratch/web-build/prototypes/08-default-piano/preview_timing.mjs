// PROTOTYPE, throwaway: web build ticket 08's preview timing (map ticket 04, decision 8).
// Served by PROTOTYPE_browsers.py, which maps /dist/ to the built module and /piano.sf2 to
// the default piano's extract, outside the repository. On the page's main thread, as a page
// calls it today, it times:
//
// - importing the loader and its glue, then loadModular();
// - fetching the extract (from 127.0.0.1, so not a network's time), then createInstrument();
// - the first preview render after createInstrument, then later ones.
//
// A preview is midishare's: one key held 0.25 s at velocity 105, then its release; the file
// ends as the key is let go, so the release is the rendering's tail (map ticket 04,
// decision 8, and the golden preview of tests/WebGoldenClips.h). The render runs on the
// calling thread, so each render's time is also how long it blocks the page.
//
// Posts the numbers to /result, and shows them.
const out = document.getElementById('out');
const now = () => performance.now();
const round = (ms) => Math.round(ms * 10) / 10;
const median = (values) => {
  const sorted = [...values].sort((a, b) => a - b);
  const middle = sorted.length >> 1;
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
};

// A format 0 file at 480 ticks a quarter and 500,000 us a quarter: `key` on at tick 0 at
// velocity 105, off at tick 240 (0.25 s), where the file ends.
function preview(key) {
  const track = [0x00, 0x90, key, 105, 0x81, 0x70, 0x80, key, 0, 0x00, 0xff, 0x2f, 0x00];
  return Uint8Array.from([0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0,
                          0x4d, 0x54, 0x72, 0x6b, 0, 0, 0, track.length, ...track]);
}

async function timeRenders(piano, key, rate, count) {
  const times = [];
  let frames;
  for (let i = 0; i < count; ++i) {
    const start = now();
    const rendering = await piano.render(preview(key), rate);
    times.push(now() - start);
    frames = rendering.channels[0].length;
  }
  return { key, rate, frames, count, medianMs: round(median(times)),
           minMs: round(Math.min(...times)), maxMs: round(Math.max(...times)) };
}

async function run() {
  const result = { userAgent: navigator.userAgent, cores: navigator.hardwareConcurrency,
                   crossOriginIsolated };
  let start = now();
  const { loadModular } = await import('/dist/modular.mjs');
  result.importMs = round(now() - start);
  start = now();
  const modular = await loadModular();
  result.loadModularMs = round(now() - start);
  result.version = modular.version;

  start = now();
  const soundfont = new Uint8Array(await (await fetch('/piano.sf2')).arrayBuffer());
  result.fetchSoundfontMs = round(now() - start);
  result.soundfontBytes = soundfont.length;
  start = now();
  const piano = await modular.createInstrument(soundfont);
  result.createInstrumentMs = round(now() - start);

  start = now();
  const first = await piano.render(preview(60), 48000);
  result.firstRender = { key: 60, rate: 48000, frames: first.channels[0].length,
                         ms: round(now() - start) };
  result.later = [
    await timeRenders(piano, 60, 48000, 25),
    await timeRenders(piano, 21, 48000, 11),
    await timeRenders(piano, 108, 48000, 11),
    await timeRenders(piano, 60, 44100, 11),
  ];
  return result;
}

let result;
try {
  result = await run();
} catch (error) {
  result = { failure: String(error?.message ?? error) };
}
out.textContent = JSON.stringify(result, null, 1);
if (new URLSearchParams(location.search).has('report'))
  await fetch('/result', { method: 'POST', body: JSON.stringify({ page: 'timing', ...result }) });
