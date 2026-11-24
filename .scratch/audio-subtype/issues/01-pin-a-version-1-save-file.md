# 01: Pin a real version-1 save file as a fixture

**What to build:** graph persistence is about to make its first format change, to container
version 2. The rule that every older version keeps opening needs a real version-1 file to
test against: one that today's Save wrote, not text reconstructed by hand. This ticket saves
such a graph with today's version-1 Save, commits it unedited with the test fixtures, and
adds a persistence test that opens it and checks what comes back.

The graph covers every kind of port a saved graph holds today: a mono audio wire, a stereo
audio wire, a value wire and an event wire. It uses only node kinds whose codecs exist on
every platform, so no Windows output device. The test states its expected values literally
and does not recompute them. It writes them with the port type factories: `audio(N)` for an
audio port, and `value(id)` / `event(id)` for the others.

Those assertions mean the same thing before and after ADR-0003's third field lands. That is
the version-1 reading rule in the ADR's persistence clarification: an audio port's single
number is its channel count with subtype 0, and a value or event port's number is its
subtype.

No production code changes. This ticket lands before the writer changes, so the repository
history shows that the version-1 writer produced the file. Ticket 02 must then pass this test
without editing it or the fixture.

**Blocked by:** None (can start immediately).

**Status:** done

- [x] The graph is saved by the current version-1 Save and committed unedited with the test
      fixtures. It contains at least one mono audio wire, one stereo audio wire, one value
      wire and one event wire, and is built only from node kinds available on every platform.
- [x] A repository line-ending attribute exempts the fixture from conversion, so it stays
      byte-exact across checkouts. Windows checkouts here use automatic CRLF conversion.
- [x] The fixtures README records the file's provenance: written by version-1 Save at a named
      commit, before any version-2 writer existed, and containing no personal data.
- [x] A persistence test opens the fixture through the public Open. It checks these against
      literal expected values:
      - every node id;
      - every port id, name and type: audio ports by channel count with subtype 0, value and
        event ports by subtype;
      - every wire;
      - the saved next identity.
- [x] When the fixture is created, opening it and saving it again with today's code
      reproduces it byte for byte. This is a one-off check recorded in this ticket's
      comments, not a test, because Save will write version 2 after ticket 02.
- [x] The new test passes on today's code. No production code changes, and no file under
      `nodes/` changes.
- [x] The Debug and Release suites pass.

## Comments

**2026-11-24 — implemented.** Branch `claude/audio-subtype-01-8e2573`, cut from a freshly
fetched `origin/main` at `dcc9748`. Checkpoint `0afbe75` adds the fixture, the
`.gitattributes` entry, the README provenance note and the test. The existing
`MODULAR_TEST_FIXTURES` define for `authoring_tests` already locates the fixtures, so
`CMakeLists.txt` is unchanged.

- *The fixture.* `tests/fixtures/graph-version-1.modular` is 1,070 bytes, SHA-256
  `d196995085a3fdabc564a1efea9c2faa95e8e6ca5ae567b29e8addbd343ebab6`. A temporary doctest
  case built it and wrote it with `saveGraphFile` and `primitiveCodecs()`. It was compiled
  against `dcc9748`, where only the test file differed, and was never committed. Its graph:

  | Node | Kind | Inputs | Outputs |
  | --- | --- | --- | --- |
  | 2 | manual trigger (start) | — | 1 `trigger` event 1 |
  | 6 | MIDI block, two notes | 3 `start`, 4 `stop` event 1 | 5 `midi` event 2 |
  | 9 | sine synth | 7 `midi` event 2 | 8 `out` audio 1 |
  | 11 | constant 0.5 | — | 10 `value` value 3 |
  | 15 | gain, 1 channel | 12 `in` audio 1, 13 `gain` value 3 | 14 `out` audio 1 |
  | 19 | gain, 2 channels | 16 `in` audio 2, 17 `gain` value 3 | 18 `out` audio 2 |
  | 22 | mixer, 1 input, 2 channels | 20 `in0` audio 2 | 21 `out` audio 2 |

  The wires are 2:1→6:3 (trigger event), 6:5→9:7 (MIDI event), 9:8→15:12 (mono audio),
  11:10→15:13 (value) and 19:18→22:20 (stereo audio). A constant (port 23, node 24) was added
  and deleted last, so `NEXT` is 25, above every surviving id. Layout positions were set so
  the file looks like an authored graph; the test does not check them. The stored bytes are
  two MIDI note messages, and there are no paths or names.
