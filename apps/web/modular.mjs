// Modular's web loader: the surface a page or Node calls, over the WebAssembly module
// built from apps/web/Exports.cpp. modular.d.mts declares it and docs/web.md describes it.
//
// Every call returns a promise: the methods are async, so even a call that fails before it
// is queued rejects rather than throws. One queue per loaded module runs the calls one at a
// time, in call order, whichever instrument they name, and a call that fails, its
// arguments' check included, settles in its own turn. A failed call rejects with an Error
// carrying Modular's message and leaves the module and its instruments usable.
//
// The exports catch everything C++ throws, so an exception out of one can only be a
// WebAssembly trap or an abort. That call rejects with "module stopped: ...", the instance
// is discarded, and the next call instantiates a fresh one. Each instrument then makes
// itself again on the fresh instance, from its own copy of the soundfont's bytes.
import createModule from './modular_web.mjs';

export async function loadModular(options = {}) {
  const { locateFile } = options;
  // The live instance, until a trap stops it.
  let instance = await instantiate(locateFile);
  // What built the module. A fresh instance after a trap is the same build.
  const { version } = instance;
  // The live instance, or a fresh one in place of a stopped one.
  const live = async () => {
    if (instance.stopped) instance = await instantiate(locateFile);
    return instance;
  };

  let queue = Promise.resolve();
  // Runs `work` with the live instance once every call made before it has settled, whether
  // or not it failed. A call whose arguments failed their check, `problem`, rejects in that
  // turn without reaching the module.
  const run = (problem, work) => {
    const result = queue.then(async () => {
      if (problem !== undefined) throw new Error(problem);
      return work(await live());
    });
    queue = result.then(ignore, ignore);
    return result;
  };

  // An instrument: its own copy of the soundfont's bytes, kept for its life, and its handle
  // in the instance that made it.
  const instrument = (soundfont, owner, handle) => {
    // The instrument's handle in `current`, made there from its copy if it was made in an
    // instance a trap has since stopped.
    const handleIn = (current) => {
      if (current !== owner) {
        handle = current.createInstrument(soundfont);
        owner = current;
      }
      return handle;
    };
    return {
      async render(midi, sampleRate) {
        const problem = !isBytes(midi) ? 'midi file: bytes must be a Uint8Array'
          : typeof sampleRate !== 'number' ? 'render: sample rate must be a number'
          : undefined;
        // Copied now, so the page may change its array before the call runs.
        const bytes = problem === undefined ? new Uint8Array(midi) : undefined;
        return run(problem, (current) =>
          current.render(handleIn(current), bytes, sampleRate));
      },
    };
  };

  return {
    version,
    async createInstrument(soundfont) {
      const problem = !isBytes(soundfont) ? 'soundfont: bytes must be a Uint8Array' : undefined;
      // The instrument's own copy, made once and never the page's array, which the page
      // may change or transfer.
      const bytes = problem === undefined ? new Uint8Array(soundfont) : undefined;
      return run(problem, (current) =>
        instrument(bytes, current, current.createInstrument(bytes)));
    },
  };
}

// Whether `value` is a Uint8Array, a Node Buffer included, from this realm or another, such
// as an iframe's. The typed arrays' own toStringTag getter names a typed array's kind and
// gives undefined for anything else, so no other object can pass for one.
const typedArrayKind = Object.getOwnPropertyDescriptor(
  Object.getPrototypeOf(Uint8Array.prototype), Symbol.toStringTag).get;
const isBytes = (value) => typedArrayKind.call(value) === 'Uint8Array';

// One instance of the module, behind the calls the loader makes to it. A method returns
// what the module gives, or throws an Error: Modular's message when an export fails, or
// "module stopped: ..." when one traps, which stops the instance for good.
async function instantiate(locateFile) {
  // Emscripten makes the object it is given into the instance, so each gets its own. The
  // glue finds its .wasm beside itself unless locateFile says otherwise.
  const settings = {};
  if (locateFile !== undefined) settings.locateFile = locateFile;
  let module = await createModule(settings);
  // The Error of the trap or abort that stopped this instance, once one has.
  let stop;

  // Calls an export. Every export catches whatever C++ throws, so an exception here is a
  // trap or an abort, after which nothing in this instance can be trusted: no later call
  // reaches it, and the module and its memory are let go at once, even while an
  // instrument made here still holds this instance.
  const call = (name, ...args) => {
    if (stop !== undefined) throw stop;
    try {
      return module[name](...args);
    } catch (error) {
      stop = new Error(`module stopped: ${error?.message ?? error}`, { cause: error });
      module = undefined;
      throw stop;
    }
  };

  const failure = () => new Error(module.UTF8ToString(call('_modular_error')));

  // What built the module, baked in by the build (apps/web/Version.cpp).
  const version = {
    commit: module.UTF8ToString(call('_modular_version_commit')),
    dirty: call('_modular_version_dirty') !== 0,
    emscripten: module.UTF8ToString(call('_modular_version_emscripten')),
  };

  // Calls `work` with the bytes copied into the module's memory, freed afterwards unless the
  // instance has stopped.
  const withBytes = (bytes, work) => {
    const pointer = call('_malloc', bytes.length);
    if (pointer === 0 && bytes.length > 0) throw new Error('out of memory');
    try {
      // Read after the malloc, which may have grown the memory and replaced the view.
      module.HEAPU8.set(bytes, pointer >>> 0);
      return work(pointer, bytes.length);
    } finally {
      if (stop === undefined) call('_free', pointer);
    }
  };

  // A copy of `frames` floats from the module's memory, owned by JavaScript.
  const channel = (pointer, frames) => {
    const start = (pointer >>> 0) / Float32Array.BYTES_PER_ELEMENT;
    return module.HEAPF32.slice(start, start + frames);
  };

  return {
    version,

    get stopped() {
      return stop !== undefined;
    },

    // Parses a soundfont's bytes into a new instrument, and returns its handle.
    createInstrument(soundfont) {
      const handle = withBytes(soundfont, (pointer, size) =>
        call('_modular_create_instrument', pointer, size));
      if (handle === 0) throw failure();
      return handle;
    },

    // Renders with the instrument `handle`. The module holds the one rendering until its
    // channels are copied out, then releases it.
    render(handle, midi, sampleRate) {
      const rendered = withBytes(midi, (pointer, size) =>
        call('_modular_render', handle, pointer, size, sampleRate));
      if (!rendered) throw failure();
      try {
        const frames = call('_modular_rendering_frames') >>> 0;
        return {
          sampleRate,
          channels: [
            channel(call('_modular_rendering_left'), frames),
            channel(call('_modular_rendering_right'), frames),
          ],
        };
      } finally {
        if (stop === undefined) call('_modular_release_rendering');
      }
    },
  };
}

function ignore() {}
