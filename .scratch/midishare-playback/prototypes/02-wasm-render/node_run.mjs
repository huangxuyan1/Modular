// PROTOTYPE, throwaway: midishare-playback ticket 02, the Node.js half of the probe.
//
//   & $env:EMSDK_NODE node_run.mjs <out folder>
//
// <out folder> is build.ps1's -Out, after build.ps1 ran. Needs the Release midi_cli
// (scripts/build_windows.ps1 -Configuration Release -Target midi_cli). Steps:
//
// 1. Native references: render_native.exe renders every clip at 44.1 and 48 kHz to raw
//    float32, and midi_cli renders every clip to a 16-bit WAV (48 kHz, all it offers).
// 2. Each module variant runs in a fresh Node process: instantiate time, render times,
//    exception behaviour, and a bit-for-bit comparison against the native float render and,
//    after the same 16-bit conversion as writeWav16, against midi_cli's WAV.
// 3. Prints a summary and writes <out folder>/node-results.json.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { gzipSync, brotliCompressSync, constants } from 'node:zlib';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../../..');
const CLIPS = ['preview', 'short', 'worst', 'long'];
const RATES = [44100, 48000];
const TIMED = ['noexc', 'jsexc', 'wasmexc', 'exnref'];
const VARIANTS = [...TIMED, 'jsexc-msg', 'wasmexc-msg', 'noexc-Oz', 'jsexc-Oz', 'wasmexc-Oz', 'exnref-Oz'];
const median = (xs) => [...xs].sort((a, b) => a - b)[Math.floor(xs.length / 2)];

// ---- comparison helpers, shared by the child --------------------------------------------
export function compare(got, want, rate) {
  const result = { frames: got.length, refFrames: want.length, differing: 0, maxAbs: 0,
    maxAt: -1, firstAt: -1 };
  const n = Math.min(got.length, want.length);
  for (let i = 0; i < n; ++i) {
    if (got[i] !== want[i] && !(Number.isNaN(got[i]) && Number.isNaN(want[i]))) {
      if (result.firstAt < 0) result.firstAt = i;
      ++result.differing;
      const d = Math.abs(got[i] - want[i]);
      if (d > result.maxAbs) { result.maxAbs = d; result.maxAt = i; }
    }
  }
  if (result.maxAt >= 0) result.maxAtSeconds = +(result.maxAt / rate).toFixed(4);
  if (result.firstAt >= 0) result.firstAtSeconds = +(result.firstAt / rate).toFixed(4);
  return result;
}

// writeWav16's conversion: NaN to 0, clamp, scale by 32767, truncate toward zero.
export function toPcm16(samples) {
  const out = new Int16Array(samples.length);
  for (let i = 0; i < samples.length; ++i) {
    let x = samples[i];
    if (Number.isNaN(x)) x = 0;
    x = Math.min(1, Math.max(-1, x));
    out[i] = Math.trunc(Math.fround(x * 32767)); // a float product in C++; exact in a double
  }
  return out;
}

function wavData(path) {
  const bytes = readFileSync(path);
  let at = 12;
  while (at + 8 <= bytes.length) {
    const id = bytes.toString('ascii', at, at + 4);
    const size = bytes.readUInt32LE(at + 4);
    if (id === 'data') return new Int16Array(bytes.buffer.slice(bytes.byteOffset + at + 8, bytes.byteOffset + at + 8 + size));
    at += 8 + size + (size & 1);
  }
  throw new Error('no data chunk');
}

function sha256(view) {
  return createHash('sha256').update(new Uint8Array(view.buffer, view.byteOffset, view.byteLength)).digest('hex').slice(0, 16);
}

