# Proposed public root tree

Status: candidate for maintainer review; **not approved or staged**. Prepared
2026-09-26 and refreshed 2026-09-28 from the current working tree, not from
`HEAD`. This explicit list still contains 124 paths: 59 already tracked and 65
newly proposed. The manifest itself is one of the proposed paths. The exact
bytes to publish are the working-tree versions at approval time; any later edit
requires rechecking this manifest and verification before staging. No root
commit or remote publication is authorized by this document.

## Proposed files

```text
.gitattributes
.gitignore
.scratch/graph-editor/01-review.md
.scratch/graph-editor/02-review.md
.scratch/graph-editor/03-review.md
.scratch/graph-editor/04-review.md
.scratch/graph-editor/05-review.md
.scratch/graph-editor/archive/premature-spec-20260912.md
.scratch/graph-editor/issues/01-author-and-edit-a-flat-graph.md
.scratch/graph-editor/issues/02-play-midi-through-placed-start-stop-nodes.md
.scratch/graph-editor/issues/03-save-and-reopen-an-authored-graph.md
.scratch/graph-editor/issues/04-reopen-midi-graphs-with-owned-content.md
.scratch/graph-editor/issues/05-record-wav-takes-and-reopen-recording-graphs.md
.scratch/graph-editor/planning.md
.scratch/graph-editor/spec.md
.scratch/identity/issues/01-node-identity-and-node-deletion.md
.scratch/identity/issues/02-port-identity-and-port-mutation.md
.scratch/identity/issues/03-supernode-face-by-id.md
.scratch/identity/spec.md
.scratch/roadmap/issues/01-plugin-host-obligations.md
.scratch/roadmap/issues/02-name-the-engine-layer.md
.scratch/roadmap/issues/03-variable-arity-after-graph-add.md
.scratch/roadmap/issues/04-rate-probe-oversampling.md
.scratch/roadmap/issues/05-recognizable-daw-acceptance-test.md
.scratch/roadmap/issues/06-constraint-and-annotation-layer.md
.scratch/roadmap/issues/07-command-api-cut.md
.scratch/roadmap/issues/08-realtime-callback-probe.md
.scratch/roadmap/issues/09-adversary-apps-for-the-gaps.md
.scratch/roadmap/issues/10-prepare-on-recompile.md
.scratch/roadmap/issues/11-event-payload-shape.md
.scratch/roadmap/issues/12-channel-layout-vs-count.md
.scratch/roadmap/issues/13-unconnected-value-default.md
.scratch/roadmap/map.md
.scratch/roadmap/prototypes/11-event-payload/PROTOTYPE_probe.cpp
.scratch/roadmap/prototypes/11-event-payload/PROTOTYPE_today_fails.cpp
.scratch/roadmap/prototypes/11-event-payload/run.sh
.scratch/roadmap/research/01-plugin-host-obligations.md
.scratch/supernode/issues/01-supernode-derived-faces.md
.scratch/supernode/issues/02-engine-flattens-by-resolution.md
.scratch/supernode/issues/03-bit-identical-demo.md
.scratch/supernode/spec.md
.scratch/version-control/candidate-tree.md
.scratch/version-control/issues/01-audited-green-baseline.md
.scratch/version-control/issues/02-single-root-public-main.md
.scratch/version-control/issues/03-enforced-pr-and-ci-gate.md
.scratch/version-control/issues/04-ticket-branch-delivery.md
.scratch/version-control/issues/05-provisional-planning-delivery.md
.scratch/version-control/spec.md
AGENTS.md
CLAUDE.md
CMakeLists.txt
CMakePresets.json
CONTEXT.md
apps/graph_editor/EditorWindow.cpp
apps/graph_editor/EditorWindow.h
apps/graph_editor/PrimitivePalette.cpp
apps/graph_editor/PrimitivePalette.h
apps/graph_editor/main.cpp
apps/midi_cli/main.cpp
apps/render_cli/main.cpp
docs/adr/0001-supernodes-are-flattened-grouping.md
docs/adr/0002-event-payload-is-fixed-inline-32-bytes.md
docs/adr/0003-audio-ports-carry-an-opaque-subtype.md
docs/adr/0004-ports-and-nodes-have-graph-scoped-ids.md
docs/agents/domain.md
docs/agents/issue-tracker.md
docs/agents/matt-pocock-workflow.md
docs/agents/matt-skills-catalog.md
docs/agents/triage-labels.md
docs/agents/version-control-discussion.md
docs/graph-editor.md
docs/graph-persistence.md
docs/node-io.md
docs/project-foundations.md
docs/roadmap.md
docs/windows.md
kernel/include/modular/Buffers.h
kernel/include/modular/Engine.h
kernel/include/modular/Graph.h
kernel/include/modular/Ids.h
kernel/include/modular/Node.h
kernel/include/modular/PortType.h
kernel/include/modular/Supernode.h
kernel/src/Engine.cpp
kernel/src/Flatten.cpp
kernel/src/Flatten.h
kernel/src/Graph.cpp
kernel/src/Node.cpp
kernel/src/Supernode.cpp
nodes/include/modular/nodes/AudioOutputNode.h
nodes/include/modular/nodes/BasicNodes.h
nodes/include/modular/nodes/ManualTriggerNode.h
nodes/include/modular/nodes/MidiFile.h
nodes/include/modular/nodes/MidiFileNode.h
nodes/include/modular/nodes/MidiNodes.h
nodes/include/modular/nodes/Payloads.h
nodes/include/modular/nodes/PrimitivePersistence.h
nodes/include/modular/nodes/WavFile.h
nodes/include/modular/nodes/WavRecorderNode.h
nodes/src/AudioOutputNode.cpp
nodes/src/MidiFile.cpp
nodes/src/PrimitivePersistence.cpp
nodes/src/WavRecorderNode.cpp
scripts/build_windows.ps1
scripts/check_kernel_purity.cmake
scripts/check_kernel_purity.sh
tests/WavTestFiles.h
tests/WindowsPlaybackProbe.h
tests/fixtures/README.md
tests/fixtures/one-note.mid
tests/test_authoring.cpp
tests/test_graph_editor_app.cpp
tests/test_kernel.cpp
tests/test_midi_cli.cmake
tests/test_midi_file.cpp
tests/test_nodes.cpp
third_party/doctest/LICENSE.txt
third_party/doctest/README.md
third_party/doctest/doctest.h
workspace/include/modular/GraphExecution.h
workspace/include/modular/GraphPersistence.h
workspace/include/modular/Workspace.h
workspace/src/GraphFiles.cpp
workspace/src/GraphPersistence.cpp
```

