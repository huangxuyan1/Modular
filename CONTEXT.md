# Modular

The ubiquitous language for Modular: a minimal graph runtime from which DAW-like
applications are meant to emerge. Terms here are the canonical vocabulary; `docs/adr/` and
`docs/project-foundations.md` hold the decisions, this file holds the words.

## Language

**Modular**:
The whole thing: a *language* for stream processing, with a compiler and a runtime, plus a
standard library and the tooling above it. Not "the engine" — that word understates a
language and collides with the `Engine` class.
_Avoid_: the engine (as a name for the product), the framework

**Kernel** (`modular_kernel`):
The language itself: graph, typed ports, connection rules, compile, block loop. Deliberately
the smallest layer; names no media concept, grep-enforced.
_Avoid_: core, runtime, engine

**Workspace** (`modular_workspace`):
The media-neutral authoring layer that holds an authored graph, its presentation data and
reusable editing behavior for interchangeable clients (DAW, CLI, script, RPC).
Hosts mechanism; the app supplies policy and content.
_Avoid_: shell, session, host, the engine

**Engine**:
The class, and only the class: one instance per compile, built from a Graph, discarded on
edit. `process()` and `streamTime()`. Owns wiring tables and storage — never node state,
which belongs to the Graph.
_Avoid_: using it for the product or for any layer

**Command**:
A reified mutation of the authored graph exposed by Modular to its clients; the GUI
invokes this shared behavior. It is the representation behind undo, atomic batching,
logging and scripting; queries and lifecycle calls are not commands.
_Avoid_: action, transaction, edit (as the type name)

**Template**:
A function that issues commands to build a subgraph — `makeTrack(ws)` and nothing more
ceremonious. Lives in the app that names it (`apps/daw/`), never in the workspace, because
its vocabulary is media vocabulary.
_Avoid_: preset, macro, factory

**Primitive node**:
A node whose behavior is defined in code. It is a *type*: instances reference it, and its
behavior follows the code — projects pick up changes when the code changes.
_Avoid_: built-in node, native node

**Supernode**:
A node whose implementation is an embedded graph it owns *by value* — a snapshot taken when
placed. Externally indistinguishable from a primitive node (ports only); later edits to the
source it was copied from do not propagate. Flattened away at compile.
_Avoid_: group, macro, subpatch, definition

**Boundary node**:
An ordinary-looking node inside a supernode's inner graph that marks one exposed port; the
supernode's outer face is derived from its boundary nodes. Never executes — the compiler
splices it out by rewiring through it. Exposure is wiring, not configuration.
_Avoid_: port mapping, inlet/outlet (as kernel terms), proxy port

**Authored graph**:
The graph as the user built it, with supernodes intact and nodes, ports and connections
available for authoring. The single source of truth from which execution and views are derived.
_Avoid_: patch, project graph

**Graph persistence**:
Saving and reconstructing an authored graph, its settings and stored node content through
Modular's shared facilities. Generated signals and in-flight processing are transient.

**Stored node content**:
Data a node retains as part of the authored graph, such as a MIDI block or an audio block.
It is saved with the node.

**Generated signal**:
The transient events, audio samples or values emitted while a graph runs.

**Graph session** (GUI app):
An open authored graph being worked on in the GUI. Its graph can be saved and later
reopened or reused inside another graph as a supernode.

**Take**:
The audio collected by the current WAV recorder between an accepted Start and Stop.
Each completed take is saved as its own WAV file.

**Executed graph**:
The graph the engine actually runs: supernodes flattened, ports fixed as the compile found
them, delay inserted by the compiler. Never edited directly; always derivable from the
authored graph.
_Avoid_: runtime graph, compiled patch

**Compile**:
The step that turns the authored graph into the executed graph (today: topo sort + storage
allocation in the Engine constructor; sprint 2 adds flattening). It also resolves ids into
whatever dense form the engine wants — unobservable from outside, since no node names another.
_Avoid_: bake, freeze (nothing is frozen; the compiled form is a snapshot)

**Variable arity**:
A node configuration that sets how many ports of a kind the node declares (e.g. a mixer's
input count). Changing it is a graph edit → recompile; the runtime never sees it change.
Since ADR-0004 it need not be set at construction — a node may add and delete ports for as
long as it is being authored.
_Avoid_: dynamic ports, fan-in

**Port id**:
A port's identity, minted by the graph its node lives in and never reused. What a connection
names on both ends. A port's *position* in `inputs()`/`outputs()` is display order only —
ordering, never identity, so deleting one port cannot silently repoint a wire onto another.
A supernode's outer port id is the port id of the boundary node backing it.
_Avoid_: port index, port number, slot

**Id source**:
The per-graph counter minting node and port ids. Owned by the graph but separately allocated,
because `Graph` is movable and a supernode holds one by value — a node holding a `Graph*`
would dangle. Its high-water mark is part of a saved project: reusing an id after a reload
would resurrect a wire onto the wrong port.
_Avoid_: allocator, registry, id pool

**Param**:
A knob *inside* a hosted plugin, in that plugin's own vocabulary. Not a port. Which params a
node exposes as ports is the node's private business, changed by command while the node sits
in the graph — no plugin can enumerate all of its params up front, so a port per param is not
available. Nothing in the kernel knows the word.
_Avoid_: parameter (as a kernel term), control, knob

**Port group**:
A presentation-level bundle of single-connection ports that reads as one multi-input port
in a view. The kernel knows nothing of groups; every connection underneath is one-to-one.
_Avoid_: multi-port, bus input

**Stream time**:
The engine's monotonic frame counter; never jumps. The only time the kernel knows.
_Avoid_: sample position, transport time, playhead

**Timeline position**:
Where in a project the user is reading; jumps on seek, wraps on loop. User-space — a
projection nodes compute, not a kernel quantity. The draggable playhead displays it.
_Avoid_: playhead (that's the UI widget), song position
