# Spec: Audio subtype — a spectral stream and a speaker bus of the same width refuse to connect

Status: ready-for-agent

Claim: **an 80-band spectral stream and an 80-channel speaker bus cannot be connected to each
other by mistake — and no file under `nodes/` changed to make that true.**

Falsified by: `audio(80, kMel)` connecting to `audio(80)`; any file under `nodes/` changed;
an existing suite needing a change beyond the one persistence case named below; the purity
grep returning anything; or a graph saved as version 1 failing to open, or opening with any
port's channel count or subtype changed.

Ruled in ADR-0003 (`docs/adr/0003-audio-ports-carry-an-opaque-subtype.md`) and its
"Persistence clarification (2026-11-15)", from map ticket
`.scratch/roadmap/issues/12-channel-layout-vs-count.md`, and sharpened by the dated note
under this claim in `docs/roadmap.md`. The test seams and three policy points were confirmed
by the user on 2026-11-23 ("Accept all four (Recommended)"). This spec is the implementation
cut. Beyond those confirmations it decides nothing the sources left open.

## Problem Statement

A graph author can wire an analysis signal into a playback path, and nothing stops them. An
80-band mel spectrogram node emits `audio<80>`, and an 80-channel speaker bus takes
`audio<80>`. The two ports compare equal, so `connect` accepts the wire and the render is
silently wrong. That is exactly the class of mistake the typed connect check exists to catch.

The cause is an asymmetry in the port type. A value or event port carries an opaque subtype,
which the kernel checks at connect time and never interprets. An audio port spends its one
slot on the channel count, which the kernel *does* read to size buffers. So audio is the only
domain with no way to say "what this signal is". Connect compares buffer shape and treats it
as meaning. That coincidence holds only while everything is mono or stereo.

The guarantee cannot be built in user space. `Graph::connect` is the only chokepoint that runs
at connect time. A tag carried on a value port could report a mismatch during `process()`,
but it could never refuse the wire.

Since ADR-0003 was ruled, graph persistence has landed, and it stores an audio port's channel
count in the saved port's `sub` field. A user's saved graphs therefore depend on the overload
being removed. Changing the port type without changing the save format would lose every
audio port's count on reopen, and changing the format without a migration would orphan every
graph saved so far.

## Solution

A port type has three fields: `domain`, an opaque `sub`, and `channels`. `sub` means the same
thing in all three domains: a subtype the kernel compares and never interprets. `channels` is
audio-only and is the one field the kernel reads. Connection stays exact equality over all
three fields. So `audio(80, kMel)` and `audio(80)` differ and `connect` throws, while the
kernel never learns what "mel" means.

Node authors keep writing `PortType::audio(N)`. It gains an optional second argument, the
subtype, which defaults to 0, so no declaration under `nodes/` changes. Subtype 0 is a
concrete type, "unspecified audio", not a wildcard. A node whose signal has a meaning beyond
its width declares that meaning with a non-zero id. The id is defined in the node library or
in user code, never in the kernel. An audio port with zero channels is now an authoring error.

Save writes container version `MODULAR_GRAPH 2`, which stores `sub` and `channels` as separate
numbers. Open still reads version 1. A saved audio port's single number becomes `channels`
with `sub` 0, and a value or event port's number stays `sub`. Graphs saved so far open
unchanged, and the next ordinary Save writes them as version 2. This is the first save-format
change, and it sets the rule for later ones: formats are versioned, and every older version
still opens.

The editor's port labels keep showing `audio<N>`, now read from `channels`.

## User Stories

1. As a graph author, I want connecting a spectral `audio<80>` output to an 80-channel speaker
   bus input to throw, so that an analysis signal never silently reaches a playback path.
2. As a graph author, I want the connect error to name both ports, as type mismatches already
   do, so that I can see which wire was refused.
3. As a graph author, I want two ports with the same subtype and the same channel count to
   connect exactly as they do today, so that correct graphs need no new ceremony.