## Deliberate exclusions

- `artifacts/midi-repair/`: private investigation material, including a repaired
  derivative of a musical sequence with unverified redistribution rights and its
  analysis README. Preserved locally but excluded together by `.gitignore`, so
  the public tree does not carry the binary or metadata about the private input.
- `build/`, `.cache/`, generated WAV files, `compile_commands.json`, `.DS_Store`,
  `.vscode/` and local `.claude/settings.local.json`: local outputs or private
  settings. The repo's `.gitignore` now covers the settings and private MIDI,
  rather than relying on this machine's global excludes.
- Ignored comparison snapshots and captures referenced by historical review
  notes are not proposed as reproducible artifacts. Those notes remain historical
  accounts, not fresh verification of this candidate.

## Public-content decisions and limitations

- The vendored `third_party/doctest/doctest.h` matches the tagged v2.4.12
  upstream file byte-for-byte; the corresponding MIT license and attribution
  are now proposed beside it.
- `tests/fixtures/one-note.mid` is a minimal 35-byte format-0 test sequence,
  structurally equivalent to `oneNote()` in `tests/test_midi_file.cpp` and
  containing no MIDI metadata. Its exact pre-audit authorship is not independently
  verifiable. Keep it as a standard-format minimal test sequence with this
  limitation recorded; no claim of newly generated authorship is made. A proposed
  regeneration was not performed because replacing the existing untracked file
  was blocked by the current permission mode.
