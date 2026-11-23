# 01 — Node identity and node deletion

**What to build:** a graph author can delete a node, and every wire between the nodes that
remain still connects exactly the nodes it connected before. Today a node's identity *is* its
position in the graph's node list, so deleting one would slide every later node down and
silently repoint the wires that named them. This ticket makes `NodeId` an id minted by the
graph's `IdSource`, never reused, and gives the author the two operations deletion requires:
removing a connection, and asking what connections touch a thing.

Ports stay positional in this ticket — that is ticket 02. The two axes are independent, and
splitting them keeps this one green and demoable on its own.

Deletion follows **RESTRICT**: deleting a node that still carries a wire throws, and the
caller unwires first. The kernel never decides what the user meant. A cascade belongs in an
app, where a reader can see what it destroys.

**Blocked by:** None — can start immediately.

**Status:** done

**Watch out:** `NodeId` keeps its underlying type, so its *meaning* changes from index to id
with no compile error to mark the sites. Every place that treats a node id as an offset into
the node list has to be found by reading, not by building. That includes the flattener and
the engine's compile, which enumerate nodes and resolve connections. This is the one part of
this claim the compiler will not catch for you.

- [x] The graph owns a separately-allocated `IdSource` that mints node ids. Separately
      allocated because `Graph` is movable and a supernode holds one by value — a node
      holding a `Graph*` would dangle after a move.
- [x] Ids are never reused within a graph, including after a deletion.
- [x] `Connection`'s node ends name ids; node lookup no longer indexes the node list.
- [x] **Falsifiable demo, must fail before the change:** build a graph of several nodes,
      wire two of them, delete a third unwired node that sits earlier in insertion order,
      and render. The surviving wire connects the same two nodes and the output is
      byte-for-byte what it was before the deletion.
- [x] Deleting a connection works and is unconditional — a wire is what RESTRICT protects,
      so removing one has nothing to protect.
- [x] A `connectionsOf()` query reports exactly the connections that touch a given node.
- [x] Deleting a node that still carries a wire throws `std::invalid_argument` with a short
      lowercase `"context: what went wrong"` message, per the authoring-path convention.
- [x] Unwiring first, then deleting the same node, succeeds.
- [x] A stale node id — one whose node was deleted — throws rather than resolving to whatever
      now occupies that slot.
- [x] The milestone render, the grouped and nested supernode renders, and block-size
      invariance all still pass unchanged.
- [x] `scripts/check_kernel_purity.sh` still returns nothing.

## Comments

**2026-08-07 — implemented.** `kernel/include/modular/Ids.h` (new) holds `NodeId`, `kNoNode`
and `IdSource`; `Graph` owns one by `unique_ptr` and mints from it. `nodes_` became
`{id, node}` entries sorted by id (ids mint upward, entries append in mint order, deletion
only removes) so lookup is a binary search with no side table. `removeNode`, `disconnect`,
`connectionsOf` and `nodeIds()` are new; `node()` throws on a stale id. The two places that
walked nodes by index — `Flatten::enumerate` and `Supernode::boundaryIds` — now walk
`nodeIds()`.

Ports stay positional, `Node::frozen_` stays, and node construction stays
`addNode(unique_ptr)` — all ticket 02.

Verification beyond the checklist: ids temporarily mutated to start at 5000 and step by 7
(sparse, non-zero-based) with the whole suite still green, which is the evidence for the
*Watch out* note that nothing surviving assumes id == position or contiguity. `render_cli`
output is byte-identical to the pre-change commit in both Debug and Release.

**Hazard opened here, and owned by ticket 03.** An outer wire names a supernode's face by
*position*, and this ticket makes boundary nodes deletable. The inner graph's RESTRICT rule
cannot see an outer wire to refuse over, so `inner().removeNode(boundaryNode)` succeeds and
shifts every later outer port. Until ticket 03 gives the face ids, the compile catches it:
`Flatten` throws `"engine: connection names an outer port that no longer exists"` instead of
resolving the wire elsewhere. This replaced an `assert`, which under `NDEBUG` was an
out-of-bounds read. Test: *"deleting a boundary node cannot silently repoint an outer wire"* —
it should be rewritten when 03 lands, since the throw will move to deletion time.
