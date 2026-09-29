# Host obligations of VST3, AU and CLAP

Resolves `.scratch/roadmap/issues/01-plugin-host-obligations.md`. Historical research
only — no code was changed during the original investigation. Its descriptions of
then-current Modular contracts (including the 16-byte event payload) are snapshots,
not claims about the current tree; later ADRs and source supersede them.

## Sources actually read

The research consulted headers and first-party documentation directly. The
findings below paraphrase those sources and identify them at each claim, rather
than reproducing long passages. Where I could not find a rule, I say so rather than inventing one.

| tag | what | where |
|---|---|---|
| `[VST3-hdr]` | `vst3_pluginterfaces` headers, `master` branch: `vst/ivstaudioprocessor.h`, `vst/ivstcomponent.h`, `vst/ivstevents.h`, `vst/ivstparameterchanges.h`, `vst/ivsteditcontroller.h`, `vst/ivstprocesscontext.h`, `vst/ivstnoteexpression.h`, `vst/ivstprefetchablesupport.h`, `vst/vstspeaker.h` | `https://raw.githubusercontent.com/steinbergmedia/vst3_pluginterfaces/master/…` |
| `[VST3-faq]` | VST 3 Developer Portal, FAQ → Processing | `https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html` |
| `[VST3-param]` | VST 3 Developer Portal, Technical Documentation → Parameters and Automation | `…/Technical+Documentation/Parameters+Automation/Index.html` |
| `[VST3-seq]` | VST 3 Developer Portal, Workflow Diagrams → Audio Processor Call Sequence | `…/Technical+Documentation/Workflow+Diagrams/Audio+Processor+Call+Sequence.html` |
| `[VST3-sil]` | VST 3 Developer Portal, Change History 3.0.0 → Silence flags | `…/Technical+Documentation/Change+History/3.0.0/Silence+flags.html` |
| `[CLAP]` | `free-audio/clap`, `main` branch: `include/clap/{plugin,process,events,audio-buffer,host}.h`, `include/clap/ext/{params,latency,tail,audio-ports,note-ports,state,state-context,render}.h` | `https://raw.githubusercontent.com/free-audio/clap/main/include/clap/…` |
| `[AU]` | Apple SDK headers from the original research machine's `MacOSX14.2.sdk`: `AudioUnitProperties.h`, `AUComponent.h`, `AUAudioUnit.h`, `AUAudioUnitImplementation.h` | first-party, historical local SDK |

Not consulted: the VST3 SDK's own hosting sample code, AAX, LV2. The VST3 licence text
was not read; header comments were used as specification evidence, not copied
as SDK source.

## Reading of Modular's current model (what I checked against)

`kernel/include/modular/Node.h`, `Buffers.h`, `PortType.h`, `Engine.h`, `Graph.h`, and
`nodes/include/modular/nodes/BasicNodes.h`. The load-bearing facts:

- `Engine(Graph&, double sampleRate, uint32 maxBlockFrames)` compiles in the constructor;
  `Node::prepare(PrepareInfo{sampleRate, maxBlockFrames})` is called once; there is no
  deactivate/reactivate, only "build a new Engine".
- `Engine::process(uint32 numFrames)`, `numFrames <= maxBlockFrames`, may vary per call.
- `Node::latencySamples()` exists and returns `uint32`; nothing consumes it yet.
- Audio is planar `float`, channel count carried in `PortType::sub` via `audio(N)`.
- `Event` = `uint32 offset` + `std::array<std::byte,16>`; `EventBuffer` keeps push order
  stable and sorts by offset.
- `ValueSlot` is size-free bytes; `ValueView::valueAt(frameOffset)` **ignores the offset**
  (block-latched), and the comment states the accessor is the contract.
- No serialization, no bypass, no tail, no silence, no process-mode, no bus activation.

---

# 1. Processing setup

### 1.1 Sample rate and maximum block size are supplied before any processing, and are constant while active

**Spec.** VST3: `IAudioProcessor::setupProcessing(ProcessSetup&)` carries
`{processMode, symbolicSampleSize, maxSamplesPerBlock, sampleRate}` and is "Called in
disable state (setActive not called with true) before setProcessing is called and
processing will begin" `[VST3-hdr ivstaudioprocessor.h]`. CLAP:
`activate(plugin, sample_rate, min_frames_count, max_frames_count)` — "The process's
sample rate will be constant and process's frame count will included in the [min, max]
range, which is bounded by [1, INT32_MAX]" `[CLAP plugin.h]`. AU:
`maximumFramesToRender` "must be set by the host before render resources are allocated.
It cannot be changed while render resources are allocated" `[AU AUAudioUnit.h]`, bridged
to `kAudioUnitProperty_MaximumFramesPerSlice` `[AU AudioUnitProperties.h]`.

**Modular.** Satisfied. `PrepareInfo` carries exactly these two quantities and `prepare()`
runs at compile, before any `process()`.

**Blast radius.** n/a. One gap worth noting: CLAP also supplies a *minimum* frame count;
Modular has no minimum, which is fine for a host (the host picks) but means a Modular-as-
guest boundary node would have to report `min = 1`.

### 1.2 Block size varies per call but never exceeds the maximum

**Spec.** "Note that the ProcessData->numSamples which indicates how many samples are used
in a process call can change from call to call, but never bigger than the
maxSamplesPerBlock." `[VST3-faq]`

**Modular.** Satisfied, and stronger than required — block-size invariance is under test.

### 1.3 Changing rate or max block size requires a full deactivate / reconfigure / reactivate cycle

**Spec.** VST3 gives the literal sequence `[VST3-faq]`:

```
Vst::IAudioProcessor::process (...);
Vst::IAudioProcessor::setProcessing (false);
Vst::IAudioProcessor::setActive (false);
Vst::IAudioProcessor::setupProcessing (...);
Vst::IAudioProcessor::setActive (true);
Vst::IAudioProcessor::setProcessing (true);
Vst::IAudioProcessor::process (...);
```

and, for sample rate, answers the same question with "No. Same as Q: Can the maximum
sample block size change while the plug-in is processing?" CLAP requires `deactivate()`
then `activate()` with the new rate (`activate` is `[main-thread & !active]`) `[CLAP
plugin.h]`. AU requires `deallocateRenderResources` → change format/`maximumFramesToRender`
→ `allocateRenderResourcesAndReturnError` `[AU AUAudioUnit.h]`.

**Modular.** Partially satisfied by accident. Destroying the `Engine` and constructing a
new one over the same `Graph` re-runs `prepare()` with a new rate, which *is* the required
cycle. But `Node::prepare` is documented "Called once by the engine at compile time", which
contradicts the actual usage a rate change requires, and `Engine` exposes no explicit
deactivate — so a plugin-host node has no defined moment at which it may drop and rebuild
the plugin's render resources, and no guarantee `process()` will not be called across the
seam.

**Blast radius: moderate.** Kernel-internal lifecycle work: an explicit
activate/deactivate boundary and a `prepare()` contract that says "may be called again
after a deactivate". `PrepareInfo` already carries everything needed, so no node churn.

### 1.4 Symbolic sample size: 32-bit is mandatory, 64-bit optional and negotiated

