# Roadmap

An ordered list of falsifiable claims. Planning for it happens on the wayfinder map at
`.scratch/roadmap/map.md`; this file is the running output, sharpened as each map ticket
resolves.

## Terminal claim

> A recognizable DAW runs on Modular while (a) the purity grep still returns nothing, and
> (b) the DAW stores nothing that isn't derivable from the authored graph.
>
> **Falsified by:** any DAW feature that forced a media concept into the kernel or the
> workspace, or that needed out-of-band storage.

Half of this is already under continuous automated test — `scripts/check_kernel_purity.sh`
runs as a ctest test. The other half makes the "graph is the single source of truth" bet
checkable: reload a project from the graph alone and the DAW's display must be identical.
The acceptance test for "recognizable" is still open (map ticket 05).

Two sharpenings, both from resolved map tickets. **"On Modular", not "on the engine"** —
"engine" as a product word retired with ticket 02; `Engine` is the class, and Modular is the
language plus its compiler, runtime, standard library and workspace. The purity clause now
covers the workspace too, since a media word there would foreclose non-music tools. And
**"derivable" needs splitting**: ticket 01 found that a plugin's opaque state is storable in
the graph but not derivable from it, so the claim must distinguish *authored* data from
*derived* data or hosting a plugin falsifies it by definition. That wording is still open.

## How to read this

**The unit is a claim, not a sprint.** One claim, phrased so it could be false. Sized by
cutting, never by extending — a big area becomes several claims. Each closes with a commit
whose subject *is* the claim.

**Committed near, ordered far.** The head claim is fully specified. The tail is coarse and
ordered, re-cut whenever a claim closes; tail entries are deliberately unsized, because you
cannot write a cut list for something you don't understand yet.

**Ordering: proof dependency first, then blast radius descending** — attack the unknowns
that are most expensive to be wrong about as early as dependency allows. Note this ordering
discipline is *not* part of the terminal claim: "every expensive unknown was attacked" has
no demo that can fail, so it is method, not a claim.

| class | meaning |
|---|---|
| cheap | new node in `nodes/`; zero kernel impact |
| moderate | kernel-internal change, no interface change |
| expensive | node interface changes; the whole library churns |
| catastrophic | port-domain model or block contract changes; everything churns |

**Applications are adversaries, not requirement sources.** A flaw is an application that
can't be expressed in user space without a kernel change. **Plugin standards are the
requirements oracle** — the engine bends to plugins, not the reverse.

## Proven

| claim | closed | class |
|---|---|---|
| Three domains (audio, value, event) compose in one graph — trigger → midi block → synth → gain → WAV renders offline | 2026-07-27 | — |
| Grouping the milestone graph into a supernode changes not one sample; nesting and block-size invariance survive it | 2026-07-30 | — |
| The event payload carries a conformant plugin note (VST3 `NoteOnEvent`, CLAP `clap_event_note`), and widening it to do so changed not one line under `nodes/` | 2026-08-05 | moderate |
| A wired, stateful node gains and loses ports mid-project without disturbing any other wire or its DSP state | 2026-11-20 | expensive |

The third one is the smallest claim on this list and is kept because its *falsifier* did real
work: "no file under `nodes/` changed" is what turned the moderate-class ruling from an
assertion into something `git diff --stat` checks. It held — `kInlineBytes` went 16 → 32,
`node_tests` passed untouched. ADR-0002 records why fixed stride survived and why the
hot-path justification for it did not.

## Next claim

No longer provisional: it was drawn from evidence found while charting, and map ticket 03 has
since confirmed it — enlarged, and ruled in full. Ticket 04's rate probe may still promote
something ahead of it.

**Ticket 01's checklist promoted event payload shape ahead of this, and ticket 11's probe has
since demoted it again** — to moderate, and out of the way. The 16-byte payload genuinely
cannot carry a plain VST3 note-on, but the fix is a widened fixed inline array, which changes
no source under `nodes/`; the variable-stride shape that would have churned every event node
was rejected. So the "cost rises with every event node written" urgency was conditional on a
fix that isn't needed, and payload shape no longer competes for ordering. See
ADR-0002 and the claim below.

**Channel layout vs count (map ticket 12) has since been ruled the same way** — catastrophic on
paper, moderate in fact, and additive. That is two overshoots in a row from the same proxy, so
the oracle's blast-radius ranking is now treated as a prompt to measure rather than a class to
inherit. Its claim is below; it competes for ordering no more than payload shape does.

Both of the oracle's catastrophic-class items are therefore retired without churn, which leaves
the tail's ordering resting on the map's own tickets rather than on the checklist.

> **Claim.** An 80-band spectral stream and an 80-channel speaker bus cannot be connected to
> each other by mistake — and no file under `nodes/` changed to make that true.
>
> **Demo.** Declare `audio(80, kMel)` and `audio(80)`, attempt the connection, and require it
> to throw; the existing suites pass untouched and the purity grep still returns nothing.
>
> **Class.** Moderate — a defaulted `PortType` field plus two lines in `Engine.cpp`. The
> falsifier doing the real work is again "no file under `nodes/` changed", which `git diff
> --stat` checks. See ADR-0003.

*Sharpened 2026-11-15, after Save/Open landed.* The save file stores an audio port's channel
count in `sub` (`docs/graph-persistence.md`), so this claim also changes the save format.
Saving writes `MODULAR_GRAPH 2` with separate `sub` and `channels` fields. Opening still reads
version 1: an audio port's single number becomes `channels` with `sub` 0, and a value or event
port's stays `sub`. This is the first save-format change, so it sets the rule for later ones:
versioned, with older versions still opening. The `nodes/` falsifier stands; the change under
`workspace/` is expected, persistence tests change with it, and the demo gains one check: a
graph saved as version 1 still opens with every port's channel count intact. With ADR-0003 and
this note, nothing is left to grill; the next step is `/to-spec`.