// ---- child: one module variant -----------------------------------------------------------
async function child(out, variant, full) {
  const factory = (await import(pathToFileURL(join(out, variant, 'render.mjs')).href)).default;
  const t0 = performance.now();
  const M = await factory();
  const instantiateMs = performance.now() - t0;
  const result = { variant, instantiateMs };
  if (!full) return result;

  const call = (fn, ...args) => {
    try {
      return { returned: M[fn](...args) };
    } catch (e) {
      const caught = { thrown: true, type: e?.constructor?.name ?? typeof e,
        isWasmException: typeof WebAssembly.Exception === 'function' && e instanceof WebAssembly.Exception,
        isError: e instanceof Error, text: String(e).slice(0, 160) };
      if (typeof M.getExceptionMessage === 'function') {
        try { caught.message = M.getExceptionMessage(e); } catch (e2) { caught.message = 'getExceptionMessage threw: ' + e2; }
        try { M.decrementExceptionRefcount(e); } catch { /* not a C++ exception */ }
      }
      return caught;
    }
  };
  const render = (bytes, rate, fn = '_modular_render') => {
    const ptr = M._malloc(bytes.length);
    M.HEAPU8.set(bytes, ptr);
    const t = performance.now();
    const r = call(fn, ptr, bytes.length, rate);
    const ms = performance.now() - t;
    M._free(ptr);
    return { ...r, ms };
  };
  const samples = (frames) => M.HEAPF32.slice(M._modular_samples() / 4, M._modular_samples() / 4 + frames);

  result.renders = {};
  for (const clip of CLIPS) {
    const bytes = readFileSync(join(out, 'clips', `${clip}.mid`));
    for (const rate of RATES) {
      const key = `${clip}@${rate}`;
      const times = [];
      let r;
      const repeats = TIMED.includes(variant) && (clip === 'worst' || clip === 'long') ? 3 : 1;
      for (let i = 0; i < repeats; ++i) {
        r = render(bytes, rate);
        if (r.thrown || r.returned < 0) break;
        times.push(r.ms);
      }
      if (r.thrown || r.returned < 0) {
        result.renders[key] = { error: r.thrown ? r : M.UTF8ToString(M._modular_error()) };
        continue;
      }
      const got = samples(r.returned);
      const entry = { frames: r.returned, renderMs: times.map((t) => +t.toFixed(1)),
        medianMs: +median(times).toFixed(1), memoryMiB: +(M.HEAPU8.buffer.byteLength / 2 ** 20).toFixed(1),
        sha256: sha256(got) };
      const ref = join(out, 'native', `${clip}-${rate}.f32`);
      entry.vsNative = compare(got, new Float32Array(readFileSync(ref).buffer.slice(0)), rate);
      if (rate === 48000) {
        const wav = wavData(join(out, 'native', `${clip}-midi_cli.wav`));
        const pcm = toPcm16(got);
        entry.vsMidiCliWav = compare(pcm, wav, rate);
      }
      result.renders[key] = entry;
      M._modular_release();
    }
  }

  // Authoring errors: bad MIDI bytes (MidiFile::parse) and a kernel connect type mismatch.
  const bad = new TextEncoder().encode('not a MIDI file');
  const shortBytes = readFileSync(join(out, 'clips', 'short.mid'));
  const before = render(shortBytes, 48000);
  const beforeHash = sha256(samples(before.returned));
  M._modular_release();
  const errors = {};
  const caught = render(bad, 48000);
  errors.parseCaughtInCpp = caught.thrown ? caught
    : { returned: caught.returned, message: M.UTF8ToString(M._modular_error()) };
  errors.parseUncaught = render(bad, 48000, '_modular_render_uncaught');
  const kc = call('_modular_kernel_error_caught');
  errors.kernelCaughtInCpp = kc.thrown ? kc : { returned: kc.returned, message: M.UTF8ToString(M._modular_error()) };
  errors.kernelUncaught = call('_modular_kernel_error_uncaught');
  const after = render(shortBytes, 48000);
  errors.renderAfterErrors = after.thrown ? after
    : { frames: after.returned, sameSamples: sha256(samples(after.returned)) === beforeHash };
  M._modular_release();
  result.errors = errors;
  return result;
}

// ---- parent ------------------------------------------------------------------------------
function sizes(path) {
  const raw = readFileSync(path);
  return { raw: raw.length, gzip: gzipSync(raw, { level: 9 }).length,
    brotli: brotliCompressSync(raw, { params: { [constants.BROTLI_PARAM_QUALITY]: 11 } }).length };
}

function references(out) {
  const native = join(out, 'native');
  const cli = join(root, 'build', 'windows', 'Release', 'midi_cli.exe');
  const timings = {};
  for (const clip of CLIPS) {
    const mid = join(out, 'clips', `${clip}.mid`);
    for (const rate of RATES) {
      const times = [];
      for (let i = 0; i < (clip === 'worst' || clip === 'long' ? 3 : 1); ++i) {
        const line = execFileSync(join(native, 'render_native.exe'),
          [mid, String(rate), join(native, `${clip}-${rate}.f32`)], { encoding: 'utf8' });
        times.push(JSON.parse(line).renderMs);
      }
      timings[`${clip}@${rate}`] = { renderMs: times, medianMs: median(times) };
    }
    const wav = join(native, `${clip}-midi_cli.wav`);
    rmSync(wav, { force: true }); // midi_cli refuses to overwrite
    execFileSync(cli, [mid, wav], { encoding: 'utf8' });
  }
  // The native wrapper equals midi_cli: same samples after writeWav16's conversion.
  const check = {};
  for (const clip of CLIPS) {
    const f = new Float32Array(readFileSync(join(native, `${clip}-48000.f32`)).buffer.slice(0));
    check[clip] = compare(toPcm16(f), wavData(join(native, `${clip}-midi_cli.wav`)), 48000);
  }
  return { timings, nativeWrapperVsMidiCli: check };
}

