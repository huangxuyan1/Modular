# 06: Save and reopen a soundfont node

**What to build:** A graph author saves a graph holding a soundfont node, deletes the original
soundfont file, and reopens the graph. The node comes back with its soundfont inside it, its
preset and its voice limit, and renders the same samples. An empty node saves and reopens
empty, ready for a load. A saved soundfont that no longer parses fails Open with an explicit
error and leaves the current graph alone. The Windows editor, which does not know the node,
refuses such a graph at Open with its ordinary unsupported-kind error. See the
[spec](../spec.md) (ticket 05, D5, and the rulings made while writing the spec).

**Blocked by:** 01: Tracer: a loaded soundfont plays notes at their pitch through the volume
envelope.

**Status:** done

## Checklist

- [x] A codec of kind `modular.soundfont/1` lives in `modular_primitive_persistence`.
  - It is behind its own accessor beside `primitiveCodecs()`, not inside it.
  - A client that saves soundfont nodes passes both spans.
  - The generic `MODULAR_GRAPH 1` container and `workspace/` do not change.
- [x] CONTENT holds the soundfont file's bytes exactly as loaded, whole.
- [x] SETTINGS holds the bank, program and voice limit, or for an empty node an empty state
  with its voice limit.
- [x] No path, and no parsed, prepared or voice state, is saved. Saving during a run reads only
  authored content.
- [x] Reopening parses the bytes with the same parser as a load, selects the saved preset and
  restores the voice limit.
- [x] Each of these fails Open with an explicit error and leaves the caller's graph unchanged:
  - bytes that no longer parse;
  - an absent preset;
  - an invalid voice limit;
  - malformed or trailing settings;
  - an empty node carrying content.
- [x] The codec checks ports like the other codecs (`midi` in, `out` audio<2>) and matches the
  exact concrete type.
- [x] A reopened graph renders the same samples as the original, at two sample rates and
  several block sizes, after the original soundfont file has been deleted. An empty node
  reopens empty, and a load then makes it ready.
- [x] `openGraph` with `primitiveCodecs()` alone refuses a soundfont graph with
  "open: unsupported node kind modular.soundfont/1".
- [x] `docs/graph-persistence.md` records:
  - the kind and its representation;
  - the separate accessor and why;
  - the limit that follows: hex doubles the size, and the container caps a file at 256 MiB,
    so a soundfont over about 128 MiB cannot be saved in a graph.
- [x] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.

## Comments

**2026-11-24 — implemented.** Branch `claude/soundfont-node-06-save-and-reopen`, cut from a
freshly fetched `origin/main` at `c6246ee`. Before the first push it was rebased onto
`39155a8`, which had moved with planning changes only.

- *What was built.*
  - `soundFontCodecs()` sits in `modular_primitive_persistence`
    (`nodes/src/PrimitivePersistence.cpp`), beside `primitiveCodecs()`. It holds
    `modular.soundfont/1`.
    - SETTINGS is `loaded bank program voiceLimit` or `empty voiceLimit`.
    - CONTENT is the file's bytes, whole, or nothing for an empty node.
  - Restore parses with `SoundFont::parse`, then calls `setPreset` and `setVoiceLimit` on
    the restored node, so the node's own checks and messages apply.
  - Two header-only accessors were added. Neither `SoundFont.cpp` nor `SoundFontNode.cpp`
    changed.
    - `SoundFont::bytes()` returns the file as loaded.
    - `SoundFontNode::soundFont()` returns the loaded file and throws while empty, as
      `MidiFileNode::file()` does.
  - `modular_primitive_persistence` now links `modular_soundfont`.
  - The docs:
    - `docs/graph-persistence.md` gains "Owned soundfont representation": the kind, the
      separate accessor and why, and the 128 MiB limit.
    - `docs/node-io.md` gains a Saving paragraph and the accessor's row.
    - `docs/graph-editor.md` records the editor's refusal.