4. As a graph author, I want two unspecified-audio ports of equal width to keep connecting, so
   that every existing mono and stereo graph keeps working.
5. As a graph author, I want ports of equal subtype but different widths to keep refusing to
   connect, so that the buffer-shape check I already rely on is not weakened.
6. As a node author, I want to declare an audio port with a subtype by passing one extra
   argument to the call I already use, so that adding meaning to a port is a one-word change.
7. As a node author, I want the subtype to default to 0, so that nodes that carry plain audio
   need no change at all.
8. As a node author, I want to define my own audio subtype ids in my own library or test code,
   so that the kernel never has to learn what my signal means.
9. As a node author, I want audio subtype ids to live in their own per-domain namespace, so
   that an audio subtype numbered 2 never collides with the MIDI event subtype.
10. As a node author, I want a generic node (gain, mixer) to stay plain audio, and to take a
    subtype as construction-time configuration only when one is needed, so that polymorphism
    uses the mechanism that already exists rather than a wildcard.
11. As a node author, I want declaring an audio port with zero channels to throw at once, so
    that a mistaken width is not read as permanent silence.
12. As a node author, I want reconfiguring a wired port to a different subtype to be refused
    while a wire would become invalid, so that a configuration edit cannot leave a wire
    joining two different types.
13. As a node author, I want the zero-channel rule to hold in Release builds too, so that the
    guarantee does not vanish in the configuration users actually run.
14. As a supernode author, I want an exposed boundary port to carry its full type, subtype
    included, so that grouping a subgraph cannot launder a spectral signal into a speaker bus.
15. As a kernel maintainer, I want the kernel to read only `channels`, and never `sub`, when
    sizing buffers, so that a subtype can never change how much memory a port gets.
16. As a kernel maintainer, I want the purity grep to stay clean, so that no media word enters
    the kernel to make this work.
17. As a GUI app user, I want my existing saved graphs to open exactly as before, with every
    audio port at its saved width, so that the format change costs me nothing.
18. As a GUI app user, I want saving a reopened old graph to produce the new format without any
    extra prompt, so that migration happens through the Save I already use.
19. As a GUI app user, I want opening an old graph not to rewrite it on disk, so that nothing
    changes until I choose to save.
20. As a GUI app user, I want port labels to keep showing `audio<1>` and `audio<2>`, so that the
    editor does not start reporting every audio port as `audio<0>`.
21. As a workspace client (CLI, script, app), I want `saveGraph` and `openGraph` to keep their
    signatures, so that no caller changes to adopt the new format.
22. As a workspace client, I want a saved graph containing a non-zero audio subtype to reopen
    with that subtype intact, so that the connect guarantee survives a save and reload.
23. As a workspace client, I want a reopened spectral port to still refuse a speaker-bus input,
    so that persistence cannot erase the meaning the type carries.
24. As a workspace client, I want a file with an unknown container version to fail with the
    existing "unsupported graph version" error and leave my open graph untouched, so that a
    file from a newer build cannot partially load.
25. As a workspace client, I want a version-2 file whose audio port claims zero channels to be
    rejected without replacing my current graph, so that corrupt files fail the way they
    already do.
26. As a workspace client, I want every rule that holds for version 1 to hold for version 2
    (the 256 MiB limit, count bounds, strict tokens, identity checks), so that the new
    version is no looser than the old.
27. As a node-codec author, I want my codec's `/1` kind version left alone, so that a
    container change never forces a node format change.
28. As a future format author, I want this change to establish that a format change bumps the
    container version and keeps every older version readable, so that I have a precedent
    instead of a fresh argument.
29. As a future format author, I want each container version to have a real file saved by that
    version's own writer committed as a test fixture, so that "still opens" is tested against
    real output, not a reconstruction of it.
30. As the roadmap owner, I want `git diff --stat` to show no path under `nodes/`, so that the
    moderate-class ruling is checked, not merely asserted.
31. As the roadmap owner, I want the existing suites to pass with exactly one deliberate edit (a
    persistence case that called version 2 unsupported), so that the blast radius is the one
    the persistence clarification predicted.
