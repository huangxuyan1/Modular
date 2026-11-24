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

**Status:** ready-for-agent

## Checklist

- [ ] A codec of kind `modular.soundfont/1` lives in `modular_primitive_persistence`.
  - It is behind its own accessor beside `primitiveCodecs()`, not inside it.
  - A client that saves soundfont nodes passes both spans.
  - The generic `MODULAR_GRAPH 1` container and `workspace/` do not change.
- [ ] CONTENT holds the soundfont file's bytes exactly as loaded, whole.
- [ ] SETTINGS holds the bank, program and voice limit, or for an empty node an empty state
  with its voice limit.
- [ ] No path, and no parsed, prepared or voice state, is saved. Saving during a run reads only
  authored content.
- [ ] Reopening parses the bytes with the same parser as a load, selects the saved preset and
  restores the voice limit.
- [ ] Each of these fails Open with an explicit error and leaves the caller's graph unchanged:
  - bytes that no longer parse;
  - an absent preset;
  - an invalid voice limit;
  - malformed or trailing settings;
  - an empty node carrying content.
- [ ] The codec checks ports like the other codecs (`midi` in, `out` audio<2>) and matches the
  exact concrete type.
- [ ] A reopened graph renders the same samples as the original, at two sample rates and
  several block sizes, after the original soundfont file has been deleted. An empty node
  reopens empty, and a load then makes it ready.
- [ ] `openGraph` with `primitiveCodecs()` alone refuses a soundfont graph with
  "open: unsupported node kind modular.soundfont/1".
- [ ] `docs/graph-persistence.md` records:
  - the kind and its representation;
  - the separate accessor and why;
  - the limit that follows: hex doubles the size, and the container caps a file at 256 MiB,
    so a soundfont over about 128 MiB cannot be saved in a graph.
- [ ] Nothing changes under `kernel/` or `workspace/`. The full suite passes, and
  `/code-review` runs on the branch's diff.
