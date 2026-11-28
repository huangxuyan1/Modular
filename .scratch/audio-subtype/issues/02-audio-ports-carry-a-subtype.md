# 02: Audio ports carry a subtype

**What to build:** the audio subtype claim, end to end. An 80-band spectral stream and an
80-channel speaker bus cannot be connected to each other by mistake, and no file under
`nodes/` changes to make that true.

**Graph author.** Connecting an output declared `audio(80, kMel)` to an `audio(80)` speaker
bus input is refused at connect time, with the same type-mismatch error any other mismatched
wire gets. Every existing graph connects exactly as before.

**Node author.** Meaning is added by one optional argument to the audio port declaration
already in use. The subtype defaults to 0, so no existing declaration changes. An audio port
with zero channels is now an authoring error.

**User.** Every graph saved so far still opens, with every port's channel count intact. The
next ordinary Save writes container version 2, which stores an audio port's subtype and
channel count separately. The editor keeps labelling audio ports `audio<N>`.

ADR-0003 and its persistence clarification settle the design, and this feature's spec
records every decision and the test seams. Two user decisions from 2026-11-23 apply here:
- **Zero channels:** an audio port with 0 channels throws, in every build. This replaces the
  ADR's word "assert".
- **Screenshot:** the deviation in the screenshot criterion below.

**Blocked by:** 01 (Pin a real version-1 save file as a fixture).

**Status:** done

**Kernel**

- [x] A port type has a domain, an opaque subtype and an audio-only channel count. Equality,
      and therefore connect, compares all three.
- [x] The audio port factory keeps its call shape and takes an optional subtype that
      defaults to 0. Value and event ports have channel count 0.
- [x] Audio subtype 0 is a concrete "unspecified audio" type, not a wildcard, on outputs and
      inputs alike.
- [x] The Engine sizes every audio buffer from the channel count and never reads the subtype.
      That covers the silence width, output allocation and input channel counts.
- [x] Declaring an audio port with 0 channels throws `invalid_argument` in every build
      configuration, and so does reconfiguring one to 0 channels. Boundary nodes and
      reconstruction during Open are covered.
- [x] Kernel tests, with `kMel` defined only in the test:
      - `audio(80, kMel)` → `audio(80)` throws;
      - `audio(80, kMel)` → `audio(80, kMel)` and `audio(80)` → `audio(80)` both connect;
      - an equal subtype with a different width still throws;
      - a boundary node exposing `audio(80, kMel)` gives the supernode's face that full type,
        and that face refuses an `audio(80)` input;
      - a subtype change on a wired port is refused, and leaves every port type unchanged.
- [x] The existing mono, stereo and mixer render tests pass unchanged.

**Graph persistence**

- [x] Save always writes `MODULAR_GRAPH 2`.
      - Its port records carry the subtype and channel count as separate numbers; value and
        event ports write channel count 0.
      - No other record changes, and every existing validation rule applies to both versions.
- [x] Open reads versions 1 and 2. A version-1 audio port's number becomes its channel count
      with subtype 0, and a value or event port's number stays its subtype. Any other version
      fails as unsupported.
- [x] Ticket 01's fixture test passes with neither the test nor the fixture edited. The
      reopened fixture saves as version 2 and reopens with identical ids, names, types and
      wires.
- [x] A test-only node and codec with an `audio(80, kMel)` output round-trip through Save and
      Open with the subtype intact. The reopened port still refuses an `audio(80)` input.
- [x] A version-2 file with a zero-channel audio port fails to open and leaves the caller's
      graph unchanged.
- [x] The only edit to an existing test: the corrupt-input test's unsupported-version case
      moves from version 2 to version 3.
      **Second edit, accepted by the user on 2026-11-28:** a second existing test needed
      an edit. "stored MIDI file times keep the digits that recover each double as earlier
      saves wrote them" was added by PR 29 on 2026-11-24, after this ticket. It compared
      Save's whole output with literal version-1 bytes, so any version-2 writer fails it. It
      now expects version 2 output with the same CONTENT; see the comments.
- [x] Node codecs keep their kind versions and encodings.
- [x] Opening a version-1 file never rewrites it; it changes only through the user's next
      Save.

**Editor**

- [x] The editor's port label reads the channel count and still shows `audio<N>`. It does not
      display a non-zero subtype.
- [x] The screenshot from the `desktop` editor test shows non-zero `audio<N>` labels. The
      test keeps its existing mono graph, so the labels read `audio<1>`.
      **Deviation from the spec, approved by the user on 2026-11-23:**
      - the spec asked for a stereo node showing `audio<2>`;
      - any non-zero width shows that the label reads the channel count, not `audio<0>`;
      - a stereo graph would move the test's click coordinates.

