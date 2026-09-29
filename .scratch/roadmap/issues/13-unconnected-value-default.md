# What does an unconnected value input read?

Type: grilling
Status: open

## Question

Graduated from the map's fog by ticket 03, which found this defect is *independent* of the
arity ruling and survives it untouched.

`ProcessContext`'s contract (`Node.h:29-30`) says unconnected inputs read defaults: "audio →
silence, event → empty, value → zeroed slot", and `ValueSlot` documents "empty = unset: reads
as zero". Ticket 01's oracle work (§3.1) found that is a **wrong answer, not a missing one**,
for the case the roadmap cares most about:

> an *unautomated* plugin parameter must read the plugin's **current** value —
> `defaultNormalizedValue` initially, and whatever a state restore left behind afterwards.

Ticket 03 removed the escape route. It was tempting to believe that exposing only the
automated parameters as ports made this moot — no port, no zero. It does not: a user ticks a
parameter for automation and the port exists before any lane is drawn, so there is a live,
unconnected value input driving a real knob. The window is small and the consequence is a
parameter snapping to zero.

CLAUDE.md's settled line is that value ports need "a per-type default", which was written
before subtypes were opaque ids (ADR-0003) — the kernel cannot know a per-type default for a
type it deliberately cannot interpret.

Decide:

- Where does an unconnected value input's value come from? Candidates: the *node* supplies a
  default when declaring the port (payload bytes at `addInput` time); the port holds a
  writable slot the node itself can set; or "unconnected" stops being readable at all and the
  node must ask whether the port is connected.
- Does a node get to know whether one of its inputs is connected? The kernel does not expose
  that today, and exposing it lets a node behave differently by wiring — which may be exactly
  right for parameters and exactly wrong for everything else.
- Is this the same mechanism as the settled "kernel needs an externally-writable value source"
  (a UI drag writing into the graph)? If a parameter's unautomated value is *written* into the
  graph rather than defaulted, it becomes authored data, which is what the terminal claim's
  derivability clause needs. If it lives in the node, it is opaque state and stresses that
  clause instead.
- Does the answer need to be size-free, given values are size-free (ADR ruling, sprint 1)?

Suspected class: moderate — `PortSpec`/`ValueSlot` and the engine's default-slot wiring,
with no node interface change if the default travels with the port declaration. Worth
checking rather than inheriting, per the ranking finding in ADR-0003 and ADR-0004.
