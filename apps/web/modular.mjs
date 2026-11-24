// Modular's web loader: the surface a page or Node calls, over the WebAssembly module
// built from apps/web/Exports.cpp. modular.d.mts declares it and docs/web.md describes it.
//
// Every call returns a promise: the methods are async, so even a call that fails before it
// is queued rejects rather than throws. One queue per loaded module runs the calls one at a
// time, in call order, whichever instrument they name. A failed call rejects with an Error
// carrying Modular's message and leaves the module and its instruments usable.
import createModule from './modular_web.mjs';

export async function loadModular(options = {}) {
  // The glue finds its .wasm beside itself unless locateFile says otherwise.
  const settings = {};
  if (options.locateFile !== undefined) settings.locateFile = options.locateFile;
  const module = await createModule(settings);

  let queue = Promise.resolve();
  // Runs `work` once every call made before it has settled, whether or not it failed.
  const enqueue = (work) => {
    const result = queue.then(work);
    queue = result.then(ignore, ignore);
    return result;
  };

  const failure = () => new Error(module.UTF8ToString(module._modular_error()));

  // Calls `call` with the bytes copied into the module's memory, freed afterwards.
  const withBytes = (bytes, call) => {
    const pointer = module._malloc(bytes.length);
    if (pointer === 0 && bytes.length > 0) throw new Error('out of memory');
    try {
      // Read after the malloc, which may have grown the memory and replaced the view.
      module.HEAPU8.set(bytes, pointer >>> 0);
      return call(pointer, bytes.length);
    } finally {
      module._free(pointer);
    }
  };

  // A copy of `frames` floats from the module's memory, owned by JavaScript.
  const channel = (pointer, frames) => {
    const start = (pointer >>> 0) / Float32Array.BYTES_PER_ELEMENT;
    return module.HEAPF32.slice(start, start + frames);
  };

  // `made` is the instrument's handle in the module and the copy of the soundfont's bytes
  // it was made from, which it keeps for its life.
  const instrument = (made) => ({
    async render(midi, sampleRate) {
      // Copied now, so the page may change its array before the call runs.
      const bytes = new Uint8Array(midi);
      return enqueue(() => {
        const rendered = withBytes(bytes, (pointer, size) =>
          module._modular_render(made.handle, pointer, size, sampleRate));
        if (!rendered) throw failure();
        // The module holds this one rendering until its channels are copied out.
        try {
          const frames = module._modular_rendering_frames() >>> 0;
          return {
            sampleRate,
            channels: [
              channel(module._modular_rendering_left(), frames),
              channel(module._modular_rendering_right(), frames),
            ],
          };
        } finally {
          module._modular_release_rendering();
        }
      });
    },
  });

  return {
    async createInstrument(soundfont) {
      // The instrument's own copy, made once and never the page's array, which the page
      // may change or transfer.
      const bytes = new Uint8Array(soundfont);
      return enqueue(() => {
        const handle = withBytes(bytes, (pointer, size) =>
          module._modular_create_instrument(pointer, size));
        if (handle === 0) throw failure();
        return instrument({ handle, soundfont: bytes });
      });
    },
  };
}

function ignore() {}
