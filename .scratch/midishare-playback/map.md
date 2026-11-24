# Map: midishare makes its sound with Modular

Labels: wayfinder:map

> **Draft, charting round 1.** The destination, the scope lines and the choice of a map of
> its own are proposals awaiting the user's round-1 answers. Tickets below may be redrawn when
> those answers land. Nothing here is accepted direction until its planning PR merges.

## Destination

*Proposed.* midishare's Play (editor and clip page) and its piano-roll note preview sound
through Modular compiled to WebAssembly, instead of abcjs's built-in synth. This map ends when
the way there is clear: the Modular-side work is decided far enough to go to `/to-spec`, and
the contract the site codes against (what it loads, what it calls, what it gets back) is
decided, so a midishare session can plan its own change.

The built change will count as done when, on the live site:

- Play, Stop, Reset, seek and note preview behave as they do today, and every sample heard is
  computed by Modular, with no abcjs synth call and no sample download from a third-party host;
- what you hear is what Download .mid contains, because the same MIDI bytes feed both;
- the purity grep still returns nothing, and no media word entered `kernel/` or `workspace/`.

## Notes

**Domain.** Modular's `kernel/`, `nodes/` and `workspace/`, plus one new client: a web page.
midishare is that client, and in the roadmap's sense an *adversary*: its job is to expose what
Modular is missing, not to specify Modular. Say "Modular", never "the engine", for the product
(`CONTEXT.md`); the page is a *client*.

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
repositories. The midishare repository also holds a set-aside plan for this change, written on
2026-11-23 against an older Modular commit; facts from it are rechecked before use. A
facts-only brief from the midishare session is pending, and wins where it differs.

**Related roadmap tickets** (on `.scratch/roadmap/`):
[How is the command/edit API cut?](../roadmap/issues/07-command-api-cut.md),
[Probe: does the block loop survive a real driver callback and a mid-stream recompile?](../roadmap/issues/08-realtime-callback-probe.md),
[Which adversary apps cover what plugin specs never reach?](../roadmap/issues/09-adversary-apps-for-the-gaps.md),
[What does `prepare()` mean when a node is rewired, not restarted?](../roadmap/issues/10-prepare-on-recompile.md),
[What does an unconnected value input read?](../roadmap/issues/13-unconnected-value-default.md).
A ticket here does not resolve one of those; where one blocks a ticket here, the ticket says so.

**Skills.** `/grilling` and `/domain-modeling` by default, `/prototype` for probes, `/research`
for research tickets. Probe code is throwaway and lives under `prototypes/` here, outside the
build, as roadmap ticket 04's did.

## Decisions so far

<!-- one line per closed ticket: enough to judge relevance, then open the ticket for detail -->

## Not yet specified

In scope, not yet sharp enough to ticket. Graduates as the frontier advances.

- **How the Modular-side work cuts into specs.** Probably two: the web build with its render
  surface, and the instrument. Depends on the build, surface and instrument tickets.
- **Where the render runs in the page** (main thread or a Web Worker), and what the site shows
  while it renders. midishare's call, informed by the probe's render times; it reaches this map
  only if it changes the contract.
- **Repeated renders on one loaded instrument.** Rebuilding the graph per clip is simplest, but
  a sampled instrument is costly to rebuild. Reusing it builds a new `Engine` on the same graph
  per render, which is exactly the `prepare()` question roadmap ticket 10 holds open. Sharp once
  the instrument and surface tickets say what the graph holds.
- **Sound parity.** What "sounds as good as today" means and who judges it, once the instrument
  is chosen.

## Out of scope

*Proposed; each line waits on the user's round-1 answer.*

- **Realtime playback in an AudioWorklet** (streaming block by block, editing while playing,
  WebMIDI input). The site pre-renders a whole clip today, and nothing at the destination needs
  streaming. It would pull roadmap tickets 07, 08 and 10 and "Modular as a plugin guest" forward,
  and threads would need cross-origin isolation headers the site does not send. It would return
  as its own effort.
- **Rendering on the server.** The site's Cloudflare Worker could run the module, but that adds
  a request per Play and per note preview for no gain over the browser.
- **Sheet music, ABC parsing and MIDI export.** abcjs keeps them; only the sound changes.