**Spec.** VST3: `enum SymbolicSampleSizes { kSample32, kSample64 }`, carried in *both*
`ProcessSetup` and `ProcessData`, with `AudioBusBuffers` a union of `channelBuffers32` /
`channelBuffers64`, and a per-plugin query `canProcessSampleSize(int32)` `[VST3-hdr]`.
CLAP is blunt: "32 bits support is required for both host and plugins. 64 bits audio is
optional." `[CLAP ext/audio-ports.h]`, with per-port flags
`CLAP_AUDIO_PORT_SUPPORTS_64BITS`, `…PREFERS_64BITS`,
`…REQUIRES_COMMON_SAMPLE_SIZE`. AU negotiates via the bus's stream format (ASBD).

**Modular.** float32 only, hard-wired into `ConstAudioView`/`AudioView`. This is
**conformant, not degraded**: 32-bit is the mandatory baseline in all three. Degradation is
limited to plugins that *prefer* 64-bit.

**Blast radius if 64-bit is ever wanted: catastrophic.** Sample format is not currently an
axis of `PortType` at all; adding it changes the port-domain model and every `process()`
in the library. Recording it here as a deliberate, presently-safe non-decision.

### 1.5 Process modes: realtime / prefetch / offline

**Spec.** VST3 `enum ProcessModes { kRealtime, kPrefetch, kOffline }`, and the switching
rule is the interesting part `[VST3-hdr ivstaudioprocessor.h]`:

The VST3 interface permits realtime/prefetch changes in the processing thread without another setup call, but switching into or out of offline mode requires setup again. `[VST3-hdr ivstaudioprocessor.h]`

So `processMode` appears in **both** `ProcessSetup` and, per block, in `ProcessData`.
Plugins may additionally implement `IPrefetchableSupport` returning
`kIsNeverPrefetchable` / `kIsYetPrefetchable` / `kIsNotYetPrefetchable`, and signal changes
with `restartComponent(kPrefetchableSupportChanged)` `[VST3-hdr ivstprefetchablesupport.h]`.
CLAP has only two modes: `clap.render` with `CLAP_RENDER_REALTIME` /
`CLAP_RENDER_OFFLINE`, set on the main thread, plus
`has_hard_realtime_requirement()` `[CLAP ext/render.h]`. AU has
`kAudioUnitProperty_OfflineRender` (UInt32, R/W): "set this to true when the rendering
operation an audio unit is being used within is going to write out the results to a file.
The value defaults to false" `[AU AudioUnitProperties.h]`.

**Modular.** No concept at all. An offline-only host is fully conformant — it simply
always declares offline. The obligation is that the mode be *communicable to the node*.

**Blast radius: split.**
- Realtime-vs-offline, decided at prepare time → **moderate** (one field on `PrepareInfo`;
  additive, existing nodes ignore it).