> **Claim.** A wired, stateful node gains and loses ports mid-project without disturbing any
> other wire or its DSP state.
>
> **Demo.** Append a port to a wired node and delete a different one; every surviving wire
> still points at the port it named, and rendered output is unchanged. Deleting a *connected*
> port throws. The purity grep still returns nothing.
>
> **Class.** Expensive, and the first of the four rulings that earns the label — node
> constructor signatures change, so the library genuinely churns. See ADR-0004.

Why this was a live problem rather than a hypothetical: `kernel/include/modular/Node.h:56`
froze a node's ports once it joined a graph, so a plugin Configure gesture — user ticks
parameter #47 for automation on an already-wired plugin — required *replacing* the node.
That loses every connection referencing its `NodeId`, churns the plugin's DSP state, and
destroys its automation lanes. `Graph` had no deletion at all, so replacement was not merely
awkward but unimplementable.

Map ticket 03 has since ruled, and confirmed the claim while enlarging it. Ports are created
*and deleted* by command; deleting one that still carries a wire throws, which is RESTRICT
borrowed from referential integrity, and the same rule covers nodes and boundary nodes — so
**node deletion lands here too**, and ADR-0001's known fragility retires with it. The
enlargement is that "a port can be deleted" is unanswerable while a port's identity *is* its
position in a vector: identity moves to ids minted per graph, `NodeId` stops being an index,
and construction moves into the graph (`graph.addNode<GainNode>(1)`) because an id must be
correct when minted and can never be rewritten afterwards.

Two things the ticket killed are worth recording, since both looked like cheaper routes.
**Port-per-parameter** needs no kernel change at all — declare a port for every knob the
plugin publishes and let the view hide the rest — and dies on the fact that no plugin can
enumerate its knobs up front: Serum's reverb, compressor and EQ sections switch in and out
with their own parameters. **Params are not ports.** And the *flat* model of a supernode — a
recorded command sequence replayed into the outer graph, which then owns every id — dissolves
the difficulty entirely but costs structural encapsulation, since nothing would prevent a wire
reaching into the middle of a rack. Unnecessary: ticket 02 already named the recipe half.
Supernode is the container, **Template** the recipe, and an Ableton rack is a template on disk
and a supernode once in the project.

*Closed 2026-11-20*, cut as `.scratch/identity/` (spec, three tickets). The demo held as
written, and a second one carried it to the supernode face: wire a later outer port, delete
an earlier boundary node, and the render is byte-identical. Each demo was also run against a
temporary patch reintroducing positional identity and failed on the sample comparison, so
neither passes by construction.
The milestone, grouped, nested and block-size renders are unchanged, and the purity grep
still returns nothing.

Two edges are sharper than the claim's wording. **"Or its DSP state"** holds as far as
authoring reaches: a port edit changes the node in place instead of replacing it, so its
state is never rebuilt by the edit. Whether state survives the *recompile* that makes the edit
audible is map ticket 10's, still open. And **a boundary node's outer wires** are not the
kernel's to refuse. RESTRICT sees one graph, and an inner graph cannot see the wire into its
supernode, so deleting a boundary node whose outer port is wired succeeds and the compile then
rejects the stale wire. The user ruled that shared editing in `workspace/` removes such wires
with the edit, so one Undo restores both. That arrives with supernode editing (ADR-0004,
2026-11-15).

## Tail — ordered, unsized

Pending the map's tickets. Ticket 01's oracle ranked three of these directly: event payload
shape (settled — see above), channel layout vs count (settled — see above; the deficiency was
real, but it was a missing *subtype* rather than a missing layout), and **tail length**
(expensive,
and the clearest cheap-now/expensive-later item: the exact twin of `latencySamples()`,
earning its place independent of plugins because offline render is the primary verification
path). It also *lowered* one bar — dropout-free mid-playback recompile is stricter than any
spec requires, so PDC plus recompile-on-demand stays moderate provided the recompile-request
path is kept out of `Node`.

Remaining candidates and their suspected classes are tracked under "Not yet
specified" in `.scratch/roadmap/map.md`: rate / multi-rate (catastrophic, and currently
absent from CLAUDE.md's ledger entirely), the per-sample subgraph domain (catastrophic),
threading (expensive), dynamic polyphony (catastrophic), the command/edit API and its
offline-then-live cut, the constraint plus annotation layer, persistence and the subtype-id
policy, PDC and cycles (moderate), plugin-host node design, full realtime device IO with
JUCE, the DAW view layer, and engine-as-plugin-guest (last, and judged easy).

Ticket 03 moved two of these. **Persistence gained a hard requirement** — ids are minted at
authoring time now, so a save file must round-trip every id and each graph's `IdSource`
high-water mark; reusing an id after a reload resurrects a wire onto the wrong port. And it
delivered the **stable node ids** the command/edit API was waiting on, so that item is no
longer blocked behind the type-system question. It also spun off one new claim, ranked
moderate and independent of everything above: an unconnected `value` input currently reads
zero, which is a wrong answer rather than a missing one for a plugin parameter that must read
its current setting (map ticket 13).

## Out of scope

Multi-user collaboration (the biggest redraw risk — concurrent editing is a different
source-of-truth model), a binary third-party node ABI (freezing the kernel's C++ interface
while kernel requirements are still being discovered; *source*-level node contribution is
free and not out of scope), video / picture sync, and mobile platforms plus GPU DSP. See the
map for the reasoning on each.
