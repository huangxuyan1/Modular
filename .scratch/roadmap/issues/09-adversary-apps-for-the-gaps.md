# Which adversary apps cover what plugin specs never reach?

Type: grilling
Status: open
Blocked by: 01

## Question

Plugin standards are the requirements oracle, but they are a *plugin-shaped* oracle: they
describe a realtime audio effect or instrument inside a host. They say nothing about the
non-music media pipelines this project deliberately targets — AI podcast generation, speech
processing, transcription, offline render, audio analysis.

Once ticket 01's checklist exists, the gaps become visible. Choose the adversary apps that
attack them, remembering the rule: an application's job is to *fail*, not to specify.

- **Non-audio payloads at long time scales.** Transcription produces `event<word>` streams
  spanning hours, not 16-byte records at sample offsets inside a 512-frame block. Does the
  event model hold, or does it assume audio-rate density?
- **Non-realtime pull.** An analysis tool has no output sink and no clock — it consumes a
  file as fast as it can. Push-vs-pull evaluation is listed as Open; this is what would
  settle it.
- **Batch / offline render at scale.** Many graphs, no device, no timeline.
- **Huge graphs.** A DAW project is thousands of nodes; every test so far is under ten.
  Does compile time or topo sort become the problem, and is that a claim or just profiling?
- Which of these are worth building versus merely reasoning about? Each must be cheap — the
  precedent is milestone 1, which was an app whose only job was to make three domains fail
  visibly if they didn't compose.

Deliverable: a short list of adversary apps, each with the assumption it attacks and the
observation that would count as failure. These become coarse claims in the roadmap's tail.
