# Shared graph persistence

Ticket 03 adds stream and filesystem Save/Open in `modular_workspace`. The app
and other clients call the same operations. `modular_primitive_persistence`
supplies the initial node codecs, keeping node kinds and settings outside the
kernel and the shared container.

```cpp
#include "modular/GraphPersistence.h"
#include "modular/nodes/PrimitivePersistence.h"

modular::saveGraphFile(workspace, path, modular::nodes::primitiveCodecs());
auto replacement = modular::openGraphFile(path, modular::nodes::primitiveCodecs());
// Release any Engine borrowing the old graph before replacing its workspace.
workspace = std::move(replacement);
```

A client that saves soundfont nodes passes their codec beside the primitives', in
one span:

```cpp
std::vector<modular::NodeCodec> codecs(modular::nodes::primitiveCodecs().begin(),
                                       modular::nodes::primitiveCodecs().end());
codecs.insert(codecs.end(), modular::nodes::soundFontCodecs().begin(),
              modular::nodes::soundFontCodecs().end());
modular::saveGraphFile(workspace, path, codecs);
```

`saveGraph` and `openGraph` accept standard streams and the same codec span.
Filesystem paths use `std::filesystem::path`, including native Windows Unicode
paths. Save encodes the complete graph before opening a temporary file, writes
and closes it, then replaces the destination on the same filesystem. A temporary
sibling directory reserves ownership exclusively. Failure cleans up only that
save's temporary file/directory and retains the previous destination. This is
ordinary explicit saving, without autosave or a crash-recovery guarantee.

Open reconstructs a separate Workspace and creates no Engine. Only successful
Open returns a replacement. Errors identify malformed/unsupported content or an
unavailable file. Cycles remain authored wiring: the existing Engine reports
them when Run compiles the graph, as it does for newly authored cycles.

## Representation: MODULAR_GRAPH 1

The versioned text container is deliberately small, inspectable and independent
of native struct layout or a GUI library. Tokens occur in this order:

```text
MODULAR_GRAPH 1
NEXT 3
NODES 1
NODE 2 "modular.const-float/1"
SETTINGS "0.5"
CONTENT ""
POSITION 48 50
INPUTS 0
OUTPUTS 1
PORT 1 1 3 "value"
WIRES 0
END
```

- `NEXT` is the next graph identity, including every deleted or consumed ID.
  It is a 64-bit decimal value from 1 through 4294967296. The last value represents
  exhausted 32-bit IDs; further minting fails instead of wrapping.
- Nodes retain insertion order. Each has its original NodeId, versioned codec
  kind, node-owned settings text, stored bytes, finite x/y coordinates, and
  ordered input/output port lists.
- `PORT` stores PortId, numeric domain (Audio 0, Value 1, Event 2), the current
  PortType `sub` field and the name. This matches the current two-field PortType:
  audio channel count occupies `sub`. ADR-0003's additional field is not yet in
  the codebase; adopting it requires an explicit format migration/version.
- `WIRES` is followed by that many `WIRE srcNode srcPort dstNode dstPort` records.
  References and type/cardinality rules go through Graph's ordinary `connect`.
- Quoted strings use `std::quoted` escaping for quotes and backslashes. Stored
  bytes use lowercase hexadecimal within the quoted CONTENT field, including
  zero bytes. Whitespace separates tokens; there is no native-endian encoding.
- Numbers use the classic locale, with enough floating-point digits to round
  trip. Missing tokens, trailing content, invalid numbers, unknown versions/kinds,
  malformed bytes, duplicate identities, IDs outside the reserved history and
  mismatched port declarations are errors. Version 1 limits encoded files to
  256 MiB and bounds parsed counts by the available file size.

The ordinary graph format also applies to a supernode's inner graph. Parent
graphs containing supernodes or boundary nodes are explicitly unsupported here;
serialization never flattens them. Existing runtime composition is unchanged.

## Reconstruction and node content

`Graph(nextIdentity)` reserves a saved allocation history. A codec reconstructs
each primitive through `Graph::restoreNode<T>(id, ports, constructorArgs...)` in
saved node order. During that constructor, `addInput` and `addOutput` deliver the
saved IDs, names and types in their respective declaration orders. The kernel
checks unique, reserved identities and matching declared port types/counts.
Thus the node stores the correct IDs in its own private references from the
start. Later additions resume ordinary minting above the saved high-water mark.

