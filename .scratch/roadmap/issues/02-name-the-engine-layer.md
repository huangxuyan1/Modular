# Name the layer: "engine" the product vs `Engine` the class

Type: grilling
Status: resolved

## Question

"Engine" is overloaded. `CONTEXT.md` has no entry for it, yet `Engine` is already the class
that compiles a graph and runs the block loop — while in conversation "the engine" means
the whole shippable thing: kernel + node library + command API + constraint mechanism.
Two different things under one word will infect every ticket and every roadmap entry.

Decide the vocabulary and record it in `CONTEXT.md`:

- What is the product layer called, given `Engine` is taken? It must not be "the kernel"
  (that's deliberately smaller) and not "the runtime" (arguably also taken).
- Which layer owns the **command/edit API** — kernel, or the product layer above it? The
  kernel currently exposes `Graph::addNode` and connection edits directly; a command API
  with stable ids, atomic edits and undo is a different surface with different guarantees.
- Which layer owns the **constraint mechanism**? Already ruled: the engine hosts a
  mechanism, never DAW content — apps supply the content, or "engine provides restrictions"
  becomes "engine knows what a track is" in one careless step. But "engine" here needs to
  resolve to a specific layer.
- Does the layering rule extend? The kernel must never name a media concept
  (`scripts/check_kernel_purity.sh` enforces it). Does the new layer inherit that rule,
  weaken it, or get an explicitly different one?

Feeds ticket 06 (constraints/annotation) and ticket 07 (command API cut), both of which
need to say which layer they land in.

## Answer

### 1. The layer is real, and it is a fourth target

Not a bundle name. `modular_workspace` sits between `modular_nodes` and the apps. It has no
code today — this ticket rules on what it will own, not on its interface.

```
apps/  (daw, render_cli, captioning …)   ← media words spelled here
  → modular_workspace   ids, undo, constraints, engine lifecycle
  → modular_nodes       the standard library
  → modular_kernel      graph, compile, block loop
```

### 2. "Engine" as a product word retires

Modular is a **language** for stream processing, with a compiler and a runtime; `nodes/` is
its standard library. That framing is not imported — it is already the project's own
working metaphor, load-bearing in ADR-0001's placement argument ("like functions: language,
not stdlib"), and structurally true: there is a compile step, flattening *is* inlining, and
a primitive node is a type whose instances reference it.

So `Engine` the class keeps its identifier — it is the precise use of the word (one
instance per compile, discarded on edit, `process()` and `streamTime()` and nothing else).
The vague conversational sense ("the engine" = all of it) is what dies; that meaning is
already spelled **Modular**. Renaming the class to free the word for the product was
rejected: it pays churn to keep a word that understates a language.

Note "for audio processing" is narrower than the kernel actually is — audio is one of three
domains, and transcription/speech pipelines are deliberate targets.

### 3. The workspace owns edits *and* engine lifecycle — mechanism only

Owns: stable node ids, atomic edits, undo, the constraint mechanism, and compiling /
holding / swapping the current `Engine`.

Editing and lifecycle are one job, not two: `Engine`'s own contract is that editing means
building a new `Engine`, so whoever owns the edit owns the recompile it forces. Splitting
them puts an edit on one side of a seam and its unavoidable consequence on the other.

But **the app calls recompile as needed** — edits do not implicitly recompile. The authored
graph and the running engine may diverge; the workspace tracks that it is dirty, the app
decides when to reconcile. Mechanism in the layer, policy in the app — the same shape as
the constraint ruling.

### 4. Named for what it owns, not for its surface

`modular_workspace`, **provisional and revisable**. It has zero code, so renaming costs one
CMake string and one `CONTEXT.md` entry; it only becomes expensive once headers, namespaces
and app call sites exist. Do not spend more on the name than that.

Rejected: **shell** — the kernel/shell pairing is legible and came from the right
instinct ("like a terminal from the user's point of view"), but a CLI is one *front-end*
among several (DAW, script, RPC), and naming the layer after a front-end mistakes the
window for the room. Also collides with "supernode shell", used 10× in prose.
**Session** — reads as a Pro Tools document, dragging DAW content into a layer that must
not know what a track is. **Runtime**, **kernel**, **core** — taken, or a distinction
without a difference.

What does not move is what the layer owns, so that is the naming axis.

**Operation kinds** (a property of operations, not a count of surfaces — a CLI or an API
can carry all three): **mutations** reify as command objects, which is what makes undo,
atomic batching, logging and scripting fall out of one representation instead of three
bolted-on features; **queries** (enumerate nodes, derive a supernode's outer face, read a
value) and **lifecycle** calls are plain — they do not undo, do not batch, do not belong in
a history. Ticket 07 inherits this as the rule for which operations get the command
treatment.

### 5. The purity rule extends to the workspace

`modular_workspace` may not name a media concept; extend `check_kernel_purity.sh` (→
`check_layer_purity.sh`) when the layer has its first file.

The reason is not drift-prevention, it is reach: `track` or `note` in the workspace would
foreclose a batch audio captioning pipeline, which has no music in it at all. The layer
serves generalist tools precisely by refusing music vocabulary. The DAW names things in its
own way.

| layer | media vocabulary |
|---|---|
| `modular_kernel` | banned, grep-enforced |
| `modular_workspace` | banned, grep-enforced (new) |
| `modular_nodes` | allowed by design — `MidiNodes.h` exists to name it |
| apps | free — this is where "track" is spelled |

This also keeps a build check behind the existing constraint ruling ("the engine hosts a
mechanism, never DAW content"), which otherwise would have been an intention in a markdown
file.

### Corollaries

**Templates are functions, not a mechanism.** A template is a function that issues commands
to build a subgraph — `makeTrack(ws)` calls addNode/connect and returns an id. It needs
nothing the command API does not already provide, so a "template mechanism" in the
workspace was proposed and dropped as a concept without information.

**Templates live in the app that names them** — `apps/daw/` holds the DAW template; a user
building a DAW starts from the demo project and edits it. A shared `templates/` folder is
*not* created now; it earns its place only if a second app wants the same content.

**The DAW stays in this repo** (`apps/daw/`, beside `apps/render_cli/`), not a separate
project. Applications are adversaries, and an adversary in another repo runs against a
pinned, stale kernel — version skew converts a fast falsification into a late one. The
terminal claim also needs somewhere to be tested: it is a demo that must be able to fail,
in ctest, beside the others. And a repo boundary would freeze an interface still being
discovered — the same argument that put a binary node ABI out of scope, and the research
delivered under ticket 01 has just found six contradictions with settled decisions.
Splitting later is cheap (an app has no reverse dependencies); splitting now costs the
feedback loop the method is built on.

**Stated expectation, deliberately:** the DAW must not be the only adversary. A UI DAW
in-repo will tug the project DAW-shaped, which is what this architecture exists to resist.
A captioning or transcription app under `apps/` is what keeps the workspace honest about
never naming a note. Feeds ticket 09.

### Found on the way (not settled here)

Hot editing is in better shape than CLAUDE.md's "hard/open" flag suggests. `Graph` owns
nodes as `std::vector<std::unique_ptr<Node>>` (`Graph.h:52`); `Engine::Impl` owns only
wiring tables and storage (`Engine.cpp:40-42`), holding raw `Node*`. So recompiling the
*same* Graph reuses the same node objects — filter state, phase and voice assignments
survive a swap for free, and the `unique_ptr` indirection keeps node addresses stable when
the vector grows. The shape is: build Engine #2 on the editing thread, publish by atomic
pointer swap, destroy Engine #1 off the audio thread. No state transfer problem, because
there is no state to transfer.

Two hazards remain, and the first became ticket 10: `prepare()` is called on every node at
every Engine construction (`Engine.cpp:198`) while `Node.h:86` documents it as called once
— so a recompile may wipe exactly the state the swap preserved; and PDC delay lines are
Engine-owned storage, so a latency change discards their contents mid-stream. Ticket 08
owns the probe.

Status: resolved