- VST3's *per-block* realtime↔prefetch switch has nowhere to live: `ProcessContext` exposes
  only `numFrames()` and `blockStartFrame()`. → **expensive** (ProcessContext is handed to
  every node's `process()`). Only needed if prefetch is ever supported; CLAP and AU do not
  have it, so it can be deferred without divergence from two of three.

---

# 2. Latency

### 2.1 The plugin reports latency; the host compensates

**Spec.** VST3 `getLatencySamples()` returns `uint32`, documented "[UI-thread & Setup
Done]" `[VST3-hdr]`. CLAP `clap_plugin_latency.get()` returns `uint32_t`, "[main-thread &
(being-activated | active)]" `[CLAP ext/latency.h]`. AU reports `latency` as an
`NSTimeInterval` — **seconds, floating point**, "The processing latency (the time it takes
an audio unit to represent an input in its audio output) specified in seconds" `[AU
AudioUnitProperties.h / AUAudioUnit.h]`.

**Divergence worth recording.** AU's latency is a time, not an integer sample count, and
is therefore not guaranteed to be a whole number of samples at the current rate. Any AU
host node must round, and there is no fractional-delay facility anywhere in Modular to do
better.

**Modular.** The *reporting* half is already in the interface: `Node::latencySamples()`,
`uint32`, virtual, defaulting to 0. The *compensating* half does not exist.

**Blast radius: moderate** — this is literally map.md's own example of moderate
("compile-time delay insertion"). AU's seconds→samples conversion is **cheap** (node-local).

### 2.2 Latency changing mid-session — and what the specs actually demand

This is the ticket's flagged hard problem, and the finding is that **all three standards
resolve it by deactivating and reactivating the plugin, and none of them require the change
to be gapless.**

**VST3.** The plugin calls `IComponentHandler::restartComponent(kLatencyChanged)`. The flag
is documented `[VST3-hdr ivsteditcontroller.h]`:

VST3 requires a host to deactivate and reactivate a plugin after it reports changed latency; the new latency is queried after reactivation. `[VST3-hdr ivsteditcontroller.h]`

And `getLatencySamples`'s own doc `[VST3-hdr ivstaudioprocessor.h]`:

VST3 acknowledges that recomputing delay compensation after a plugin latency change may interrupt playback. `[VST3-hdr ivstaudioprocessor.h]`

`[VST3-faq]` adds: "The host will call `Vst::IAudioProcessor::getLatencySamples()` in order
to check the new latency and **adapt its latency compensation if supported**."

**CLAP** is stricter still: "Once activated the latency and port configuration must remain
constant, until deactivation" `[CLAP plugin.h]`, and `clap_host_latency.changed()` is
`[main-thread & being-activated]` with "The latency is only allowed to change during
plugin->activate. If the plugin is activated, call host->request_restart()" `[CLAP
ext/latency.h]`.

**AU** actively discourages the whole idea `[AU AUAudioUnit.h]`:

AU recommends that plugins maintain a fixed latency even when parameters or bypass change, since host delay compensation is generally established before rendering. `[AU AUAudioUnit.h]`

**Modular.** Not satisfied: there is no path by which a node asks for a recompile, and no
PDC. But the shape required is exactly "tear down the Engine, rebuild it" — which Modular
already does structurally.

**Blast radius: moderate.** The recompile itself is the moderate-class work already
anticipated. The notification direction (node → application "please recompile") need not
touch the `Node` interface at all: a plugin-host node lives in `nodes/`, and the
application owns the `Engine`, so the request can be an out-of-band main-thread signal on
the node object. Keeping it out of `Node` is what keeps this moderate rather than
expensive — worth designing for deliberately.

### 2.3 Latency is constant while active

**Spec.** CLAP states it flatly (above); VST3 enforces it by requiring deactivation; AU
advises it. **Modular agrees**: CLAUDE.md's supernode ruling already says "latency is
structural, not signal-dependent — runtime path switching selects among statically aligned
streams; only graph edits change latency."

This is a **validated** decision, not a contradiction. Recording it because the ticket
treats plugin standards as the requirements oracle, and here the oracle agrees.

### 2.4 Presentation latency (host tells the plugin what is upstream and downstream)

**Spec.** VST3 `IAudioPresentationLatency::setAudioPresentationLatencySamples(dir,
busIndex, latencyInSamples)` — marked "[optional]" `[VST3-hdr]`. AU
`kAudioUnitProperty_PresentationLatency` (Float64 seconds, write-only, per Scope/Element)
`[AU AudioUnitProperties.h]`. **CLAP: I found no equivalent** — `clap_audio_buffer` has a
`latency` field ("latency from/to the audio interface") which is related but not the same
thing, and I did not find a doc explaining a host's obligation around it. Reported as
uncertain.

**Modular.** No concept. Computing it requires the compiler to sum latency along each path
to each port, which the flattening ADR already says is how per-path latency "emerges".
Delivering it is the problem: it is *per port*, so `PrepareInfo` (a flat struct) does not
fit.

**Blast radius: expensive** (a new per-port `Node` entry point), but it is **optional in
both standards that have it**, so a host omitting it is conformant. Low priority.

---

# 3. Parameters and automation

### 3.1 Discovery: how many, and when

**Spec.** VST3: `IEditController::getParameterCount()` and
`getParameterInfo(index, ParameterInfo&)`, both UI-thread, after `initialize`.
`ParameterInfo` = `{ParamID id, title, shortTitle, units, int32 stepCount,
ParamValue defaultNormalizedValue, UnitID unitId, int32 flags}` `[VST3-hdr
ivsteditcontroller.h]`. **All values are normalized `ParamValue` (double) in [0,1]** —
one scalar type for every parameter.

The parameter set is **not frozen for the session**. `restartComponent` flags include
`kParamValuesChanged` ("The host invalidates all caches of parameter values and asks the
edit controller for the current values"), `kParamTitlesChanged` (titles, defaults,
stepCount or flags changed), and `kParamIDMappingChanged` `[VST3-hdr]`.

CLAP: `clap.params` `count`/`get_info`; adding or removing parameters requires
`clap_host->restart()`, then, once deactivated, `rescan(CLAP_PARAM_RESCAN_ALL)` `[CLAP
ext/params.h, scenario VI]`. AU: `parameterTree`, with KVO notifications on `parameterTree`
and a pseudo-property `allParameterValues` for bulk changes `[AU AUAudioUnit.h]`.

**Modular.** The *structural* half fits: "ports are reconfigurable while authoring, frozen
at compile" and "changing arity is a graph edit → recompile" is exactly the
deactivate-rescan-reactivate protocol. Same recompile mechanism as §2.2.

**The half that does not fit** — and this is a genuine finding, not a wording problem:

- A plugin routinely exports hundreds of parameters. Modeling each as a `value` input port
  means hundreds of ports per plugin node.
- Worse, the semantics are wrong. Modular's contract is "unconnected inputs read defaults:
  … value → zeroed slot", and `ValueSlot`'s comment says "empty = unset: reads as zero".
  But an *unautomated* plugin parameter must read the plugin's **current** value —
  `defaultNormalizedValue` initially, and whatever a state restore left behind afterwards.
  Zero is a wrong answer, not a missing one.
- The workable node design is therefore: parameters live inside the plugin node, and only
  the automated ones are exposed as ports (variable arity, set at configuration time).
- **Consequence:** the values of un-exposed parameters then live in the plugin's opaque
  state blob, not in the graph. See §5.1 — this is where the terminal claim gets stressed.

**Blast radius: cheap** for the node design itself; the tension it creates is recorded
under state.

### 3.2 Sample-accurate automation vs Modular's block-latched values — the ticket's central question

**VST3's model.** `IParameterChanges` is a list of `IParamValueQueue`, one per changed
parameter; each queue is a list of `(int32 sampleOffset, ParamValue value)` points. Both
interfaces are marked `[host imp]` and `[mandatory]` — **the host implements them and must
supply them** `[VST3-hdr ivstparameterchanges.h]`. The semantics `[VST3-hdr]`:

VST3 automation queues describe a piecewise-linear approximation of a parameter curve, with points sent per processing block. Plugins can interpolate within the block. `[VST3-hdr ivstparameterchanges.h]`

with two encoding rules that matter to any host:

VST3 allows omission of redundant constant points and a special previous-block position (−1) for a continuing slope. `[VST3-hdr ivstparameterchanges.h]`
>
VST3 encodes an abrupt change with adjacent points for the old and new values. `[VST3-hdr ivstparameterchanges.h]`

and the host-side obligation, stated on the doc portal `[VST3-param]`:

VST3 requires the transmitted changes to preserve enough information for sample-accurate reconstruction of the source automation curve; the plugin performs the reconstruction. `[VST3-param]`

plus two invariants a host must not break `[VST3-param]`: "A parameter (ID) is present only
one time in the IParameterChanges list!" and "Automation Points inside a
Vst::IParamValueQueues are sorted per offset (position inside the audio block)!"

**Is block-latched delivery non-conformant, or legal but degraded?** The ticket asks for a
clear ruling, so:

**Legal but degraded — with one conformance trap.**

- *Legal*, because the obligation is phrased relative to "the underlying automation curve".
  A host whose automation model has block resolution has no finer curve to transmit; a
  single point per block faithfully transmits the curve it actually has. The queue produced
  is well-formed, and the plugin's documented interpolation (a segment from the last point
  at block position −1 to the new point) reconstructs precisely the intended step.
- *Degraded*, because the host can never express sub-block automation. Any automation the
  host holds at finer resolution — a curve read off a timeline at sample resolution, a
  fast filter sweep, a click-free fade — is quantised to the block grid. Timing error is up
  to `maxBlockFrames − 1` samples (≈10.7 ms at 512 frames / 48 kHz), and stepped rather
  than ramped parameter changes are the classic source of zipper noise.
- *Becomes non-conformant* the moment the host **does** hold sub-block automation data and
  flattens it anyway: then it has failed "transmits … in a way that allows a sample-accurate
  reconstruction". So this is a conformance cliff Modular would walk off later, when a
  timeline with real automation curves exists — not a problem today.
- **The trap:** a block-latched host must emit its single point at `sampleOffset 0`. A host
  that emits one point at some offset *k* > 0 meaning "step at k" will be read by a
  spec-following plugin as a *linear ramp* from position −1 to *k*. Getting this wrong
  produces a plausible-sounding but incorrect result that no assertion will catch.

**CLAP diverges materially, and in Modular's favour.** CLAP has no automation-curve
concept. Parameter changes are ordinary events in the same sorted list as notes:
`CLAP_EVENT_PARAM_VALUE` carrying `{param_id, cookie, note_id, port_index, channel, key,
double value}` with `header.time` = sample offset `[CLAP events.h]`. There is no
interpolation contract — an event is a step at `time`. So a CLAP host that emits param
events at exact sample offsets is *fully* sample-accurate with no curve machinery at all.
CLAP also has `CLAP_EVENT_PARAM_MOD` for modulation, and `clap_plugin_params.flush(in,
out)` for delivering changes when not processing.

**AU diverges in a third direction, and exceeds Modular's event model.** AU v3 delivers
`AUParameterEvent { eventSampleTime, rampDurationSampleFrames, parameterAddress, value }`
with type `AURenderEventParameter` (immediate) or `AURenderEventParameterRamp` `[AU
AUAudioUnitImplementation.h]`, and "a ramp event will only appear in the render cycle
during which it starts; the audio unit is responsible for maintaining continued ramping
state for any further render cycles." AU v2's `AudioUnitScheduleParameters` takes
`AudioUnitParameterEvent` whose ramp variant is `{SInt32 startBufferOffset, UInt32
durationInFrames, startValue, endValue}` `[AU AUComponent.h]`.

Two things there that Modular's event domain **cannot express**:
- `startBufferOffset` is **signed** — a ramp may have started before this block.
- `durationInFrames` may exceed the block, so a ramp spans block boundaries.

Modular's `Event.offset` is `uint32` constrained to `[0, numFrames)`.

**Modular status.** Not satisfied for sub-block automation. But the settled design pays
off here: `ValueView::valueAt(frameOffset)` already takes the offset and merely ignores it,
and CLAUDE.md records that "The accessor *is* the contract; sample-accurate changes can
arrive later without rewriting nodes."

**Blast radius: moderate.** Making values sample-accurate means changing `ValueSlot` to
hold a per-block change list and changing the engine's value wiring — kernel-internal.
`valueAt`'s signature does not change, so the node library does not churn. This is the
single best vindication of a settled decision found in this research: without the
offset-taking accessor, this would have been expensive.

### 3.3 Parameter flush without audio

**Spec.** VST3 `[VST3-faq]`: "Yes, the host can call `Vst::IAudioProcessor::process`
without buffers (numInputs and numOutputs are zeroed), in order to flush parameters (from
host to plug-in)." `AudioBusBuffers`'s `numChannels` "could be set to value '0' when the
host wants to flush the parameters (when the plug-in is not processed)" `[VST3-hdr]`.
CLAP: `clap_plugin_params.flush(in, out)`, `[active ? audio-thread : main-thread]`, and the
host must honour `clap_host_params.request_flush()` by scheduling either `process()` or
`flush()` `[CLAP ext/params.h]`. AU: no direct equivalent found.

**Modular.** `Engine::process(numFrames)` is the only entry point; there is no "run the
graph with no audio". Note the harder half is an application concern — when the transport
is stopped the engine is not running at all, so nothing pumps the plugin.

**Blast radius: cheap to moderate.** A host node can drive its own flush from `process()`.
The stopped-transport case is application wiring, which is where CLAUDE.md's discipline
says transport belongs anyway.

---

# 4. Buses and channel arrangements

### 4.1 Bus enumeration and activation

**Spec.** VST3: `IComponent::getBusCount(mediaType, dir)` / `getBusInfo(…, BusInfo&)`
where `BusInfo = {mediaType, direction, channelCount, name, busType kMain|kAux, flags}`;
media types are `kAudio` and `kEvent`; "All busses are initially inactive"; `activateBus`
turns them on; "The kMain busses have to place before any others kAux busses" `[VST3-hdr
ivstcomponent.h]`. CLAP splits them: `clap.audio-ports` and `clap.note-ports`, each
`count`/`get`, "The plugin is only allowed to change its ports configuration while it is
deactivated" `[CLAP ext/audio-ports.h, ext/note-ports.h]`. AU uses scopes and elements with
an `AUAudioUnitBusArray` per scope.

**Modular.** A node's ports *are* its buses; there is no separate activation. The mapping
is clean: an inactive/disconnected bus corresponds to an unconnected input, which Modular
already reads as silence — matching VST3's "a plug-in with a disconnected input bus will
continue to receive default silence buffers" `[VST3-faq]`. Bus activation becomes node
configuration → variable arity → recompile, which is the settled model.

**Blast radius: cheap.**

### 4.2 Channel arrangement negotiation — the sharpest finding against a settled decision

**Spec.** VST3 negotiates with
`setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs,
int32 numOuts)`, and a `SpeakerArrangement` is **a bitmask of speaker positions, not a
count** `[VST3-hdr vstspeaker.h]`:

VST3 represents channel arrangement as a bitset of speaker positions; stereo combines the left and right speaker bits. `[VST3-hdr vstspeaker.h]`

with `kSpeakerL = 1<<0`, `kSpeakerR = 1<<1`, `kSpeakerC = 1<<2`, `kSpeakerLfe = 1<<3`, and
so on through ~40 defined positions, and named arrangements `kMono`, `kStereo`,
`kStereoSurround`, `k30Cine`, `k30Music`, `k51`, `k61Cine`, etc. Note that `k30Cine`
(L R C) and `k30Music` (L R Cs) are **both three channels and are different arrangements**.

The negotiation is genuinely a negotiation, not a declaration — the plugin has three
documented responses `[VST3-hdr ivstaudioprocessor.h]`: accept and return `kResultTrue`;
adapt as close as it can, modify its own busses and return `kResultFalse` (with "requested
arrangements for kMain busses should be handled with more priority than the ones for kAux
busses"); or fall back to a default and return `kResultFalse`. Either way the host must
then re-read `getBusArrangement` / `getBusInfo` and adapt its own graph.

CLAP keeps count and layout as **separate fields**: `clap_audio_port_info` has
`uint32_t channel_count` *and* a distinct `const char *port_type` compared against
`CLAP_PORT_MONO`, `CLAP_PORT_STEREO`, `CLAP_PORT_SURROUND`, `CLAP_PORT_AMBISONIC`, with
"If null or empty then it is unspecified (arbitrary audio)" `[CLAP ext/audio-ports.h]`.

AU carries both too: `AUChannelInfo` pairs are pure counts with negatives as wildcards
("{-1,-1} means any number of channels on input and output as long as they are the same")
`[AU AudioUnitProperties.h]`, while the actual layout is a separate property
(`kAudioUnitProperty_AudioChannelLayout` / `SupportedChannelLayoutTags`), and the header
explicitly distinguishes the two: "layout is relevant. By comparison, the
kAudioUnitProperty_StreamFormat…".

**Contradiction with CLAUDE.md.** The settled decision reads: "**Audio channels:** channel
count is part of the port type — `audio<N>`; connection requires matching N." All three
standards carry *more* than a count. Under `audio<N>`:

- a 5.1 port and a 6-channel ambisonic port and six mono stems all type as `audio<6>` and
  connect silently;
- a plugin-host node cannot faithfully represent the arrangement it negotiated — it can
  only say "6";
- `k30Cine` vs `k30Music` — a real, shipping distinction — is inexpressible.

This is **not a conformance break for a mono/stereo-only host**, and Modular is nowhere
near surround. It *is* a statement that `audio<N>` is under-specified relative to every
standard consulted, and that the type system will not catch the class of bug it was
introduced to catch, once channel counts exceed 2.

**Blast radius: catastrophic by the letter of map.md's table** — `PortType` *is* the
port-domain model, and the engine needs the count for buffer allocation, so a layout id
cannot simply replace `sub` the way an opaque subtype id does for value/event. A softer
variant — keep `audio(N)` and add an optional layout id alongside it, defaulting to
"unspecified" so existing nodes are unaffected — would be **expensive** rather than
catastrophic. Worth noting that this softer variant is *more* kernel-pure than today's
design: an opaque layout id the kernel never interprets is exactly the `value<T>`/`event<T>`
treatment, whereas `audio(N)` is the one place the kernel assigns meaning to `sub`.

### 4.3 The negotiation protocol itself

The host must *ask*, read back what it got, and adapt. Modular's `Graph::connect` is a pure
equality check with no negotiation and no adaptation. This is fine — the negotiation
happens at node configuration time, before ports are declared, and re-negotiation is a
graph edit → recompile, per the settled model. **Cheap**, provided the recompile-request
mechanism from §2.2 exists.

### 4.4 The host must always pass buffer arrays, even for inactive buses

**Spec.** `[VST3-hdr ivstaudioprocessor.h]`: "the host must always supply an array for the
channel buffers, regardless if the bus is active or not. However, if an audio bus is
currently inactive, the actual sample buffer addresses are safe to be null."

**Modular.** Satisfied in spirit — unconnected audio inputs read silence. The host node
synthesises the null convention. **Cheap.**

### 4.5 In-place processing

**Spec.** CLAP `in_place_pair` on `clap_audio_port_info` — "allow the host to use the same
buffer for input and output" `[CLAP ext/audio-ports.h]`. VST3 alludes to it: "No need to
copy the input buffers to the output buffers when the pointers are the same" `[VST3-faq]`.

**Modular.** Engine owns storage and hands out distinct views. Purely a performance matter;
a host without in-place is conformant. **Moderate** (engine buffer allocation) if wanted.

---

# 5. Events

### 5.1 Intra-block sample offsets

**Spec.** VST3 `Event.sampleOffset` is `int32`, "sample frames related to the current block
start sample position" `[VST3-hdr ivstevents.h]`. CLAP `clap_event_header.time` is
`uint32_t`, "sample offset within the buffer for this event" `[CLAP events.h]`.

**AU diverges:** `AUEventSampleTime eventSampleTime` is "The sample time at which the event
is scheduled to occur" — an **absolute** `int64` timeline value, not a block offset `[AU
AUAudioUnitImplementation.h]`. The plugin subtracts the render timestamp itself, and
past-due events (scheduled before the current block) are possible.

**Modular.** Satisfied for VST3 and CLAP: `Event.offset` is `uint32` in `[0, numFrames)`,
and CLAUDE.md's claim that this offset "is the event domain's core mechanic" is
corroborated by all three. AU's absolute→relative conversion, including clamping past-due
events to 0, is node-local. **Cheap.**

### 5.2 Sorted delivery

**Spec.** CLAP is explicit: "Input read-only event list. The host will deliver these sorted
in sample order." `[CLAP process.h]` AU: "A time-ordered linked list of the AURenderEvents
to be rendered during this render cycle." `[AU AUAudioUnitImplementation.h]`

**VST3: I could not find a stated sorting requirement for `IEventList`.** The sortedness
rule I found is stated only for automation points ("Automation Points inside a
Vst::IParamValueQueues are sorted per offset" `[VST3-param]`), and `IEventList` itself is
just `getEventCount`/`getEvent`/`addEvent` with no ordering comment `[VST3-hdr]`. Recording
this as *not found*, not as *not required* — plugins in practice assume sorted input.

**Modular.** Satisfied and stronger: `EventBuffer::push` inserts keeping offset order,
stable for equal offsets.

### 5.3 The 16-byte inline event payload — a contradiction, and larger than "SysEx"

**Spec, VST3.** `Vst::Event` is `{int32 busIndex, int32 sampleOffset, TQuarterNotes
ppqPosition (double), uint16 flags, uint16 type, union{…}}`. Measuring the union members
straight off the header `[VST3-hdr ivstevents.h]`:

| variant | fields | bytes |
|---|---|---|
| `NoteOnEvent` | int16 channel, int16 pitch, float tuning, float velocity, int32 length, int32 noteId | **20** |
| `NoteOffEvent` | int16, int16, float, int32, float | 16 |
| `PolyPressureEvent` | int16, int16, float, int32 | 12 |
| `DataEvent` | uint32 size, uint32 type, **`const uint8* bytes`** | 16 + out-of-line buffer |
| `ChordEvent` / `ScaleEvent` | … + **`const TChar* text`** | 16 + out-of-line string |
| `NoteExpressionTextEvent` | typeId, noteId, textLen, **`const TChar* text`** | out-of-line string |

**The plain note-on is already 20 bytes — before the 20-byte common header.** SysEx
(`DataEvent` with `type == kMidiSysEx`) is only the most obvious case, not the boundary
case.

**Spec, CLAP.** `clap_event_header_t` is `{uint32 size, uint32 time, uint16 space_id,
uint16 type, uint32 flags}` — **16 bytes of header alone**, i.e. Modular's entire payload
budget. `clap_event_note` adds `{int32 note_id, int16 port_index, int16 channel, int16 key,
double velocity}` for roughly 40 bytes total. `clap_event_midi_sysex` is
`{header, uint16 port_index, const uint8_t *buffer, uint32 size}` with an explicit lifetime
rule: "The lifetime of this buffer is (from host->plugin) only the process call in which the
event is delivered" `[CLAP events.h]`. And critically, CLAP events are **variable-size by
design** — that is what `header.size` is for, and the header comment says "clap_event
objects are contiguous regions of memory which can be copied with a memcpy of `size` bytes
starting at the top of the header."

**Spec, AU.** `AUMIDIEvent` ends with `uint8_t data[3]` documented as "The number of valid
MIDI bytes in the data field. 1, 2 or 3 for most MIDI events, **but can be longer for
system-exclusive (sys-ex) events**" — a trailing flexible array, the struct is
over-allocated. `AUMIDIEventList` embeds a whole `MIDIEventList` of UMP packets `[AU
AUAudioUnitImplementation.h]`.

**Contradiction with CLAUDE.md, on two counts.**

1. CLAUDE.md frames the limit as a SysEx edge case: "Only *events* have a fixed inline
   payload (16 bytes) — that constraint is real there … larger event payloads (SysEx) will
   need an out-of-line handle subtype later," echoed in `Buffers.h`. **It is not an edge
   case.** 16 bytes cannot hold a conformant note-on in VST3, cannot hold a CLAP note event,
   and equals a CLAP event header exactly.
2. The stated justification — "fixed-stride records in the audio hot path" — is
   contradicted by the newest of the three formats. CLAP is a realtime plugin ABI designed
   from scratch in the 2020s and it deliberately chose **variable-stride** events with a size
   field. Fixed stride is a defensible implementation choice; it is not, on this evidence,
   forced by the audio hot path.

**Blast radius: split, and the split matters for ordering.**
- Raising `kInlineBytes` from 16 to (say) 48 or 64: **moderate**. `Event::make`/`Event::as`
  are `memcpy` + `static_assert`, so nothing in `nodes/` changes source; it is a recompile
  and a memory-footprint change, kernel-internal.
- Variable-size or out-of-line payloads (the shape all three standards actually use):
  **catastrophic** — it changes `Event`'s layout, `EventBuffer`'s storage and iteration, and
  therefore the block-processing contract itself, plus it introduces payload lifetime rules
  the kernel currently does not have.
- Because the cheap fix and the real fix have wildly different costs, this deserves a spike
  before the node library grows further — every event-handling node written under the
  16-byte assumption raises the cost of the catastrophic path.

### 5.4 Event buses, MIDI channels and dialects

**Spec.** VST3: for a `kEvent` bus, "the channelCount corresponds to the number of
supported MIDI channels by this bus" `[VST3-hdr ivstcomponent.h]`. CLAP negotiates a
*dialect*: `clap_note_port_info.supported_dialects` is a bitfield over
`CLAP_NOTE_DIALECT_CLAP | _MIDI | _MIDI_MPE | _MIDI2`, plus a single `preferred_dialect`
`[CLAP ext/note-ports.h]`. AU has `hostMIDIProtocol` (MIDI 1.0 vs UMP) which must be set
before allocating render resources `[AU AUAudioUnit.h]`.

**Modular.** `event<subtype>` with an opaque subtype id and exact-equality connection.
Dialect selection is node configuration → frozen at compile → cheap. The one thing `connect`
cannot express is "this port accepts any of {A, B, C}" — but that is resolved at
configuration time, before ports are declared, so it never reaches the kernel. **Cheap.**

### 5.5 Output events and plugin→host parameter changes

**Spec.** VST3 `ProcessData::outputEvents` and `outputParameterChanges`, both marked
"(optional)" for the host to supply `[VST3-hdr]`. CLAP `clap_process.out_events` is not
marked optional, and "The plugin must insert events in sample sorted order" `[CLAP
process.h, events.h]`. AU `AUMIDIOutputEventBlock` `[AU AUAudioUnit.h]`.

**Modular.** An output `event` port covers this directly. **Cheap.**

### 5.6 What the host is required to supply at all

Reading `ProcessData`'s field comments `[VST3-hdr ivstaudioprocessor.h]`: `inputEvents` and
`outputEvents` are annotated "(optional)", `outputParameterChanges` "(optional)", and
`processContext` "(optional, but most welcome)". `inputParameterChanges` carries **no**
optional marker, and `IParameterChanges`/`IParamValueQueue` are both `[host imp]
[mandatory]`. I read that as: a VST3 host must supply `inputParameterChanges` and may
supply the rest as null. Flagging this as **inference from annotation asymmetry**, not a
quoted rule — I did not find a sentence stating it outright.

---

# 6. State

### 6.1 Opaque save/restore

**Spec.** VST3 splits it in two: `IComponent::getState(IBStream*)` / `setState(IBStream*)`
for the processor, **and** `IEditController::getState`/`setState` for the controller, plus
`IEditController::setComponentState` to push the processor's state into the controller
`[VST3-hdr ivstcomponent.h, ivsteditcontroller.h]`. `IComponent::setState` is legal in
"[Initialized | Connected | Setup Done | Activated | Processing]" — i.e. **including while
processing**.

CLAP: `clap.state` `save(ostream)` / `load(istream)`, both `[main-thread]`, plus
`clap_host_state.mark_dirty()`. The advice to hosts is explicit `[CLAP ext/params.h]`:
"Hosts should _not_ try to save and restore parameter values for plugins that don't
implement the state extension" and "do not implement a parameter saving fall back for
plugins that don't implement the state extension."

AU: `fullState` (an `NSDictionary`, bridged to `kAudioUnitProperty_ClassInfo`) — "The Audio
Unit should not persist transitory properties such as stream formats, but should save and
restore all parameters and custom properties" — plus a separate
`kAudioUnitProperty_ClassInfoFromDocument` which a host "should use first (instead of
kAudioUnitProperty_ClassInfo) when restoring the state of an audio unit when the audio unit
is a part of a document" `[AU AUAudioUnit.h, AudioUnitProperties.h]`.

**Modular.** No serialization exists at all; CLAUDE.md lists it as deferred and map.md
lists "Persistence and the subtype-id policy" as not-yet-specified.

**Tension with the terminal claim.** map.md's destination is "the DAW stores nothing that
isn't derivable from the authored graph." A plugin state blob is opaque by construction:
it cannot be derived, only stored. This does **not** break "the graph is the single source
of truth" — the blob can live on the node inside the graph. It does break "derivable",
unless the claim's wording distinguishes *authored* data (a node's construction parameters,
of which a state blob is one) from *derived* data (anything the compile or a view
recomputes). Recommendation: sharpen the terminal claim's wording before a plugin-host node
exists, rather than discovering the ambiguity while trying to falsify it.

Note also the pleasing fit: `ValueSlot` is already "size-free … the kernel stores bytes and
never interprets them", which is exactly the semantics an opaque blob needs. But plugin
state is not a *port*, it is node construction data, so `ValueSlot` is a precedent rather
than a solution.

**Blast radius: expensive by the letter of the rubric** — persisting node state means a
`Node`-level save/load pair, and `Node` is the interface whose growth map.md defines as
expensive. Mitigated to **moderate** in practice if the pair is a defaulted no-op virtual,
the way `latencySamples()` already is.

### 6.2 Preset vs duplicate vs project context

**Spec.** CLAP `clap.state-context` distinguishes `CLAP_STATE_CONTEXT_FOR_PRESET`,
`_FOR_DUPLICATE`, `_FOR_PROJECT` `[CLAP ext/state-context.h]`. AU's
`ClassInfoFromDocument` is the same idea. VST3: no direct equivalent found.

**Modular.** No concept. **Cheap** once §6.1 exists.

---

# 7. Bypass, silence, tail

### 7.1 Bypass — and a divergence in *what kind of thing* it is

**Spec.** VST3 makes bypass a **parameter**: `ParameterInfo::kIsBypass = 1 << 16`, "Special
bypass parameter (only one allowed): plug-in can handle bypass. Highly recommended to
export a bypass parameter for effect plug-in." `[VST3-hdr ivsteditcontroller.h]` And
`[VST3-faq]`:

For a plugin-provided VST3 bypass, the plugin keeps processing and manages click-free transitions and any required latency-aligned output; the host should use the plugin parameter rather than substituting its own. `[VST3-faq]`

with the host-side rule: if a plugin exports no bypass parameter, "the host can perform
bypass processing" `[VST3-param]`. So the host's obligations are: find the `kIsBypass`
parameter, expose and automate it like any other, keep calling `process()`, and *not*
substitute its own bypass when the plugin exports one.

CLAP agrees on kind: `CLAP_PARAM_IS_BYPASS` is a parameter flag `[CLAP ext/params.h]`.
**AU diverges**: bypass is a *property*, `kAudioUnitProperty_BypassEffect` (UInt32, R/W) /
`shouldBypassEffect` — host-driven, outside the parameter system, therefore not automatable
through it `[AU AudioUnitProperties.h, AUAudioUnit.h]`.

**Modular.** No bypass concept — correct per CLAUDE.md's design discipline; bypass is
user-space wiring. Either kind is just an input on the host node. **Cheap.**

**Validation worth recording:** VST3's insistence that a bypassed plugin still delay its
output by its latency is precisely because latency must not change when bypass toggles.
That is the same conclusion as CLAUDE.md's "latency is structural, not signal-dependent",
arrived at independently by Steinberg — and AU says it outright ("a latency that varies
with parameter settings, **including bypass**, is generally not useful to hosts").

### 7.2 Silence flags

**Spec.** VST3 `AudioBusBuffers.silenceFlags` is a `uint64` bitset, one bit per channel.
The header is unusually direct about its status `[VST3-hdr ivstaudioprocessor.h]`:

VST3 silence flags are optional optimization hints: when used, buffers still need valid memory, even for a channel marked silent. `[VST3-hdr ivstaudioprocessor.h]`

If a host *does* use them `[VST3-sil]`: "The host has the responsibility to clear the input
buffers (set to something near zero, like 10e-7, to prevent de-normalization issue) when it
enables the silence flags (the output silence flags will be set by the host to no silence
(=0))". CLAP's analogue is `clap_audio_buffer.constant_mask`, and it is explicitly a hint:
"checking the constant mask is optional, and this implies that the buffer must be filled
with the constant value… The constant mask is a hint." `[CLAP audio-buffer.h]` AU:
`kAudioUnitRenderAction_OutputIsSilence`, "The receiver of the buffer can then use the flag
as a hint" `[AU AUComponent.h]`.

**Modular.** No concept. **Conformant** — optional in all three. Pure throughput.

**Blast radius: expensive.** Carrying a flag is cheap-ish (a field on `AudioView` /
`ConstAudioView` plus a `ProcessContext` accessor, additive and ignorable → moderate), but
*honouring* it correctly is per-node, so the library churns. Note that Modular's block-size
invariance test would catch a node that gets silence propagation wrong only if the test
compares silent and non-silent renders — worth a test design note if this is ever taken up.

### 7.3 Tail length — the one that bites an offline-first project immediately

**Spec.** VST3 `getTailSamples()` returns `uint32`, with `kNoTail = 0` and
`kInfiniteTail = kMaxInt32u`, and the doc names offline rendering as the primary consumer
`[VST3-hdr ivstaudioprocessor.h]`:

VST3 plugins may report a tail duration in samples so offline rendering and other host processing can avoid cutting off a delayed output. `[VST3-hdr ivstaudioprocessor.h]`

CLAP: `clap.tail` `get()` returning `uint32_t`, "Any value greater or equal to INT32_MAX
implies infinite tail", plus `clap_host_tail.changed()` `[audio-thread]` and a process
return status `CLAP_PROCESS_TAIL` = "Rely upon the plugin's tail to determine if the plugin
should continue to process" `[CLAP ext/tail.h, process.h]`. AU: `tailTime`, an
`NSTimeInterval` in **seconds** — same units divergence as latency `[AU AUAudioUnit.h]`.

**Modular.** No concept. Because Modular renders offline and offline is the primary
verification path, "how long do I keep rendering after the last input?" is a question the
render CLI has no way to ask the graph today. A reverb node added tomorrow has the same
problem — this is not a plugin-only obligation.

**Blast radius: expensive by the rubric** (a `Node::tailSamples()` entry point), but it is
the exact twin of `latencySamples()`, which already exists with a defaulted implementation.
Adding it now, while the node library is four files, is close to free; adding it after the
library grows is the expensive version. This is the clearest "cheap now, expensive later"
item in the whole checklist.

### 7.4 Process status and sleep (CLAP only)

**Spec.** `clap_process_status` ∈ {`CLAP_PROCESS_ERROR`, `CONTINUE`,
`CONTINUE_IF_NOT_QUIET`, `TAIL`, `SLEEP`} `[CLAP process.h]`. On `SLEEP` the host may stop
calling `process()` until an event arrives or the plugin calls `request_process()`. VST3
has no sleep protocol (it has `setProcessing(false)`); AU has none.

**Modular.** `Node::process()` returns `void`. Giving it a return value is a `Node`
interface change → **expensive**, for a CLAP-only optimisation. A host that always processes
is conformant. Low priority.

---

# 8. Transport and musical context (not on the ticket's list, but a host obligation)

**Spec.** VST3 `ProcessData::processContext` points at a `ProcessContext` carrying
`sampleRate` and `projectTimeSamples` ("always valid"), and optionally `systemTime`,
`continousTimeSamples`, `projectTimeMusic`, `barPositionMusic`, `cycleStartMusic`,
`cycleEndMusic`, `tempo`, `timeSigNumerator`/`Denominator`, `chord`, SMPTE offset and frame
rate, `samplesToNextClock`, plus state flags `kPlaying | kCycleActive | kRecording`
`[VST3-hdr ivstprocesscontext.h]`. Since 3.7.0 plugins declare what they need via
`IProcessContextRequirements` — marked **[mandatory]** — and "If you do not implement this
interface, you may not get any information at all of the process function!" `[VST3-hdr
ivstaudioprocessor.h]`.

CLAP: `clap_process.transport`, "If null, then this is a free running host, no transport
events will be provided" `[CLAP process.h]`. AU: host supplies `musicalContextBlock` and
`transportStateBlock`, both nullable, "the block may be called at the beginning of each
render cycle" `[AU AUAudioUnit.h]`.

**Modular.** Expressible today entirely in user space: a node computes timeline position,
tempo and transport state and feeds them to the plugin-host node over `value` ports. Since
all three standards deliver this per block, at block start, a **block-latched value port is
exactly the right shape** — this is the one place where block-latching is not a compromise.

**This validates rather than contradicts** CLAUDE.md's "Timeline position … user-space, not
kernel" and "Stream time … is the only time the kernel knows." I checked specifically
because it was the decision most at risk from a plugin standard, and it survives.

One useful permission found along the way `[VST3-hdr ivstprocesscontext.h]`:

VST3 permits a fixed-block host to report correct project time at the start of the next block instead of splitting a block at every timeline jump. `[VST3-hdr ivstprocesscontext.h]`

Meaning: Modular never has to split a block at a loop point. Given "block size is dispatch
granularity" and the block-size-invariance test, that permission is load-bearing.

**Blast radius: cheap.**

---

# 9. Contradictions with CLAUDE.md's settled decisions

Listed strongest first. Each is a finding, per the ticket.

1. **The 16-byte inline event payload is too small for ordinary events, not just SysEx.**
   CLAUDE.md (and `Buffers.h`) frame the limit as needing "an out-of-line handle subtype
   later" for "larger event payloads (SysEx)". In fact VST3's plain `NoteOnEvent` is 20
   bytes, CLAP's *event header alone* is 16 bytes and its note event ~40, and AU's MIDI
   event is a trailing flexible array. Every ordinary note event in every standard
   overflows the budget. Secondary contradiction: the stated justification, "fixed-stride
   records in the audio hot path", is undercut by CLAP — a modern realtime ABI that chose
   variable-stride events with an explicit `size` field.

2. **`audio<N>` is under-specified: channel *count* is strictly less than a channel
   *arrangement*.** CLAUDE.md settled "channel count is part of the port type — `audio<N>`;
   connection requires matching N." VST3 negotiates `SpeakerArrangement`, a bitmask of
   speaker positions where `k30Cine` (L R C) and `k30Music` (L R Cs) are both 3 channels and
   are different. CLAP carries `channel_count` **and** a separate `port_type`. AU carries
   `AUChannelInfo` counts **and** a separate channel-layout property. Under `audio<N>`,
   incompatible 6-channel ports connect silently and a host node cannot represent what it
   negotiated. Harmless while Modular is mono/stereo; wrong as soon as it is not.

3. **"Recompilation must be possible mid-playback without dropout" is stricter than any
   standard requires.** CLAUDE.md files this as the hard/open consequence of engine-owned
   PDC. But VST3 explicitly permits the interruption ("this could lead to audio playback
   interruption") and mandates deactivate/reactivate; CLAP forbids latency changing while
   active at all; AU tells plugins to pad to a fixed latency instead. The requirement, as
   the oracle states it, is "be able to stop, recompile, and resume" — materially easier
   than gapless. Worth downgrading the aspiration explicitly, so it is not mistaken for a
   conformance blocker.

4. **`Node::prepare` is documented "Called once", but a rate change requires calling it
   again.** All three standards require a deactivate → reconfigure → reactivate cycle for a
   sample-rate or max-block change. Modular's only expression of that is destroying and
   rebuilding the `Engine`, which *does* re-run `prepare()` — so the code is right and the
   contract comment is wrong. Small, but it is a contract, and a plugin-host node needs a
   defined moment to release and rebuild render resources.

5. **Opaque plugin state is not derivable from the graph.** map.md's terminal claim says the
   DAW must store "nothing that isn't derivable from the authored graph". A plugin's state
   blob can be *stored in* the graph but never *derived from* it. The claim needs to
   distinguish authored data from derived data, or hosting a plugin falsifies it by
   definition. Better to sharpen the wording now than to discover it while trying to
   falsify the claim.

6. **`value` ports read zero when unconnected, but an unautomated plugin parameter must read
   its current value.** CLAUDE.md: "empty slot reads as zero". A parameter with no
   automation must read `defaultNormalizedValue`, or whatever a state restore left. This
   pushes parameter storage inside the plugin node and out of the graph, which is what
   feeds contradiction 5.

Decisions I checked *because* they were at risk, and which **survived** — recorded because
a validated decision is also a finding:

- **Timeline position as user-space, stream time as the only kernel time** (§8). All three
  standards deliver transport/musical context as per-block data *into* the plugin, which is
  a node input, and all three make it optional/nullable. Block-latched `value` ports are
  exactly the right shape. VST3 additionally grants that a host need not split blocks at
  loop points.
- **Latency is structural, not signal-dependent** (§2.3). CLAP mandates it, VST3 enforces
  it via deactivation, AU states it in prose including the bypass case.
- **`valueAt(offset)` as the contract even though v1 ignores the offset** (§3.2). This is
  what keeps sample-accurate automation a *moderate* change instead of an expensive one.
  The single best-vindicated design decision found.
- **One connection per input, fan-in is a node's job.** Nothing in any standard requires the
  host graph to sum or arbitrate at a port; buses are point-to-point and mixing is a node.

---

# 10. Candidate roadmap claims, ranked by blast radius

Following map.md's ordering discipline — proof dependency first, then blast radius
descending. Each is phrased so it could be false.

### Catastrophic-class — attack early, with a spike, not a design

1. **Event payload size and shape.** *Claim: every event in a conformant VST3/CLAP note
   stream can be carried by Modular's event domain without out-of-line storage.* Currently
   false — a plain note-on does not fit. The spike is cheap (write the payload structs
   `nodes/Payloads.h` would need for a conformant note stream and see what they measure) and
   the answer decides between a moderate fix (raise `kInlineBytes`) and a catastrophic one
   (variable-size or out-of-line payloads). **Every event-handling node written before this
   is resolved raises the cost of the catastrophic branch**, which is the proof-dependency
   argument for doing it first.

2. **Channel arrangement in the port type.** *Claim: a 5.1 bus and six mono stems cannot be
   connected to each other by mistake.* Currently false. Attack with the cheapest probe:
   try to express a surround plugin's bus negotiation in today's model. The softer landing
   (count plus an opaque layout id the kernel never interprets) is expensive rather than
   catastrophic and is arguably *more* kernel-pure than `audio(N)` — `sub` would stop being
   a number the kernel understands.

3. **64-bit sample support.** Deliberately *not* a claim. Conformant today (32-bit is
   mandatory everywhere); catastrophic if ever needed. Record as a known cliff and move on.

### Expensive-class — Node interface grows, library churns

4. **Tail.** *Claim: an offline render of a graph containing a reverb produces the complete
   tail without the application knowing what a reverb is.* Currently impossible — nothing to
   ask. This is the strongest "cheap now, expensive later" item on the list: it is the exact
   twin of the existing `latencySamples()`, and the node library is currently four files.
   It also earns its place independently of plugins, since offline rendering is Modular's
   primary verification path.

5. **Node state serialization.** *Claim: a graph containing a node with opaque internal
   state round-trips through save/load and re-renders bit-identically.* Blocked behind the
   `value<T>`/`event<T>` subtype-id ruling that map.md already flags. Also forces the
   terminal claim's authored-vs-derived wording (contradiction 5), so it has a
   proof-dependency on that sharpening.

6. **Presentation latency** (optional in VST3 and AU, absent from CLAP) and **silence flags**
   (explicitly optional in VST3, a hint in CLAP and AU). Both are expensive and both are
   conformance-neutral. Deprioritise; note them so they are not re-derived later.

### Moderate-class — kernel-internal

7. **Latency compensation plus recompile-on-demand.** *Claim: a node that changes its
   reported latency mid-session causes the engine to recompile, and the render before and
   after is correctly aligned.* This is the ticket's flagged hard problem, correctly sized
   at last: the standards want stop-recompile-resume, not gapless. Two halves — the compiler
   inserting delay (moderate), and the recompile-request path. **Design the request path to
   stay out of `Node`** — a plugin-host node in `nodes/` signalling the application, which
   owns the `Engine` — because that choice alone is what keeps this moderate.

8. **Explicit activate/deactivate lifecycle, and `prepare()` callable again.** Required by
   all three standards for rate and block-size changes, and a prerequisite for 7. Small,
   and it fixes contradiction 4.

9. **Sample-accurate values.** *Claim: a value that changes at a prime frame offset inside a
   block produces output identical to the same change delivered as its own block boundary.*
   Currently false. `valueAt(offset)` already has the right signature, so this is
   `ValueSlot` storage plus engine value wiring — no node churn. Only worth doing once
   something upstream actually produces sub-block automation, at which point the "legal but
   degraded" status of block-latched delivery becomes an actual conformance break (§3.2).

### Cheap-class — nodes only, never competes for ordering

10. Process-mode plumbing (realtime vs offline on `PrepareInfo` is *moderate*; the rest is
    node-local), bypass as an ordinary input, AU seconds→samples and absolute→relative event
    conversion, note-dialect selection as node configuration, transport/musical context as
    value ports, parameter flush pumped from a host node's `process()`, bus activation as
    variable arity.

---

## Points that stayed uncertain

- **VST3 event sorting.** I found no statement that a host must deliver `IEventList` sorted
  by `sampleOffset`. The sortedness rule I found applies only to automation points inside an
  `IParamValueQueue`. Modular sorts anyway, so nothing turns on it, but I am not asserting a
  requirement I did not find.
- **Whether `ProcessData::inputParameterChanges` may be null.** Inferred as "must be
  supplied" from the annotation asymmetry in the header (every neighbouring field is marked
  "(optional)" and it is not) plus the `[host imp] [mandatory]` tags on `IParameterChanges`.
  Not a quoted rule.
- **CLAP presentation latency.** No equivalent to VST3's `IAudioPresentationLatency` found.
  `clap_audio_buffer.latency` exists ("latency from/to the audio interface") but I found no
  documentation of a host obligation around it.
- **AU latency-change notification.** `latency` is a readonly KVO-observable property and
  the v2 property has the standard property-listener mechanism, so hosts can observe it; I
  did not find an Apple sentence stating that a host is *obliged* to. The prose instead
  discourages plugins from varying latency at all.
- **VST3 licence text** was not read. Header comments were treated as specification evidence; no SDK source was
  copied into this repo.
