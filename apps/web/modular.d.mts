// Declarations for Modular's web loader, modular.mjs. docs/web.md describes the surface.
// The generic Float32Array<ArrayBuffer> needs TypeScript 5.7 or later.

/**
 * Loads and instantiates one Modular module. Each loaded module runs its calls one at a
 * time, in call order, whichever instrument they name.
 */
export function loadModular(options?: {
  /**
   * Passed to Emscripten: maps a file the module loads, its `.wasm`, to the URL to fetch.
   * Without it, the module finds its `.wasm` beside its own script.
   */
  locateFile?: (path: string, prefix: string) => string;
}): Promise<Modular>;

export interface Modular {
  /**
   * Parses a soundfont's bytes once into an instrument that plays the file's lowest
   * preset. The bytes are copied when the call is made; the instrument keeps its own copy.
   * Rejects with an `Error` carrying Modular's message, such as `soundfont: ...`.
   */
  createInstrument(soundfont: Uint8Array): Promise<Instrument>;
}

export interface Instrument {
  /**
   * Renders Standard MIDI File bytes at `sampleRate`: the file's own frames, then its
   * release for at most two seconds, with the silent tail trimmed. The bytes are copied
   * when the call is made. Rejects with an `Error` carrying Modular's message, such as
   * `midi file: ...` or `render: ...`.
   */
  render(midi: Uint8Array, sampleRate: number): Promise<Rendering>;
}

export interface Rendering {
  /** The rate requested, unchanged. */
  sampleRate: number;
  /**
   * Left, then right: two arrays of equal length that JavaScript owns. The length may be
   * 0, for a file of zero duration. Each has its own `ArrayBuffer`, never shared memory,
   * so it can go straight to `AudioBuffer.copyToChannel`.
   */
  channels: [Float32Array<ArrayBuffer>, Float32Array<ArrayBuffer>];
}