- *Byte-for-byte resave, the one-off check.* The same temporary case then opened the file
  with `openGraphFile` and saved it again with `saveGraph`. The output equalled the file's
  bytes: doctest printed `byte-identical resave: true size 1070`. The case was then
  removed. After ticket 02 this check cannot be repeated, because Save will write version 2.

  ```cpp
  TEST_CASE("TEMP generate the version 1 fixture") {
    Workspace workspace;
    auto& graph = workspace.graph();
    const auto start = graph.addNode<ManualTriggerNode>();
    const auto block = graph.addNode<MidiBlockNode>(std::vector<MidiBlockNode::Entry>{
        {0, {0x90, 60, 100}}, {480, {0x80, 60, 0}}});
    const auto synth = graph.addNode<SineSynthNode>();
    const auto level = graph.addNode<ConstFloatNode>(0.5f);
    const auto mono = graph.addNode<GainNode>(1);
    const auto stereo = graph.addNode<GainNode>(2);
    const auto mix = graph.addNode<MixNode>(1, 2);
    auto& trigger = static_cast<ManualTriggerNode&>(graph.node(start));
    auto& midi = static_cast<MidiBlockNode&>(graph.node(block));
    auto& sine = static_cast<SineSynthNode&>(graph.node(synth));
    auto& constant = static_cast<ConstFloatNode&>(graph.node(level));
    auto& monoGain = static_cast<GainNode&>(graph.node(mono));
    auto& stereoGain = static_cast<GainNode&>(graph.node(stereo));
    auto& mixer = static_cast<MixNode&>(graph.node(mix));
    graph.connect(start, trigger.trigger(), block, midi.trigger());
    graph.connect(block, midi.midi(), synth, sine.midiIn());
    graph.connect(synth, sine.out(), mono, monoGain.in());
    graph.connect(level, constant.value(), mono, monoGain.gain());
    graph.connect(stereo, stereoGain.out(), mix, mixer.in(0));
    graph.removeNode(graph.addNode<ConstFloatNode>(0.0f));
    double x = 40;
    for (const auto id : {start, block, synth, level, mono, stereo, mix}) {
      workspace.setPosition(id, {x, id % 2 ? 60.0 : 180.0});
      x += 160;
    }
    const auto path = std::filesystem::path(MODULAR_TEST_FIXTURES) / "graph-version-1.modular";
    saveGraphFile(workspace, path, primitiveCodecs());
    auto reopened = openGraphFile(path, primitiveCodecs());
    std::stringstream resaved;
    saveGraph(reopened, resaved, primitiveCodecs());
    std::ifstream file(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    CHECK(resaved.str() == bytes);
    MESSAGE("byte-identical resave: ", resaved.str() == bytes, " size ", bytes.size());
  }
  ```

- *Line endings.* `.gitattributes` adds `tests/fixtures/*.modular -text`. On this
  `core.autocrlf=true` checkout, `git ls-files --eol` shows the fixture as
  `i/lf w/lf attr/-text`, while the README beside it is `i/lf w/crlf`. The committed blob
  hashes to the SHA-256 above.
