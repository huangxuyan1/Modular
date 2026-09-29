# 03 — The supernode face by id, and the claim closes

**What to build:** a supernode author can delete a boundary node from an inner graph, and
every wire into the supernode's remaining outer ports still delivers where it did before.
Sprint 2 shipped this as a recorded fragility — ADR-0001 noted that a supernode's outer port
index is "stable only because boundary nodes cannot be deleted or reordered." Tickets 01 and
02 removed that precondition by giving nodes and ports ids; this ticket removes the
fragility.

The mechanism needs no new concept. Both boundary nodes and their ports draw ids from the
inner graph's one `IdSource`, so two boundary nodes in one inner graph cannot collide — which
is exactly why graph scope was chosen over node scope in ADR-0004. A supernode's outer port
id therefore **is** the port id of the boundary node backing it: nothing borrowed, nothing
converted, no side table.

This is the last ticket of the claim, so the documentation and the regression suite land with
it. The close-out review follows it and is not part of it.

**Blocked by:** 02 — Port identity, port mutation, and the claim's demo.

**Status:** ready-for-agent

- [ ] A supernode's outer port id is the port id of the boundary node backing it, with no
      conversion step.
- [ ] Two boundary nodes in one inner graph have different port ids — the collision that
      killed node-scoped ids in ADR-0004 cannot occur.
- [ ] An inner graph's ids mean nothing outside it, so a supernode snapshot drops into
      another project unchanged. Its by-value ownership is unaffected.
- [ ] The derived face is addressed by id rather than position; deriving it stays off the
      block loop, as it is today.
- [ ] A supernode's outer port ids survive arbitrary edits to its inner graph, so a wire into
      a supernode is not disturbed by work done inside it.
- [ ] **Falsifiable demo, must fail before the change:** a supernode exposes several ports;
      wire a later one from outside; delete an earlier boundary node; render. The surviving
      outer wire still delivers to the same inner consumer and the output is byte-for-byte
      unchanged.
- [ ] Deleting a boundary node that still backs a wire throws, the same RESTRICT rule as
      ports and nodes — one rule for all three kinds.
- [ ] ADR-0001's "known fragility, waiting on deletion" note is retired in the ADR itself,
      matching the strike-through already recorded in CLAUDE.md.
- [ ] Full regression: the milestone render, the grouped supernode render, the nested
      supernode render and block-size invariance all byte-identical; the whole suite green.
- [ ] `scripts/check_kernel_purity.sh` still returns nothing — ids, deletion and RESTRICT
      name no medium.
- [ ] CLAUDE.md updated: the status line, and anything describing the graph's authoring API
      or the frozen-ports rule.
- [ ] `docs/roadmap.md` appended — it is a running artifact, so the claim sharpens it rather
      than waiting for a final write-up.
- [ ] Map ticket 03 (`.scratch/roadmap/issues/03-variable-arity-after-graph-add.md`) noted as
      implemented, and its requirements handoff to map ticket 10 (`prepare` on recompile)
      left open and unchanged — that ticket is out of scope here.
