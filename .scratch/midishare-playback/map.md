# Map: midishare makes its sound with Modular

Labels: wayfinder:map

> **Charted 2026-11-24.** The user confirmed the destination and the scope lines in charting
> round 1. Like every planning record, this map is provisional until its planning PR merges.

## Destination

midishare's Play (editor and clip page) and its piano-roll note preview sound through Modular
compiled to WebAssembly, instead of abcjs's built-in synth. The sound is a sampled piano: a
Modular soundfont node that reads a piano soundfont by default. This map ends when the way
there is clear: the Modular-side work is decided far enough to go to `/to-spec`, and the
contract the site codes against (what it loads, what it calls, what it gets back) is decided,
so a midishare session can plan its own change.

The built change will count as done when, on the live site:

- Play, Stop, Reset, seek and note preview behave as they do today, and every sample heard is
  computed by Modular, with no sample download from a third-party host. The abcjs synth stays
  only as a fallback until this is verified on the live site, and then goes;
- what you hear is what Download .mid contains, because the same MIDI bytes feed both;
- it works in current desktop Chrome, Edge, Firefox and Safari, with phones best effort.
  Safari is checked by hand once, since the development machine runs Windows;
- the purity grep still returns nothing, and no media word entered `kernel/` or `workspace/`.

**Stretch, if time allows:** the site's users choose among a few soundfonts. In the user's
words: "modular should have a soundfont node that reads a piano soundfont as default but i want
the user to be able to select a few of it. (again if time allows)". The default piano is the
destination; the choice is not required to reach it.

## Notes

**Domain.** Modular's `kernel/`, `nodes/` and `workspace/`, plus one new client: a web page.
midishare is that client, and in the roadmap's sense an *adversary*: its job is to expose what
Modular is missing, not to specify Modular. Say "Modular", never "the engine", for the product
(`CONTEXT.md`); the page is a *client*.

**Charting decisions** (user, round 1, 2026-11-24):

- The destination above, with the stretch goal recorded as one.
- Render the whole clip to a buffer that Web Audio plays; no streaming.
- Play and note preview both move; abcjs keeps parsing, sheet music and MIDI export.
- A map of its own here, not tickets on the roadmap map.
- **Beside the roadmap's route, not on it.** In the user's words, "not on the route but a
  useful compliment (assume we end up with a good soundfont node) that's gonna be useful". So
  the soundfont node is built as a reusable part of Modular's node library, to its contracts,
  not as midishare-specific code; a DAW on Modular can use it later. Anything this effort
  forces into `kernel/` or `workspace/` goes to the roadmap as a claim in its own planning PR.

**Rules for every session on this map.**

- Any change to Modular follows this repository's workflow (`AGENTS.md`).
- midishare sessions do not edit this repository, and sessions here do not edit, build, install,
  deploy or run git in the midishare repository. Reading it is allowed.
- Decisions go to the user through the planning hub session.
- midishare's deployed site must not change before its submission deadline,
  2026-11-24 at 11:59 PM ET.
- Committed files name the other checkout "the midishare repository" and carry no machine
  paths.

**Sources of fact.** [Charting facts](facts.md) records what charting found in both
repositories, with midishare's side taken from the midishare session's facts-only brief. The
midishare repository also holds a plan for this change, written by an agent on 2026-11-23
against an older Modular commit and never approved by the user: one candidate route, not a
decision.