**Documentation**

- [x] The graph persistence documentation:
      - describes container version 2;
      - explains how version 1 is read;
      - states the versioning precedent: a format change bumps the container version, every
        older version keeps opening, and each version has a fixture written by its own Save;
      - drops its note that the third port-type field is not yet in the codebase.
- [x] The graph editor documentation no longer says audio has no separate subtype field.

**Close**

- [x] `git diff --stat` against the base shows no file under `nodes/`.
- [x] The kernel purity check passes.
- [x] The Debug and Release suites pass, including both `desktop`-labelled editor tests.
- [x] The claim closes with a commit whose subject is the claim, and the roadmap records the
      claim as proven, as the identity claim's close did.
      The PR's title is the claim, and the squash merge makes it the commit's subject, as
      `0d9396a` did; that merge is the user's. The user accepted the second test edit above,
      so the close stands.

## Comments

**2026-11-28 — implemented.** Branch `claude/audio-subtype-02-24b585`, cut from a freshly
fetched `origin/main` at `0e180dc`, which had not moved when the review ran. Checkpoint
`b1b741e` holds the implementation, tests and documentation; `38128dd` the roadmap close,
the `AGENTS.md` roadmap line and this ticket; `c191ab8` the review fixes.

- *Kernel, red then green.*
  - The demo, "an 80-band spectral stream cannot connect to an 80-channel speaker bus", first
    failed to compile: `PortType::audio` took one argument.
  - With only the port type changed, the kernel suite crashed in the first render test
    ("event timing is sample-accurate…", SIGSEGV): the Engine still read `sub`, now 0. So
    the existing render tests guard all three Engine sites, as the spec said. Switching the
    silence width, output allocation and input channel count to `channels` made the suite
    green.
  - "an audio port with no channels throws when declared or reconfigured" failed with ten
    assertions (nothing threw), then passed once `Node::addPort` and `Node::setPortTypes`
    check the width. It covers primitive declaration, both boundary directions and an
    unwired reconfiguration that publishes none of its changes.
  - Four more cases passed on their first run, because the mechanism already existed: an
    equal subtype at a different width, per-domain subtype ids, a supernode face carrying
    `audio(80, kMel)` both ways, and a refused subtype change on a wired port.
  - *Falsification.* With `sub` dropped from equality, four cases fail: the demo, the face,
    the retype case and "connect enforces exact type equality". With `channels` dropped,
    three fail: the width case, "connect enforces exact type equality" and "connections to a
    supernode type-check like connections to any node". The real equality was restored from
    a saved copy.
- *Persistence, red then green.*
  - With the kernel change alone, 12 of 29 authoring cases failed with `restore: port
    declaration does not match node`, ticket 01's fixture test among them.
  - The three new cases failed next: no `MODULAR_GRAPH 2` header, `open: unsupported graph
    version`, and the fixture mismatch.
  - After the reader and writer changed, 31 of 32 passed. The one failure is the open item
    above: the MIDI-times test from PR 29 compared Save's whole output with literal
    version-1 bytes. Its edit keeps the earlier version-1 text as the file it opens, checks
    the same times, and expects Save to write that graph as version 2 with the same CONTENT,
    written out literally. Any Save that writes a new container version fails the old form.
  - New cases:
    - "a saved spectral port reopens with its subtype and refuses a speaker bus": the saved
      text holds the literal `PORT <id> 0 7 80 "spectrum"`, the reopened type equals
      `audio(80, kMel)`, and `CaptureSinkNode(80)` refuses it.
    - "zero-width or mistyped saved ports fail to open and keep the graph": in version 2,
      zero channels against a declaration, zero channels declared as saved (the kernel's
      rule during reconstruction), a lost and a gained subtype, a channel count on a value
      port, a version-1 record and a negative number; in version 1, a zero against the
      gain's declaration and a zero that reaches the kernel's rule. Each leaves the
      caller's graph as it was.
    - "a reopened version 1 graph saves as version 2 and reopens unchanged": the literal
      records `PORT 16 0 0 2`, `PORT 17 1 3 0` and `PORT 5 2 2 0`; identical ids, names,
      types and wires on reopen; an identical second save; and fixture bytes unchanged by
      Open.
- *Scope.* `git diff --stat origin/main...HEAD` lists no path under `nodes/`. Ticket 01's
  fixture and test are byte-unchanged. The removed lines in existing tests are the version
  case and the MIDI-times comparison.
