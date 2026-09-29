# Extract the host-side obligations of VST3 (and AU/CLAP where they diverge)

Type: research
Status: resolved

## Question

Plugin standards are this roadmap's requirements oracle: they are a decades-old distilled
enumeration of what audio tools need, so conforming to them replaces speculating about
"common requirements". What, concretely, does a *host* have to provide?

Produce a checklist of host obligations, each tagged with the blast-radius class it implies
for this kernel (cheap / moderate / expensive / catastrophic) and whether today's model
already satisfies it. Areas known to matter, to be verified against the actual specs rather
than from recall:

- **Processing setup** — sample rate and max block size supplied at activation; what a host
  must do when rate changes; symbolic sample size; process modes (realtime / offline /
  prefetch), given this project currently only renders offline.
- **Latency** — reporting, and crucially the plugin *changing* its latency mid-session and
  forcing the host to redo compensation. This is the flagged hard/open problem
  ("recompilation must be possible mid-playback without dropout") arriving as a hard
  requirement rather than an aspiration.
- **Parameters and automation** — how many, discovered when? Sample-accurate parameter
  changes within a block vs. the block-latched value implementation in v1. Is
  block-latched *legal but degraded*, or non-conformant?
- **Buses and channel arrangements** — how a host negotiates channel layouts, against the
  settled `audio<N>` decision where channel count is part of the port type.
- **Events** — note/expression events carrying intra-block sample offsets, against the
  settled event model. Also payloads exceeding the 16-byte inline limit (SysEx).
- **State** — opaque blobs to save and restore, against the deferred serialization work.
- **Bypass, silence flags, tail length**, and anything else a host must honour.

Deliverable: `.scratch/roadmap/research/01-plugin-host-obligations.md`. Cite the spec or
primary source for each obligation; flag anything that contradicts a decision already
recorded in CLAUDE.md, since a contradiction is a finding, not a problem.

## Answer

Delivered: **`.scratch/roadmap/research/01-plugin-host-obligations.md`** — 1015 lines, every
claim cited to a header or first-party doc page actually opened. Sources: `vst3_pluginterfaces`
headers off `master`; four VST3 Developer Portal pages (Processing FAQ, Parameters+Automation,
Call Sequence, Silence flags) quoted verbatim; `free-audio/clap` headers off `main`; Apple's
AudioToolbox headers from the local MacOSX14.2 SDK. Sections 1–8 cover the ticket's checklist
plus transport; §9 is the contradictions; §10 ranks candidate claims by blast radius.

**Six contradictions with settled decisions in CLAUDE.md.** Each is a finding, not a problem:

1. **The 16-byte event payload is too small for ordinary notes, not just SysEx.** VST3's plain
   `NoteOnEvent` is 20 bytes; CLAP's event *header alone* is 16 and its note event ~40; AU's
   MIDI event is a trailing flexible array. CLAUDE.md files oversized payloads as a later SysEx
   concern — it is a today concern for every note. The "fixed-stride records in the audio hot
   path" justification is undercut by CLAP, a modern realtime ABI that deliberately chose
   variable-stride events with a `size` field.
2. **`audio<N>` is under-specified.** VST3 negotiates `SpeakerArrangement`, a bitmask of speaker
   *positions*: `k30Cine` (L R C) and `k30Music` (L R Cs) are both 3 channels and are different.
   CLAP carries `channel_count` plus a separate `port_type`; AU carries counts plus a layout
   property. Channel count is strictly less information than any of the three.
3. **"Recompilation mid-playback without dropout" is stricter than any spec requires.** VST3
   explicitly permits the interruption and mandates deactivate/reactivate; CLAP forbids latency
   changing while active; AU tells plugins to pad to a fixed latency. The real requirement is
   stop-recompile-resume — materially easier than the flagged aspiration.
4. `Node::prepare` says "called once", but a rate change requires calling it again. (Now
   ticket 10, which found the same collision from the recompile direction.)
5. **Opaque plugin state is storable in the graph but not derivable from it** — the terminal
   claim's wording must split authored from derived data, or hosting a plugin falsifies it by
   definition.
6. `value` ports read zero when unconnected, but an unautomated plugin parameter must read its
   current value — which pushes parameter storage out of the graph, feeding #5.

**The ticket's central question, answered.** Block-latched parameter delivery is **legal but
degraded**, not non-conformant: VST3's obligation is phrased relative to "the underlying
automation curve", and a block-resolution host has no finer curve to transmit. It *becomes*
non-conformant the moment the host holds sub-block automation and flattens it. Conformance
trap: a block-latched host must emit its point at `sampleOffset 0`, because a spec-following
plugin reads a lone point at offset *k*>0 as a linear ramp from position −1, not a step.

**Four settled decisions the oracle vindicated** — worth as much as the contradictions:
timeline-position-as-user-space, latency-is-structural, one-connection-per-input, and best of
all `valueAt(offset)` taking an offset it currently ignores — that signature is the only reason
sample-accurate automation is a moderate change rather than an expensive one.

**Ranked claims (§10).** Catastrophic first: **event payload shape** — spike it before the node
library grows, since every event node written under the 16-byte assumption raises the cost of
the fix — then **channel arrangement**. Expensive: **tail**, the clearest cheap-now/
expensive-later item and the exact twin of the existing `latencySamples()`; it earns its place
independent of plugins, because offline render is the primary verification path. Moderate: PDC
plus recompile-on-demand, with the note that keeping the recompile-request path *out of* `Node`
is what keeps it moderate.

Unblocks ticket 09.

Status: resolved