32. As a maintainer reviewing this change, I want the documentation to stop saying audio has
    no subtype field and that audio's count occupies `sub`, so that the docs match the code.
33. As an agent implementing this, I want the port label checked by a screenshot that the
    `desktop`-labelled editor test takes, so that the one visual check needs no manual step.

## Implementation Decisions

**Port type (kernel).**
- The port type gains a third field, `channels`, beside `domain` and `sub`. The declaration
  order and field names are the ones ADR-0003 gives. `sub` is the opaque subtype in all three
  domains. `channels` is meaningful only for audio, and it is the one field the kernel
  interprets.
- Equality, and therefore `connect`, compares all three fields. Equality's role is unchanged.
- The audio factory keeps its call shape and gains a defaulted subtype: `audio(channels,
  subtype = 0)`. The value and event factories are unchanged and produce `channels` 0.
- Audio subtype 0 is a concrete, unspecified subtype, not a wildcard. Inputs do not accept a
  wildcard either; ADR-0003 rejected both.
- The kernel's port type comment is rewritten. `sub` is no longer described as the audio
  channel count.

**Engine (kernel).**
- Every Engine site that reads an audio port's width reads `channels` and never `sub`. Today
  there are three such sites, one more than ADR-0003 named: the width of the shared silence
  buffer, output buffer allocation, and each audio input's channel count. Engine behavior is
  otherwise unchanged.

**Zero channels (kernel).** User-confirmed on 2026-11-23; this replaces ADR-0003's word "assert".
- An audio port with `channels` 0 is an authoring error and throws `std::invalid_argument`,
  in every build configuration. That follows the house rule that kernel authoring errors
  throw.
- The check lives where a port type enters a node: declaring a port (including during
  reconstruction) and node-owned port type changes. Boundary nodes declare their port through
  the same path, so they are covered. A check only in the factory would be bypassed by
  aggregate construction.
- No new kernel rule is added for value or event `channels`. The factories produce 0, and the
  existing declaration-matching during reconstruction already rejects a saved value or event
  port whose type differs from what the node declares.

**Connect and node configuration (kernel).** `connect`'s signature and error behavior are
unchanged; only the equality it calls compares one more field. The node-owned port type
change re-validates every attached wire with the same equality, so a subtype change that
would invalidate a wire is refused atomically, as a width change already is.

**Node library.** No change. No subtype ids are defined, `kMel` included; it exists only in
tests. Generic nodes stay plain audio. The first node that emits a spectral or surround signal
will define the id it needs.

**Graph persistence (workspace): container version 2.**
- Save always writes `MODULAR_GRAPH 2`. It never writes version 1.
- The only record that differs between versions is `PORT`. In version 2 its tokens are, in
  order: port id, numeric domain, `sub`, `channels`, quoted name. Value and event ports write
  `channels` 0.
- Open accepts versions 1 and 2, and any other version fails with the existing
  unsupported-version error.
- A version-1 `PORT` carries one number:
  - for an audio port, it becomes `channels` and `sub` is 0;
  - for a value or event port, it becomes `sub` and `channels` is 0.
- Every other record (`NEXT`, `NODES`, `NODE`, `SETTINGS`, `CONTENT`, `POSITION`,
  `INPUTS`/`OUTPUTS`, `WIRES`, `END`) and every existing validation rule applies unchanged to
  both versions: the 256 MiB limit, count bounds, strict numbers, identity checks, trailing
  content, and wires through ordinary `connect`.
- An audio port with zero channels in either version fails Open, through the zero-channel rule
  or declaration matching, and the caller's graph is retained as with any other corrupt file.
- The container version is independent of node kind versions. Every codec keeps its `/1` kind
  and its settings and content encoding, which is why no codec under `nodes/` changes.
- The public persistence API (stream and file Save/Open, codec span) keeps its signatures. Its
  header comment changes from "version 1" to describe version 2 and version-1 reading.