- *The test, red then green.* "a graph saved by the version 1 writer opens with its
  identities port types and wires" (`tests/test_authoring.cpp`):
  - It first reads the header and requires `MODULAR_GRAPH 1`, so a replaced fixture cannot
    pass silently.
  - It then opens the fixture through `openGraphFile`.
  - It checks against literals: `nextIdentity()` 25; the node ids; each port's id, name and
    type written as `PortType::audio(N)`, `value(3)` or `event(1|2)`; and the five wires in
    saved order.

  Before the fixture existed, the test failed: the header `REQUIRE` read an empty line.
  With the fixture, it passes with 63 assertions. Two temporary tamperings of the fixture
  each fail it; each time the file was restored from a saved copy, to the same SHA-256:
  - `PORT 16 0 2` → `PORT 16 0 1`: Open throws `restore: port declaration does not match
    node`;
  - `NEXT 25` → `NEXT 26`: the next-identity check fails.
- *Scope.* `git diff --stat origin/main...HEAD` lists only `.gitattributes`,
  `tests/fixtures/README.md`, `tests/fixtures/graph-version-1.modular` and
  `tests/test_authoring.cpp`, plus this ticket. There is no production code and no path
  under `nodes/`.
- *Checks on `0afbe75`.*
  - `scripts/build_windows.ps1` (Debug, all targets): 8/8 CTest entries passed, including
    `graph_editor_app`, `graph_editor_gesture` and `kernel_purity`.
  - `-Configuration Release`: 8/8 passed.
  - `authoring_tests` ran 22 cases, 496 assertions, in both configurations.
  - Neither build log has a compiler warning.
  - `python scripts/check_docs.py` (0 problems) and `git diff --check` are clean.

**2026-11-24 — review.** `/code-review` ran over `git diff origin/main...HEAD` (`0afbe75`
against `dcc9748`), with the Standards and Spec axes as separate agents. The fixes landed in
the next checkpoint, with this record.

- *Standards: no hard violations.* The test name is an invariant sentence. The test reaches
  the fixture through `MODULAR_TEST_FIXTURES` and observes only public seams. The commit
  holds no local paths, and its author is the noreply identity. Judgement calls taken:
  - The fixture path line was 93 columns and now wraps, per `docs/project-foundations.md`'s
    "approximately 90-column lines". The test title stays longer, as many existing titles do.
  - `std::get<0..2>` on the expectation rows became the structured binding
    `[port, name, type]`.
  - The header pre-check closes its stream in its own scope before Open reads the file.
  - The README entry now separates what the file contains from how it was made, as the
    `one-note.mid` entry does.

  Not taken:
  - The port comparison loop repeats the shape of the one in "shared save and open preserve
    authored identities and later minting". The reviewer advised waiting for a third copy
    before extracting a helper. Ticket 02's version-2 resave test may add that copy.
  - `docs/windows.md` says `.gitattributes` keeps shell scripts on LF and does not mention
    the new `-text` rule. The sentence is still true, and the file is outside this ticket's
    scope, so it is left for a later documentation change.
  - Numeric subtypes stay, rather than `subtype::Trigger` and its siblings. The reviewer
    agreed: the file pins numbers, not names.
- *Spec: nothing missing, no scope creep, nothing implemented wrongly.*
  - Every checklist item this diff can satisfy is met.
  - Every fixture port matches today's node declarations.
  - None of the kinds is behind the `_WIN32` guard.
  - Every assertion keeps its meaning under the version-1 reading rule once `channels`
    lands.

  The reviewer judged two extras in scope: the header `REQUIRE`, and the `*.modular`
  pattern rather than a single path. Taken: the README names the full commit hash instead
  of `dcc9748`.

  Noted for ticket 02: Seam 2's "Saving the reopened graph writes version 2. Opening that
  output reproduces identical ids, names, types and wires" belongs there. It must be a new
  test, since this one may not be edited.

  Limit: the reviewer could not build. Its view of the resave check, the red and green runs
  and the suites rests on this record.

**2026-11-24 — checks after the review fixes.** The full Debug and Release suites
(`scripts/build_windows.ps1`, all targets, desktop entries included) each passed 8/8.
`authoring_tests` ran 22 cases and 496 assertions; the fixture case alone ran 63. Neither
build log has a compiler warning. `python scripts/check_docs.py` and `git diff --check` are
clean, and the diff still touches no production code and nothing under `nodes/`.