- Long quoted passages from third-party plugin specifications were paraphrased
  with source tags in the historical roadmap research. This was not a legal
  determination about all short quoted snippets remaining in that note.
- Machine-specific paths in instructions, skill links, review notes and the
  historical GUI reviews were removed or made portable; the private MIDI
  investigation README is excluded with its source file. Upstream public
  skill links were checked for path existence, but they may not represent the
  exact local versions reviewed in 2026.
- A bounded scan found no Windows user-home path, obvious private-key marker
  or credential-token prefix in the proposed text files. This is not a complete
  security or publication-rights guarantee.
- The current rule is still *commit only when asked*; future ticket checkpoint
  authority described in planning files is not active. The two startup files
  were checked byte-identical.

## Verification at preparation

Native Windows fresh CMake configure in an isolated build directory, followed
by Debug and Release builds and full CTest: **8/8 passed in each configuration**
(including `graph_editor_app`, `graph_editor_gesture` and `kernel_purity`). The
isolated build under a temporary directory produced MSBuild warning MSB8029
about possible incremental-build issues; it did not affect fresh builds. A
separate Debug real-window `--gesture` invocation passed. This is an automated
GUI interaction, not a human visual inspection or a hardware audio test. The
full suite was run once per configuration after the source and sanitization
changes; documentation-only edits made afterward do not change built source.
`git diff --check` and the byte comparison of `AGENTS.md` and `CLAUDE.md` passed.
No device playback was repeated for this audit.

## Refresh verification (2026-09-28)

The live `git ls-files --cached --others --exclude-standard` set matches all
124 paths above exactly: 59 tracked and 65 nonignored untracked. Another 1,519
ignored paths remain outside the proposed tree, predominantly `build/` (1,389)
and `.cache/` (120); the rest include local settings, generated audio, the
private MIDI investigation and platform metadata. The current branch is still
`claim/event-payload-32-bytes`, with substantial unstaged changes, and no Git
remote is configured. Nothing here represents a staged snapshot.

A fresh isolated native MSVC build completed for Debug and Release, followed
by all eight CTest entries in each configuration: **8/8 passed in Debug and
8/8 in Release**, including `graph_editor_app`, `graph_editor_gesture`, and
`kernel_purity`. A separate Debug actual-window `--gesture` probe passed.
The build directory was under the session's temporary scratchpad, so MSBuild
warned MSB8029 about potential *incremental* build behavior there; the fresh
builds and tests completed. This was automated real-window interaction, not
human visual inspection or hardware audio verification. GUI timing reliability
beyond these runs is not established.

`AGENTS.md` and `CLAUDE.md` still match byte-for-byte (6,492 bytes each;
SHA-256 `691cda83df57329ef32e2a8444d8c1a047a78c4e4db14fd2e45fb1a4a9ecfbc5`).
All 65 relative Markdown links checked against existing candidate files resolve;
`git diff --check` reports no whitespace errors. A bounded, redacted scan of
all 124 candidate paths found no user-home paths, obvious credential patterns
or email addresses. This is not a complete secret or rights audit. The sole
binary candidate is the 35-byte MIDI fixture, whose bytes match the minimal
sequence built in `tests/test_midi_file.cpp`. The vendored doctest header hash
still matches the prior audit's recorded upstream hash; no fresh upstream
retrieval or independent legal review was performed. The earlier fixture
provenance limitation and third-party quotation caveat still apply.

This refresh updates two of the candidate's Markdown records (this manifest
and ticket 01) after the code build; the source, fixture and build definitions
have not been changed by this refresh. The path inventory, instruction parity,
link and whitespace checks are rechecked after those records are edited. Since
there are no frozen byte hashes for every candidate path and this is a live
working tree, ticket 02 must re-audit any changes before using an approval.

## Approval boundary

The publication choices are made conservatively here: include project planning
records, exclude the private MIDI investigation, and retain the minimal test
fixture with its provenance limitation documented. This is the proposed tree,
not authorization to perform ticket 02's history replacement or public push.
No files have been staged, history changed, repository created or pushed for
this ticket.
