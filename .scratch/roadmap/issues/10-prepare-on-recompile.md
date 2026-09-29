# What does `prepare()` mean when a node is rewired, not restarted?

Type: grilling
Status: open

## Question

Found while resolving ticket 02. This is a concrete contradiction in code today, not a
speculation.

`Engine.cpp:198` calls `prepare` on every node at every Engine construction:

```cpp
for (const ResolvedNode& r : impl_->nodes) r.node->prepare(info);
```

`Node.h:86` documents `prepare` as called once. Both were written when an Engine was
built once per render, so the two readings never collided.

They collide the moment a recompile happens mid-stream. Ticket 02 established that node
state lives in the **Graph**, not the Engine — `Graph` owns nodes as
`std::vector<std::unique_ptr<Node>>` (`Graph.h:52`), while `Engine::Impl` owns only wiring
tables and storage (`Engine.cpp:40-42`) and holds raw `Node*`. So recompiling the same
Graph and swapping engines preserves filter state, oscillator phase and voice assignments
for free — *unless* `prepare` is the conventional "clear your buffers", in which case every
recompile wipes exactly the state the swap just preserved.

Decide the contract:

- Is there one `prepare` with a flag/reason, or two distinct calls — "the rate or block
  size changed, resize and clear" versus "you are being rewired, keep everything"?
- What must a node do on each? Reallocation is legal on the editing thread; clearing state
  is the thing at issue.
- What happens to a node **newly added** by the edit — it has no state to keep, so it takes
  the cold path. Does that fall out of the contract, or need saying?
- Does the answer change `PrepareInfo`, and is that an interface change (expensive-class,
  the whole node library churns) or additive?

Cheap to settle now and expensive later: every node written under today's reading raises
the cost of the fix, which is the same argument the ticket-01 research made about the
16-byte event payload.

Related but separate: PDC delay lines are Engine-owned storage, so a latency change
discards their contents mid-stream even when node state survives. That is ticket 08's
probe, not this ticket.

## Comments

**Sharpened by ticket 03 (2026-08-07).** Two of the options above are now closed, and the
question is narrower than when it was written.

Ports became mutable while a node sits in a graph, and deletion landed with them, so the node
object is now genuinely shared between an *editable* graph and a *running* compiled snapshot —
it is no longer only rate and block size that can change under a node. That makes two things
requirements rather than preferences:

- **Recompile must re-`prepare()` every node.** A node caches nothing about its ports for the
  block loop, but anything it sized or resolved at prepare time can now be invalidated by an
  edit rather than only by a restart.
- **Recompile must preserve node state.** Non-negotiable now: ticket 03 exists to let a wired
  plugin gain and lose ports *without churning its DSP state*, so a recompile that clears state
  would defeat the ruling that motivated it.

So "one `prepare` that clears" is ruled out; what remains is the shape — a reason flag on one
call, or two distinct calls. Add a third trigger to the list alongside rate/block-size change
and rewiring: **the node's own port set changed**, which is the case where a node may need to
resize per-port storage while keeping everything else. The newly-added-node cold path is
unchanged by ticket 03.

See ADR-0004.