- Open stays free of side effects. Opening a version-1 file never rewrites it. The file
  becomes version 2 only through the user's next ordinary Save (there is no autosave).
- The precedent this sets, which the persistence documentation records: a format change bumps
  the container version, every older version keeps opening, and each version has a fixture
  that its own writer saved.

**Editor port label (GUI app).** User-confirmed on 2026-11-23.
- The audio branch of the port type label reads `channels` instead of `sub` and keeps the
  `audio<N>` text. This is a one-line change.
- The label does not show a non-zero audio subtype, because no node the editor can place
  declares one.

**Documentation.**
- The graph persistence document:
  - its representation section moves to version 2, with the new `PORT` token order;
  - it gains a short section on how version 1 is read;
  - it states the versioning precedent;
  - it drops the note that ADR-0003's field "is not yet in the codebase".
- The graph editor document drops "There is no separate audio subtype field yet" and says the
  label shows `channels`.
- The project foundations document already describes the three-field port type and needs no
  change. The roadmap and the ADRs are not edited by this work.

## Testing Decisions

**What a good test is here.** A good test observes behavior only through the two existing public
seams below. It does not unit-test the `PORT` record parser, Engine's wiring tables or
any other internal. If those are wrong, a seam test fails. Tests follow existing conventions:
doctest, one file per layer, test names written as invariant sentences, and helper nodes and
codecs local to the test file.

**Seam 1 — `Graph`'s public `connect` (kernel tests).** User-confirmed on 2026-11-23.
- **The demo**, which must fail against today's code (where the two-argument audio factory
  does not exist): the test defines `kMel` locally as any non-zero id. A test-only node outputs
  `audio(80, kMel)`, another node has an `audio(80)` input, and `connect` throws
  `std::invalid_argument`.
- Controls: `audio(80, kMel)` → `audio(80, kMel)` connects; `audio(80)` → `audio(80)` connects.
- Equal subtype with a different width still throws.
- An audio subtype never equals an event or value subtype with the same number.
- Declaring an audio port with zero channels throws, and so does reconfiguring an existing
  port to zero channels.
- A boundary node exposing `audio(80, kMel)` gives the supernode's face that full type, and
  wiring that face to `audio(80)` throws.