**Specs: how the Modular-side work cuts.** This graduated from "Not yet specified" (planning
hub, 2026-11-24, under the user's delegation). The work splits into two specs:

- **(A) The soundfont node**, [spec](../soundfont-node/spec.md) and
  [tickets](../soundfont-node/issues/): the SF2 reader, the voice, the policy, the node, its
  codec, tests and docs. Its tickets 01 to 03 complete the website's route.
- **(B) The web build and its render surface**, [spec](../web-build/spec.md) and
  [tickets](../web-build/issues/), after tickets 02, 03 and 08: the Emscripten
  build, the glue (`loadModular`, `createInstrument`, `render`), where the module lives, the
  `MidiFileNode` import from bytes, the output sink, and the extract tool (a `scripts/` script,
  per [ticket 03](issues/03-where-the-web-build-lives.md)).

Neither spec covers the midishare page.

**Related roadmap tickets** (on `.scratch/roadmap/`):
[How is the command/edit API cut?](../roadmap/issues/07-command-api-cut.md),
[Probe: does the block loop survive a real driver callback and a mid-stream recompile?](../roadmap/issues/08-realtime-callback-probe.md),
[Which adversary apps cover what plugin specs never reach?](../roadmap/issues/09-adversary-apps-for-the-gaps.md),
[What does `prepare()` mean when a node is rewired, not restarted?](../roadmap/issues/10-prepare-on-recompile.md),
[What does an unconnected value input read?](../roadmap/issues/13-unconnected-value-default.md).
A ticket here does not resolve one of those; where one blocks a ticket here, the ticket says so.

**Skills.** `/grilling` and `/domain-modeling` by default, `/prototype` for probes, `/research`
for research tickets. Probe code is throwaway and lives under `prototypes/` here, outside the
build, as roadmap ticket 04's did. Research findings live under `research/` here.

## Decisions so far

<!-- one line per closed ticket: enough to judge relevance, then open the ticket for detail -->

- [Which soundfonts ship, in what format, under what licence and at what size?](issues/07-which-samples-ship.md)
  — SoundFont 2 read by Modular at authoring time: the only route with no codec in Modular that
  keeps the instrument an authored node owning its samples. The host serves at most 25 MiB per
  file, so the site ships a single-preset extract. Recommended default: FluidR3_GM's acoustic
  grand piano (MIT, today's sound), fallback FreePats' Upright Piano KW small (CC0); the extract's
  size needs the file, so measuring it is a new task.
- [Install an Emscripten toolchain the probe can use](issues/01-install-emscripten.md)
  — Emscripten 6.0.13 is installed in its own emsdk folder (2.0 GB) and activated per shell with
  `emsdk_env.ps1`, which changes no lasting environment variable. `emcc`, `emcmake` with the
  Ninja already present, and a C++20 hello-world that throws and catches with `-fexceptions` all
  work. Link C++ with `em++`.
- [Is the soundfont node one primitive or a supernode of voices, and where do its samples live?](issues/05-soundfont-node-shape-and-samples.md)
  — one polyphonic primitive (`event<midi>` in, `audio<2>` out) that owns the soundfont file's
  bytes as stored node content, saved once and whole inside the graph; its voices are internal,
  so release and stealing are its own policy. No kernel or workspace change, and no ADR.
- [Measure the FluidR3_GM piano before it becomes the default](issues/10-measure-the-fluidr3-piano.md)
  — it fits: the full piano preset extracts to a 7.45 MiB SF2 (5.13 MiB for notes 36–95);
  one velocity layer of 32 kHz stereo pairs played as panned mono zones, with an audible filter.
- [What does the page hand Modular, and what does it get back?](issues/04-page-to-modular-contract.md)
  — two layers: midishare's own `AudioEngine` calls a Modular surface that parses the soundfont's
  bytes once into an instrument handle and renders abcjs's Download .mid bytes at a given rate
  into two JavaScript-owned channels, release included (render to a 2 s cap, trim below
  −80 dBFS). Preview is a one-note clip through the same call; errors reject with Modular's
  message, and the abcjs fallback stays in the page until a clip census or a patched writer
  clears the MIDI writer's defects. No kernel or workspace change.
- [How does a released voice keep sounding until its release ends?](issues/06-release-survives-voice-allocation.md)
  — a note-off never frees a voice; a voice ends when its envelope has fallen 96 dB or its
  sample runs out. The pool counts sounding samples, 256 by default (FluidSynth's). When it is
  full, the node takes whole notes, released before held and oldest first, with a 6 ms fade. A
  key struck again gets a voice beside the old one, and note-offs end a key's held notes oldest
  first, so unisons and abcjs's overlapping slurs play right.
- [Which part of the SoundFont format does the node read?](issues/09-soundfont-format-subset.md)
  — every generator that shapes a voice, file modulators evaluated at note-on, and both velocity
  defaults (velocity to cutoff linear at every velocity, unlike FluidSynth's switch at 64);
  effects sends and controllers (the pedal too) are ignored. Malformed and SoundFont 3 files
  throw; the rest plays as the spec's rules say, with a list of what was ignored. Velocity is
  each note-on's byte, channel 10 plays the preset, and each stereo side is its own mono voice.
  FluidSynth is the comparison renderer for the piano.
- [Probe: does the offline MIDI render run in a page as WebAssembly?](issues/02-probe-offline-render-in-a-page.md)
  — Yes, with no source change: `midi_cli`'s graph built with `em++` renders in Node, Chrome and
  Edge bit for bit equal to native, 1.1 to 1.25 times native time (a 261 s worst-case clip in about
  0.36 s). Recommended: native WebAssembly exceptions (legacy encoding), caught at the exported
  function and returned as a message; 37.7 KB Brotli with the glue.
- [Where does the web build live, and how does a built module reach the site?](issues/03-where-the-web-build-lives.md)
  — the web is a supported platform: a `web` preset, a `web` CI job under `CI gate` on a
  pinned Ubuntu with emsdk 6.0.13, and `docs/web.md`, scoped to the web surface and the tests
  that pass under Node. The glue is a general `apps/web/` target. The CI job uploads the
  module and a manifest of its Modular commit as an artifact, which midishare commits. The
  extract tool is a `scripts/` Python script in spec B; the extract lives only in midishare,
  with its MIT notice. No isolation headers. A pre-existing libc++ break in persistence goes
  to its own fix ticket.
- [How is a browser render checked against a native one?](issues/08-browser-render-checked-against-native.md)
  — bit for bit, by construction. The soundfont node computes its output with portable `exp2`,
  `sin` and `cos` instead of the maths library (its spec's new ticket 08). MIDI times move from
  `long double` to `double` as spec B's first ticket, reversing ticket 02's decision 2 because
  MSVC and wasm32 round half-frame ties apart; that also fixes ticket 03's persistence break.
  GCC and Clang build with `-ffp-contract=off`. CTest checks golden per-block hashes natively
  and under Node, in the `web` job under `CI gate`; Firefox and Safari are checked once by hand.
  Every map ticket is now closed. Nothing left under Not yet specified blocks spec B (ticket
  04's decision 4 answers the repeated-renders item), so spec B can start.

## Not yet specified

In scope, not yet sharp enough to ticket. Graduates as the frontier advances.

- **Choosing among a few soundfonts (stretch).** What "a few" are (different instruments, or
  different pianos), who chooses (the clip's author, stored with the clip, or each listener),
  and how the choice crosses the contract. Sharp once the default piano and the contract are
  decided. Research favours further FluidR3_GM presets (one licence), each extracted under the
  host's 25 MiB limit. On the node's side a choice is an authoring edit between renders: another
  file loaded into the one node, or another preset of the same file (the node-shape decision
  above).
- **Where the render runs in the page** (main thread or a Web Worker), and what the site shows
  while it renders. midishare's call, informed by the probe's render times; it reaches this map
  only if it changes the contract.
- **Sound parity.** Not a map ticket (planning hub, 2026-11-24, on
  [Which part of the SoundFont format does the node read?](issues/09-soundfont-format-subset.md)).
  The user judges by ear on the live site, after the node passes its FluidSynth comparison,
  whether the abcjs fallback can go; the [soundfont node spec](../soundfont-node/spec.md)
  carries this as its last acceptance item. Expect a
  dry output and a wider velocity spread than abcjs's synth.

## Out of scope

Ruled beyond this destination by the user in round 1. Returns only if the destination is
redrawn, and then as a fresh effort.

- **Realtime playback in an AudioWorklet** (streaming block by block, editing while playing,
  WebMIDI input). The site pre-renders a whole clip today, and nothing at the destination needs
  streaming. It would pull roadmap tickets 07, 08 and 10 and "Modular as a plugin guest" forward,
  and threads would need cross-origin isolation headers the site does not send.
- **Rendering on the server.** The site's Cloudflare Worker could run the module, but that adds
  a request per Play and per note preview for no gain over the browser.
- **Sheet music, ABC parsing and MIDI export.** abcjs keeps them; only the sound changes.