- *Tests.* `tests/test_authoring.cpp` has six new doctest cases (128 assertions). The
  editor app check gained one step.
  - A node loaded from a path is saved and the file deleted. CONTENT is the file's hex,
    trailing bytes after the RIFF chunk included. The reopened node keeps bank 1, program 5
    and voice limit 1. It renders the same samples as a freshly authored graph at 44.1 and
    48 kHz, at blocks of 1, 17, 127 and 512.
  - Saving during a run writes the same text as saving idle.
  - An empty node with limit 9 reopens empty with that limit. `Engine` then throws
    `soundfont: load a soundfont first`. A load makes it ready, and it sounds.
  - `primitiveCodecs()` alone refuses the graph at Open with
    `open: unsupported node kind modular.soundfont/1`, and refuses it at Save.
  - 20 corrupt settings and content cases and 2 port cases each fail Open with an exact
    message and leave the caller's graph unchanged:
    - truncated, renamed and missing bytes;
    - an absent preset;
    - voice limits of 0 and 65536;
    - malformed and trailing settings;
    - an empty node carrying content.
  - Save refuses a derived node and a node whose `midi` port was removed.
  - `graph_editor_app`: the real editor opens a graph holding a soundfont node, shows
    `open: unsupported node kind modular.soundfont/1`, and keeps its current graph and dirty
    state.
- *Red.* The round trip was written first. It failed at runtime, with
  `save: unsupported node kind`, against an empty `soundFontCodecs()`. The codec was then
  written whole, so the later cases passed on their first run. Temporary mutations
  confirmed that each fails when its behaviour breaks. Each mutation was then restored:
  - the empty node's voice limit not restored;
  - the trailing-settings check removed;
  - the empty-with-content check removed;
  - matching by `dynamic_cast` instead of the exact type;
  - `requirePorts` removed;
  - the editor step expecting another kind.

  Two cases were not mutated: the refusal by `primitiveCodecs()` alone and saving during a
  run. They characterise the ruling and the codec reading only authored state.
- *Checks, on the final tree.*
  - Focused: `-Target authoring_tests -TestRegex '^authoring_tests$'`, 28 cases and 624
    assertions.
  - Full suite through the script: Debug 9/9 and Release 9/9 CTest entries. These include
    both `desktop` tests and kernel purity.
  - `python scripts/check_docs.py`: 0 problems.
  - `git diff --check`: clean.
  - Nothing under `kernel/` or `workspace/` changed.
- *Review.* `/code-review` ran on both axes over `git diff origin/main...HEAD` at
  `fc597a4`, with a clean working tree.
  - Standards found no hard violations. Fixed:
    - The settings' bank and program are a `std::optional<SoundFont::PresetNumber>`,
      empty for an empty node, instead of a flag and two numbers.
    - The restore's repeated loaded check became one branch.
    - A CMake comment now explains the new link.
    - In the test helpers, `block` became `notes` and `node()` became `synthNode()`, so the
      MIDI block's id no longer shares a name with the block size.
    - A 94-column line was wrapped.
  - Standards, kept:
    - The settings parser repeats the whole-token `from_chars` check a third time. Sharing
      it would change `ContentReader::number`, which the `long double` paths use and spec
      B's first ticket moves.
    - `renderSoundFont` resembles `renderPlayback`, but each serves its own graph.
  - Spec found nothing missing or wrong. Kept:
    - Two accessors, not one. The reader's bytes are private, so the reader needs one. The
      node's mirrors `MidiFileNode::file()`.
    - The settings parser tolerates extra whitespace, as `ContentReader` and the recorder's
      settings do.
    - The editor now links the soundfont library through persistence, though its Open
      refuses the kind. See the open notes.
- *Judgement calls.*
  - The words `loaded` and `empty` mirror `modular.midi-file/1`.
  - The node's own setter checks the voice limit after the parse, so an invalid limit costs
    one parse before Open fails.
  - Errors from the reader and the node keep their `soundfont:` prefix. The codec's own
    errors use `open:`.

**Open notes for the planning hub.**

- *What the editor links.* The codec shares `requirePorts` with the primitive codecs in one
  source file, so every client of `modular_primitive_persistence`, the Windows editor
  included, now links `modular_soundfont`. It already links `modular_midi` the same way. If a
  music-free client of persistence matters later, `soundFontCodecs()` can move to its own
  source file with a small internal header.
- *Ticket 02 runs in parallel.* This branch adds one accessor each to `SoundFont.h` and
  `SoundFontNode.h`, and edits the status line and one paragraph of `docs/node-io.md`'s
  soundfont section. Whichever branch merges second may need to resolve a small conflict.