- *Editor screenshot.* `build/windows/graph-editor-Debug.bmp` shows `audio<1>` on the gain's
  `in`/`out` and the mixer's `in0`–`in2`/`out`. With the label temporarily reading `sub`
  again, the same capture shows `audio<0>` on every one of them; the test itself still
  passes, so the image is the check.
- *Checks before the review.*
  - Debug, all targets: 12/12 CTest entries passed, `graph_editor_app`,
    `graph_editor_gesture` and `kernel_purity` included, with no compiler warning.
  - Release: the first run failed `soundfont_extract` alone. Its render comparison had
    already passed bit-identical; the later step compares the commit the extract tool
    records with `git rev-parse HEAD`, and checkpoint `38128dd` was committed while the test
    ran. It passed alone and in eight repeats, and the full Release rerun passed 12/12 with
    no commit during it.
  - The `web` preset under emsdk 6.0.13: 9/9, `web_core_tests` and `web_smoke` included.
  - The Release capture also shows `audio<1>` on every gain and mixer audio port.

**2026-11-28 — review.** `/code-review` ran over `git diff origin/main...HEAD` (`b1b741e`,
`38128dd` against `0e180dc`), with the Standards and Spec axes as separate agents. The
fixes are checkpoint `c191ab8`.

- *Standards: no hard violations.* One minor breach of "comments state invariants": both
  test files said `kMel` was "defined here and nowhere else", though each defines it. Both
  comments now say it is a test-only id. Judgement calls taken:
  - `AudioPorts` became `AudioStage`, since it is a node; `withSpectrum` became
    `codecsWithSpectrum`; `requireChannels` became `requireAudioChannels`, since it passes
    value and event types.
  - `readPorts` assigns with if/else instead of through a conditional expression.

  Not taken:
  - The resave test adds the third copy of the graph-against-graph port loop that ticket
    01's review set as the trigger for a helper. Extracting it would edit "shared save and
    open preserve authored identities and later minting", beyond this ticket's allowed
    edits. **Follow-up:** extract a shared port and wire comparison for the authoring tests
    in a later change.
  - The MIDI-times test's `written` literal re-spells the container that `midiNodeGraph()`
    builds. A version parameter on the helper would touch the other tests that use it.
  - `PortType::audio(channels, subtype)` takes two adjacent `uint32` values, which can be
    swapped silently. ADR-0003 fixes that shape and the foundations make subtypes plain
    `uint32` ids.
- *Spec: nothing missing in the code, no scope creep.* The reviewer confirmed: no path
  under `nodes/`; ticket 01's test and fixture byte-unchanged; the three Engine sites read
  `channels`; the zero-channel rule in `addPort` and `setPortTypes`; the version-1 reading
  rule; the `PORT` token order; every documentation bullet. It judged the extra corrupt
  cases (user story 26) and the `AGENTS.md` line (the identity close's precedent) in scope.
  Findings taken:
  - A zero-width port in a version 1 file had no test, though the spec says Open fails "in
    either version". The corrupt-port test now covers version 1 too.
  - This record was missing: the open item pointed at comments that did not exist yet.
  - The claim-subject item is met by the PR title at squash merge, not by a branch commit;
    the item now says so.

  Not taken, for the user: the roadmap close records the claim as proven while the
  "only edit" item is open. The reviewer suggested holding the close until the user rules.
  It stays in, because nothing merges before the user does: if the second edit is ruled to
  falsify the claim, the Proven row, the close paragraphs and the `AGENTS.md` line come out
  before merge. The reviewer also judged the MIDI-times edit faithful, and found no smaller
  edit that a version-2 Save allows.

  Nit left: the roadmap's ticket 05 entry still says it "does not displace the next claim
  above", which is now empty. That sentence records ticket 05's ruling as it was made.

  Limit: neither reviewer could build; their view of the suites rests on this record.

**2026-11-28 — checks after the review fixes.** On `c191ab8`, the full Debug and Release
suites (`scripts/build_windows.ps1`, all targets, desktop entries included) each passed
12/12, with no compiler warning in either build log. `kernel_tests` ran 50 cases and
15,045 assertions, `authoring_tests` 32 cases and 749. `python scripts/check_docs.py`,
`git diff --check`, `scripts/check_line_length.py` and `scripts/check_public_content.py`
are clean over `origin/main...HEAD`, and `git diff --stat` still lists no path under
`nodes/`.

**2026-11-28 — ruling.** The planning hub relayed the user's answer to "Accept the second test
edit on PR 45?": "Yes, merge when green". The edit to "stored MIDI file times keep the digits
that recover each double as earlier saves wrote them" is accepted, and the roadmap close, its
Proven row and the `AGENTS.md` status line stay as they are. Every checklist item is now ticked,
so the status is `done`. The merge stays with the planning hub.