// The sine voice's maths alone (PROTOTYPE_libm.cpp), native against WebAssembly: per note and
// rate, the pitch increment's bits and hashes of 30 s of double sines and float outputs; and,
// for notes 36, 69 and 95 at 48 kHz, every double sine value.
function libm(out) {
  const dir = join(out, 'libm');
  const native = execFileSync(join(dir, 'libm.exe'), [join(dir, 'native.f64')], { encoding: 'utf8' }).trim().split(/\r?\n/);
  const wasm = execFileSync(process.execPath, [join(dir, 'libm.js'), join(dir, 'wasm.f64')], { encoding: 'utf8' }).trim().split(/\r?\n/);
  const r = { runs: native.length, incrementsDiffer: 0, doubleRunsDiffer: 0, floatRunsDiffer: 0 };
  native.forEach((line, i) => {
    const [a, b] = [line.split(' '), wasm[i].split(' ')];
    if (a[2] !== b[2]) ++r.incrementsDiffer;
    if (a[3] !== b[3]) ++r.doubleRunsDiffer;
    if (a[4] !== b[4]) ++r.floatRunsDiffer;
  });
  const n = new Float64Array(readFileSync(join(dir, 'native.f64')).buffer.slice(0));
  const w = new Float64Array(readFileSync(join(dir, 'wasm.f64')).buffer.slice(0));
  const nb = new BigInt64Array(n.buffer), wb = new BigInt64Array(w.buffer);
  let differ = 0, maxUlp = 0n, floatsDiffer = 0;
  for (let i = 0; i < n.length; ++i) {
    if (nb[i] === wb[i]) continue;
    ++differ;
    const ulp = nb[i] > wb[i] ? nb[i] - wb[i] : wb[i] - nb[i]; // same sign: bit distance is ulps
    if (ulp > maxUlp) maxUlp = ulp;
    if (Math.fround(n[i]) !== Math.fround(w[i])) ++floatsDiffer;
  }
  Object.assign(r, { dumpedSines: n.length, sinesDiffer: differ, maxUlp: Number(maxUlp),
    floatCastsDiffer: floatsDiffer });
  rmSync(join(dir, 'native.f64')); rmSync(join(dir, 'wasm.f64'));
  return r;
}

async function parent(out) {
  const results = { node: process.version, references: references(out), libm: libm(out), variants: {} };
  for (const variant of VARIANTS) {
    const v = { wasm: sizes(join(out, variant, 'render.wasm')), glue: sizes(join(out, variant, 'render.mjs')) };
    const runs = [];
    for (let i = 0; i < 5; ++i) {
      const full = i === 0;
      const json = execFileSync(process.execPath, [fileURLToPath(import.meta.url), '--child', out, variant, full ? 'full' : 'instantiate'],
        { encoding: 'utf8', maxBuffer: 1 << 26 });
      runs.push(JSON.parse(json));
    }
    v.instantiateMs = runs.map((r) => +r.instantiateMs.toFixed(1));
    v.instantiateMedianMs = median(v.instantiateMs);
    Object.assign(v, { renders: runs[0].renders, errors: runs[0].errors });
    results.variants[variant] = v;
    console.error(`${variant}: done`);
  }
  writeFileSync(join(out, 'node-results.json'), JSON.stringify(results, null, 2));
  summarise(results);
}

function summarise(results) {
  const kb = (n) => (n / 1024).toFixed(1);
  console.log(`Node ${results.node}`);
  console.log('native wrapper vs midi_cli WAV:', Object.entries(results.references.nativeWrapperVsMidiCli)
    .map(([c, r]) => `${c} ${r.differing}/${r.frames} differ`).join(', '));
  console.log('sine maths, native vs wasm:', JSON.stringify(results.libm));
  console.log('native render ms (median):', Object.entries(results.references.timings)
    .map(([k, t]) => `${k} ${t.medianMs.toFixed(1)}`).join(', '));
  console.log('\nvariant      wasm KiB raw/gz/br      glue KiB raw/gz/br   inst ms');
  for (const [name, v] of Object.entries(results.variants)) {
    console.log(`${name.padEnd(12)} ${kb(v.wasm.raw)}/${kb(v.wasm.gzip)}/${kb(v.wasm.brotli)}`.padEnd(37) +
      `${kb(v.glue.raw)}/${kb(v.glue.gzip)}/${kb(v.glue.brotli)}`.padEnd(21) + `${v.instantiateMedianMs}`);
  }
  console.log('\nrender median ms, and vs native (differing samples, max abs)');
  for (const [name, v] of Object.entries(results.variants)) {
    const cells = Object.entries(v.renders).map(([k, r]) => r.error ? `${k} ERROR`
      : `${k} ${r.medianMs}ms ${r.vsNative.differing}/${r.vsNative.maxAbs.toExponential(2)}` +
        (r.vsMidiCliWav ? ` wav:${r.vsMidiCliWav.differing}` : ''));
    console.log(`${name}: ${cells.join(' | ')}`);
  }
  console.log('\nerrors');
  for (const [name, v] of Object.entries(results.variants)) console.log(name, JSON.stringify(v.errors));
}

if (process.argv[2] === '--child') {
  const r = await child(process.argv[3], process.argv[4], process.argv[5] === 'full');
  process.stdout.write(JSON.stringify(r));
} else if (process.argv[2]) {
  await parent(resolve(process.argv[2]));
} else {
  console.error('usage: node node_run.mjs <out folder>');
  process.exit(2);
}
