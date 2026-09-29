# Probe: can a 4x-oversampled effect be expressed in today's model?

Type: prototype
Status: open

## Question

Sample rate is absent from the project's ledger entirely — not in Settled, not in Open.
Every port implicitly runs at one global rate. That makes it the most likely
catastrophic-class unknown, because rate touching the port type would revisit the
three-domain model itself.

This is a **spike, not a design**. Do not design multi-rate. Attempt the cheapest thing
that would expose the flaw, and report what happened:

- Try to express a 4x-oversampled distortion using only what exists today — upsample node,
  nonlinearity, downsample node, wired in a graph, rendered offline.
- Does it work? If it does, the claim "one global rate is sufficient" survives and rate
  drops down the ordering.
- If it doesn't, *why* — what precisely is missing? Does a port need to carry a rate? Does
  a node need to see a different `numFrames` than its neighbours? Does the block contract
  itself assume one rate for the whole graph?
- Related pressures to note but not solve: analysis at 1/64 rate, varispeed, and a plugin
  host changing rate while loaded (the guest case, which is last on the roadmap and so
  cannot be what retires this question).

Outcome is one of: no claim needed (rate is fine), a moderate claim (rate is expressible
but awkward), or a catastrophic-class claim that jumps to the head of the order. Any code
written here is throwaway; the finding is the deliverable.