- A node-owned subtype change on a wired port is refused, and leaves every port type as it was.
- Prior art: "connect enforces exact type equality", the supernode face tests, and the
  channel-edit tests ("channel edits preserve ports and reject incompatible wires without
  partial changes").
- Engine reading `channels` needs no new render test. The existing mono, stereo and mixer
  render tests (including the save/open test that drives a reconstructed stereo graph through
  the Engine) fail if any Engine site still reads `sub`.

**Seam 2 — `saveGraph` / `openGraph` (authoring tests).** User-confirmed on 2026-11-23.
- A test-only node and codec declaring an `audio(80, kMel)` output round-trip through Save and
  Open. The saved text begins with `MODULAR_GRAPH 2`, the reopened port's type equals
  `audio(80, kMel)` with its subtype intact, and connecting it to an `audio(80)` input still
  throws. Prior art: the `StoredAudio` test node and codec.
- **The demo's version-1 check, using a real version-1 fixture.**
  - The fixture is saved by today's (version-1) Save before the writer changes, and committed
    under the test fixtures directory.
  - It covers a mono audio wire, a stereo audio wire, a value wire and an event wire, using
    only codecs available on every platform (no Windows output device).
  - The test opens it and checks each port against literal expected values written in the
    test, not values recomputed by code: audio ports have their saved `channels` and `sub` 0,
    and value and event ports have their saved `sub` and `channels` 0. Every wire is restored.
  - Saving the reopened graph writes version 2. Opening that output reproduces identical ids,
    names, types and wires.
- Prior art: the fixture-directory define authoring tests already use for `one-note.mid`, and
  "shared save and open preserve authored identities and later minting".
- A version-2 file with an audio port at zero channels fails to open, and the caller's current
  graph is untouched. Prior art: "open rejects corrupt identities ports settings content and
  references without replacement".
- **The one deliberate change to an existing test.** In that corrupt-input test, the
  unsupported-version case `MODULAR_GRAPH 1` → `MODULAR_GRAPH 2` becomes `MODULAR_GRAPH 3`.
  Every other existing test stays untouched. The hand-written version-1 strings already in the
  persistence and editor tests now double as version-1 Open coverage.

**Checks that close the claim.**
- `git diff --stat` against the base shows no path under `nodes/`.
- The kernel purity CTest passes.
- The Debug and Release suites pass, including the two `desktop`-labelled editor tests
  (`graph_editor_app`, `graph_editor_gesture`). They run by default locally, and a separate
  change moves them onto a private Win32 desktop so that no window appears on the user's
  screen. CI excludes them with `ctest --label-exclude desktop`.
- The user-confirmed visual check for the label is one screenshot taken by the
  `desktop`-labelled editor test, showing a placed gain or mixer node with `audio<2>` on its
  ports.

## Out of Scope

- **Defining any subtype ids** in the node library: mel, spectral, layouts such as VST3
  `k30Cine`, `k30Music`. Ids without consumers are speculative design. The first node that
  emits one adds it.
- **Subtype wildcards**, on ports or on inputs only, and any type inference in compile. ADR-0003
  rejected them, and they reopen only with their type-inference cost attached.
- **Showing a non-zero audio subtype in the editor**, or any editor control for choosing one.
- **Sample rate in the port type** (map ticket 04). ADR-0003 notes it would land beside
  `channels` as a second interpreted field.
- **A subtype-id registry or numbering policy** across libraries. That stays the convention
  question the node library's payload header already defers.
- **A kernel rule that value and event `channels` must be 0.**
- **Writing older versions**: no "save as version 1", and no downgrade path.
- **Persisting supernodes or boundary nodes.** They remain unsupported by Save, unchanged.
- **Bumping any node codec kind version**, or changing any codec's settings or content
  encoding.
- **Renegotiating bus arrangements.** That stays node configuration time, outside the kernel,
  per ADR-0003.

## Further Notes

- **Blast radius re-derived, as ADR-0003 asks.** The ADR estimated "a defaulted struct field
  plus two lines in `Engine.cpp`; nothing in `tests/`". The persistence clarification already
  widened that. The actual list is:
  - the port type;
  - three Engine sites;
  - the zero-channel check;
  - the workspace persistence reader and writer;
  - one editor label line;
  - one existing test case;
  - new tests and a fixture;
  - documentation.

  Nothing under `nodes/` changes. The class stays moderate.
- **The fixture is committed text and must stay byte-exact.** The repository's only line-ending
  rule covers shell scripts, and Windows checkouts here use `core.autocrlf=true`, so the
  fixture needs a `.gitattributes` entry (`-text` or `eol=lf`). Its entry in the fixtures
  README should record its provenance: written by version-1 Save at a named commit, before
  the version-2 writer, and containing no personal data. That README already documents
  provenance for the public tree.
- **Order within the implementation.** Generate and commit the fixture before changing the
  writer; after the change, the code can no longer produce a version-1 file.
- **`CaptureSinkNode(0)`** has no zero check of its own and will now throw from the kernel. No
  test constructs it with zero. The gain, mixer, recorder and output nodes already reject zero
  themselves.
- **Glossary gap.** `CONTEXT.md` has no entry for port type or subtype, though both are now
  load-bearing. Note it for `/domain-modeling`. This spec does not add the term.
- **Checked against current `main`.** The identity claim's close (`0d9396a`) and the CI gate
  (`a8c433b`) landed after the sources above were written. Neither changes what this spec
  relies on:
  - the three Engine sites;
  - the persistence writer;
  - the editor label;
  - the unsupported-version test case.

  This change lands through a PR into `main`, and the PR must pass `CI gate`.
- **Closing the claim.** Per the roadmap's convention, the claim closes with a commit whose
  subject *is* the claim. Recording it among the roadmap's proven claims is part of that
  close-out, not of this spec.