The constructor must express the saved authored configuration. For the mixer,
the saved input list supplies its current arity, including surviving ports from
earlier edits; saved names and IDs need not be newly minted or contiguous.
Missing mandatory constant/gain/output ports and a zero-input mixer are explicit
unsupported configurations. The initial GUI cannot create these configurations.
Primitive deletion RESTRICT is unchanged.

Variable-arity growth checks the remaining identity capacity before mutating
ports. A failed node constructor can consume IDs before adoption; these remain
retired. The app marks changed allocation history unsaved even when construction
fails, so a later Save preserves it and abandoning it requires the usual prompt.

Each `NodeCodec` owns a stable kind name plus match, Save and restore functions.
`NodeContent` separates settings text from opaque stored bytes. Shared persistence
never interprets either. A codec must reject authored content it cannot reconstruct
and exclude execution state; when called during Run it may read only authored
data that remains stable during processing. Use exact concrete-type matching so
a derived node with additional owned content cannot be silently saved as its base.

The codecs cover ConstFloatNode, GainNode, MixNode, ManualTriggerNode,
MidiBlockNode, MidiFileNode, SineSynthNode, VoiceAllocatorNode, WavRecorderNode and, on Windows,
AudioOutputNode. Device codecs are unavailable on other platforms and produce
the normal unsupported-kind error there. Loading never opens an output device;
its configured channel count is restored and readiness is checked on Run.
The SoundFontNode codec lives in the same library behind its own accessor,
`soundFontCodecs()`; see [Owned soundfont representation](#owned-soundfont-representation).

### Recorder settings

`modular.wav-recorder/1` stores channel count followed by quoted UTF-8 output
directory and filename prefix in SETTINGS. CONTENT is empty. Capture buffers,
retained failed takes, pending Retry/Discard requests, write status and take
number search state are transient. Saving during capture or after a write
failure yields the same authored representation as saving idle. Opening does
not create the output directory or a WAV, and the reconstructed recorder starts
idle. A new completed take searches the destination for an unused numbered path.
Owned persistent audio content in other node kinds continues to use its codec's
CONTENT bytes; recorder transience does not change that shared contract.

### Owned MIDI representation

Each node kind has its own `/1` version; the generic `MODULAR_GRAPH 1` container
is unchanged. All stored MIDI numbers are classic-locale decimal text within
the opaque CONTENT bytes:

| Kind suffix | Settings | Decoded CONTENT |
| --- | --- | --- |
| `manual-trigger/1` | `start` or `stop` | Empty; pending presses are excluded |
| `midi-block/1` | Empty | Entry count, then `offsetFrames status data1 data2` for each entry |
| `midi-file/1` | `loaded` | Duration in seconds, skipped-SysEx count, entry count, then `seconds status data1 data2` per entry |
| `midi-file/1` | `empty` | Empty; an unconfigured node remains repairable with Load MIDI |
| `sine-synth/1`, `voice-allocator/1` | Empty | Empty; allocator arity comes from its surviving output declarations |
| `windows-audio-output/1` | Channel count, 1 or 2 | Empty |

Kind names above have the `modular.` prefix. Timestamps are stored with
`double`'s `max_digits10`, 17 significant digits, enough to recover the authored
`double` exactly. Graphs saved while times were MSVC's `long double`, which is the
same 64-bit type, open unchanged and save the same bytes. Simultaneous message order,
channel bytes and end-of-track duration survive. This preserves the parsed
meaning: source-file packaging and skipped SysEx payloads are not reconstructed.
Frames are computed again at preparation for the new sample rate.

`MidiFile::fromEntries` validates reconstructed meaning: finite nonnegative
ordered timestamps within duration and canonical channel messages (status
0x80–0xef, data bytes below 128, zero unused second byte for program change and
channel pressure). Block codecs require ordered frame offsets and the same
channel-message convention. Corrupt counts, numbers, messages and trailing
content fail explicitly. Constructors receive their saved port IDs through the
existing reconstruction contract, including an allocator with deleted voices.

Imports become owned content immediately. The original path is not required or
saved. A failed replacement import retains previous content; an empty node stays
not ready and offers Load MIDI. Transient import errors are not saved. Output
device failures remain node errors, with the app explaining default-device
repair and allowing another Run. No universal external-file policy was added.

Saving during Run reads only the authored MIDI file or block and configuration.
Prepared frame schedules, cursors, held notes, synth phases, allocator state,
pending manual presses and device queues are excluded. Reopening creates fresh
idle nodes. A test-only node also round-trips owned PCM through `NodeContent`,
without extending the container or adding a production audio-block player.

### Owned soundfont representation

`modular.soundfont/1` saves a `SoundFontNode` inside the graph, once and whole,
so a reopened graph plays after the original soundfont file is moved or deleted:

| State | Settings | CONTENT |
| --- | --- | --- |
| Loaded | `loaded bank program voiceLimit` | The soundfont file's bytes exactly as loaded, whole |
| Empty | `empty voiceLimit` | Empty; the node reopens empty, ready for a load |

The numbers are classic-locale decimal. No path or URL is saved, and neither are
the parsed zones, the values that depend on the sample rate or the voices, so
saving during Run yields the same representation as saving idle. Reopening parses
the bytes with the same parser as a load, selects the saved preset and restores
the voice limit. Bytes that no longer parse, an absent preset, a voice limit
outside 1 to 65535, malformed or trailing settings and an empty node carrying
content each fail Open with an explicit error, and the caller's graph stays as it
was. Like the other codecs, it matches the exact concrete type and saves only the
node's own ports, `midi` (`event<midi>`) in and `out` (`audio<2>`).

**A separate accessor.** The codec is not in `primitiveCodecs()`. If it were, the
Windows editor's Open would accept a soundfont graph and then fail while drawing
it, since the editor has no presentation for the node. So the editor, which passes
only `primitiveCodecs()`, refuses such a graph with
`open: unsupported node kind modular.soundfont/1`, as other platforms refuse a
device codec they lack. Giving the editor the node is a later, separate ticket:
running it under Start/Stop needs `MidiBlockNode` to count held notes per channel
and key.

**A size limit follows.** CONTENT is hex, two characters per byte, and version 1
caps an encoded file at 256 MiB, so a soundfont over about 128 MiB cannot be saved
in a graph: Save fails with `save: graph exceeds 256 MiB format limit`. The node
itself sets no limit. A denser
encoding, or sharing one soundfont between nodes, would be a container decision.

## Verification

Real-graph tests exercise settings, layout, topology, all surviving IDs and names,
middle-port deletion, later growth, trailing deleted IDs and exhausted allocation.
After loading, new signal/sink nodes drive the reconstructed constant/gain/mixer
through Engine and verify actual samples. Corrupt-content tests retain the caller's
original graph. File tests cover Unicode paths, replacement and failure preservation.

The actual Windows app tests cover Save/Open and Save As, restored layout/settings,
Edit after Open from Run, unchanged saved content during processing, unsaved choices
for New/Open/Close, cancelled Save, real Windows sharing-violation failures,
unsupported Save, invalid Open, no autosave, Discard and Save while closing a running
graph. The normal Debug/Release CTest suites include identity/supernode regressions
and kernel purity.

Owned-content coverage adds source-file removal, parsed tempo/channel-message
round trips, MIDI times that need all 17 digits checked against the bytes an
earlier Save wrote, fresh deterministic MIDI/audio comparison at several block sizes,
idle reload after an active/pending-control Save, stored PCM through a test codec,
allocator identity gaps, malformed content and import repair. The app smoke path
also closes and reopens in a fresh process after removing the import source.

Soundfont coverage builds its own SoundFont 2 files in memory
(`tests/SoundFontTestFiles.h`). It saves a node loaded from a path, deletes the
file, and checks the saved CONTENT is the file whole, trailing bytes included.
The reopened node keeps its preset and voice limit and renders the same samples at
two sample rates and four block sizes. It also covers an empty node reopened and
then loaded, saving during Run, every Open failure above, the exact-type and port
checks, and the refusal by `primitiveCodecs()` alone, both through `openGraph`
and through the actual editor's Open.
