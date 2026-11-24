// PROTOTYPE, throwaway: midishare-playback ticket 02, the browser half of the probe.
// See index.html for the query parameters. Results go into <pre id="result"> as JSON, the
// title becomes "done" (or "failed"), and with ?report=1 they are posted to /result.
const params = new URLSearchParams(location.search);
const variant = params.get('variant') ?? 'wasmexc';
const CLIPS = ['preview', 'short', 'worst', 'long'];
const result = { variant, userAgent: navigator.userAgent };

async function sha256(view) {
  const digest = await crypto.subtle.digest('SHA-256', view);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('').slice(0, 16);
}

function compare(got, want, rate) {
  const r = { frames: got.length, refFrames: want.length, differing: 0, maxAbs: 0, maxAt: -1 };
  for (let i = 0, n = Math.min(got.length, want.length); i < n; ++i) {
    if (got[i] === want[i]) continue;
    ++r.differing;
    const d = Math.abs(got[i] - want[i]);
    if (d > r.maxAbs) { r.maxAbs = d; r.maxAt = i; }
  }
  if (r.maxAt >= 0) r.maxAtSeconds = r.maxAt / rate;
  return r;
}

async function run() {
  const t0 = performance.now();
  const factory = (await import(`./out/${variant}/render.mjs`)).default;
  const M = await factory();
  result.instantiateMs = performance.now() - t0; // module script fetch + wasm fetch, compile, instantiate
  if (params.get('only') === 'instantiate') return;

  const render = (bytes, rate, fn = '_modular_render') => {
    const ptr = M._malloc(bytes.length);
    M.HEAPU8.set(bytes, ptr);
    const t = performance.now();
    try {
      return { frames: M[fn](ptr, bytes.length, rate), ms: performance.now() - t };
    } finally {
      M._free(ptr);
    }
  };
  const view = (frames) => M.HEAPF32.subarray(M._modular_samples() / 4, M._modular_samples() / 4 + frames);

  const ctx = new AudioContext();
  result.audioContext = { sampleRate: ctx.sampleRate, state: ctx.state };
  const rates = [...new Set([44100, 48000, ctx.sampleRate])];
  result.renders = {};
  for (const clip of CLIPS) {
    const bytes = new Uint8Array(await (await fetch(`./out/clips/${clip}.mid`)).arrayBuffer());
    for (const rate of rates) {
      const r = render(bytes, rate);
      const entry = { frames: r.frames, renderMs: +r.ms.toFixed(1) };
      if (r.frames < 0) {
        entry.error = M.UTF8ToString(M._modular_error());
      } else {
        const got = view(r.frames);
        entry.sha256 = await sha256(got);
        entry.memoryMiB = +(M.HEAPU8.buffer.byteLength / 2 ** 20).toFixed(1);
        const ref = await fetch(`./out/native/${clip}-${rate}.f32`);
        if (ref.ok) entry.vsNative = compare(got, new Float32Array(await ref.arrayBuffer()), rate);
        if (rate === ctx.sampleRate && clip === 'worst') {
          // Hand the render to Web Audio straight from the module's memory, then start it
          // through a silent gain: the probe needs no audible sound.
          try {
            const t = performance.now();
            const buffer = ctx.createBuffer(1, r.frames, rate);
            buffer.copyToChannel(got, 0);
            const source = new AudioBufferSourceNode(ctx, { buffer });
            const silent = new GainNode(ctx, { gain: 0 });
            source.connect(silent).connect(ctx.destination);
            source.start();
            source.stop(ctx.currentTime + 0.1);
            result.playback = { ok: true, clip, seconds: buffer.duration, bufferMs: performance.now() - t,
              state: ctx.state };
          } catch (e) {
            result.playback = { ok: false, error: String(e) };
          }
        }
      }
      result.renders[`${clip}@${rate}`] = entry;
      M._modular_release();
    }
  }

  // Authoring errors, as in node_run.mjs: caught inside the module, and left uncaught.
  const outcome = (fn) => {
    try {
      const returned = fn();
      return { returned, message: returned < 0 ? M.UTF8ToString(M._modular_error()) : undefined };
    } catch (e) {
      const r = { thrown: true, type: e?.constructor?.name ?? typeof e, isError: e instanceof Error,
        isWasmException: typeof WebAssembly.Exception === 'function' && e instanceof WebAssembly.Exception,
        text: String(e).slice(0, 120) };
      if (typeof M.getExceptionMessage === 'function') r.message = M.getExceptionMessage(e);
      return r;
    }
  };
  const bad = new TextEncoder().encode('not a MIDI file');
  result.errors = {
    parseCaughtInCpp: outcome(() => render(bad, 48000).frames),
    parseUncaught: outcome(() => render(bad, 48000, '_modular_render_uncaught').frames),
    kernelCaughtInCpp: outcome(() => M._modular_kernel_error_caught()),
    kernelUncaught: outcome(() => M._modular_kernel_error_uncaught()),
  };
  const shortBytes = new Uint8Array(await (await fetch('./out/clips/short.mid')).arrayBuffer());
  const after = render(shortBytes, 48000);
  result.errors.renderAfterErrors = { frames: after.frames,
    sha256: after.frames > 0 ? await sha256(view(after.frames)) : null,
    sameAsBefore: after.frames > 0 && (await sha256(view(after.frames))) === result.renders['short@48000'].sha256 };
  M._modular_release();
  await ctx.close();
}

run().then(() => { document.title = 'done'; }, (e) => {
  result.failure = String(e?.stack ?? e);
  document.title = 'failed';
}).finally(async () => {
  document.getElementById('result').textContent = JSON.stringify(result, null, 1);
  if (params.get('report')) await fetch('/result', { method: 'POST', body: JSON.stringify(result) });
});
