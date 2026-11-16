# 01: Audited, green baseline candidate

**What to build:** Give the maintainer an exact, reviewable candidate for the public initial commit, containing all appropriate current source and project records and backed by fresh verification. Stop before changing Git history or publishing it.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [x] Inventory tracked, untracked and relevant ignored files, including pending work from earlier milestones; account for every candidate rather than relying on the current branch diff alone.
- [x] Review the exact proposed public file list and inspect included content for private inputs, local paths, credentials, generated files, fixtures, third-party provenance and reproducible investigation evidence. Propose explicit sanitization or exclusion for unsuitable material without silently dropping work.
- [x] Verify that both root agent-instruction files have identical content and that the proposed public records accurately describe current policy, not a workflow that is not yet active.
- [x] Run clean native Windows Debug and Release builds and the complete available tests, including kernel purity; record exact results and a separate actual-GUI smoke result where a desktop is available. Diagnose failures before presenting the baseline as working.
- [x] Present the exact candidate tree, exclusions or sanitizations, verification evidence and unresolved publication concerns to the user for approval. Do not stage a root commit, rewrite history, create a GitHub repository or push as part of this ticket. Await maintainer review.

This ticket does not itself authorize changes to the current branch history or publication. A failing check or unresolved public-content concern blocks ticket 02 until corrected and the candidate is accepted.

## Audit checkpoint (2026-09-25; not an approved public tree)

The live candidate inventory is `git ls-files --cached --others --exclude-standard`:
59 tracked working-tree files and 63 nonignored untracked files. This includes the
previously uncommitted graph editor, workspace, nodes, tests and project records;
a HEAD-only diff is not the candidate. The 1,496 ignored files are not proposed
for publication: principally `build/` outputs and `.cache/` private investigation
material, plus machine-local settings and generated media. An exact post-sanitization
manifest and the maintainer's approval are still required before ticket 02.

Public-content holds:

- `artifacts/midi-repair/test-repaired.mid` derives almost entirely from a MIDI
  file in a personal Downloads directory. Do not publish without verified
  redistribution rights; otherwise exclude the binary and redact the absolute
  source path in `artifacts/midi-repair/README.md`. Historical build claims there
  are not fresh verification evidence.
- Absolute machine-local skill paths occur in both paired root instruction files,
  `docs/agents/matt-skills-catalog.md`, `docs/agents/matt-pocock-workflow.md`,
  and graph-editor review notes. Make public references portable without changing
  the historical meaning of reviews; their ignored comparison snapshots are not
  part of this candidate. Review `docs/windows.md` for machine-local examples.
- Confirm attribution and license provenance for vendored doctest 2.4.12 and
  third-party quotations in roadmap research. Confirm the 35-byte MIDI test
  fixture's origin or replace it with demonstrably authored bytes.
- `.claude/settings.local.json` is excluded only by the current user's global
  Git excludes, not `.gitignore`; make that exclusion reproducible before sharing.
- The content scan found no credible credentials in the candidate set, but was
  bounded, not proof that no secret exists. Existing review records and old
  pass counts are historical, not a fresh green-baseline assertion.

`AGENTS.md` and `CLAUDE.md` were byte-compared and are identical (90 lines each);
scoped ticket checkpoint-commit authority is not yet active. `git diff --check`
reported no whitespace errors. The normal Windows build script passed all seven
CTest entries in both Debug and Release after two test-harness adjustments in
`tests/test_graph_editor_app.cpp`: startup now waits for the summary control, and
recorder triggers wait for visible worker state instead of a fixed 30 ms sleep.
Separate Debug and Release actual-app gestures/capture probes also passed. Fresh
isolated CMake builds and all seven tests passed in both configurations, although
MSBuild warned that the isolated build directory is under a temporary directory.

**Not stable yet:** Before the adjustments the GUI check failed intermittently
with a missing summary control, an unmoved node and a recorder still recording.
After the adjustments, an eight-run GUI stress check passed, but a later ten-run
check failed on run seven: expected selected node position `(68, 100)` after a
Run-mode drag; the visible inspector instead showed palette creation settings.
A later focused reproduction established that a `WM_MOUSEMOVE` with no pressed
button during a captured move displaced the Gain node from `(408, 50)` to
`(-291, 634)`, matching the intermittent full-test position. The editor now cancels
an active left/middle drag if a move message lacks its corresponding button flag.
A focused `graph_editor_gesture` regression failed before that fix and passed
afterward; it also verifies a subsequent normal move. The full GUI test passed
three short consecutive runs after the fix. The final Debug and Release Windows
build-script runs each passed all eight CTest entries (including kernel purity and
the new regression). This supports the fix, but does not establish that no other
GUI timing issue remains. Public-content holds above still block baseline approval.
No history change, staging, publication or merge has been performed for this ticket.

## Candidate prepared (2026-09-26; awaiting maintainer approval)

[Proposed public root tree](../candidate-tree.md) is the exact 124-path working-tree
candidate (59 tracked files, 65 nonignored untracked files including the manifest),
not a HEAD-only diff. The previously noted 63 untracked paths became 65: four new
files (doctest license and provenance note, fixture provenance note, candidate
manifest) minus the two now-excluded private MIDI investigation files.
The proposed tree preserves the uncommitted GUI, workspace, nodes, tests and planning
work. Ignored output/private inputs remain local (1,518 ignored files at the latest
inventory; not a proposed public file list).

The private MIDI investigation remains on disk, but both its repaired MIDI and
its analysis README are excluded by `.gitignore` because redistribution rights
are unknown. The minimal test fixture is retained without claiming newly
established authorship; its bytes and limitation are documented. Regeneration
of the untracked fixture was blocked by the current permission mode, so no
replacement or newly created provenance is claimed.
Paths in both root instructions, skill documentation and historical GUI reviews
are portable. Skill references point to upstream source locations, whose 37 catalog
and 12 workflow paths were checked for existence; the installed historical version
may differ. Doctest v2.4.12 matches its tagged upstream header byte-for-byte and
now carries the tagged MIT `LICENSE.txt`. Roadmap research's extended external
passages were paraphrased with source tags. The 35-byte one-note fixture matches
the local test builder's minimal sequence, but its exact prior authorship is not
independently established; that limitation is explicit in the manifest. The
project-local ignore now covers `.claude/settings.local.json`. A bounded scan of
all proposed text found no user-home paths or obvious key/token markers.

A fresh isolated MSVC Windows configure and build on September 26 succeeded in
both Debug and Release; CTest passed **8/8 in each**, including the actual editor
app, focused gesture regression and kernel purity. A separate Debug real-window
`--gesture` invocation passed. The temp-directory MSBuild warning MSB8029 was
nonfatal. Hardware audio and human visual inspection were not repeated for this
candidate. After the final documentation updates, `git diff --check`, paired
instruction byte parity (6,492 bytes each), and relative Markdown link checks
passed. These results do not prove that a former GUI flake cannot recur.

The maintainer delegated the minor publication choices on 2026-09-27; the
conservative choices are recorded in the candidate tree. Ticket 02's separate
history replacement and publication gates remain in force. No root commit,
staging, history rewrite,
repository creation, push or merge has been done.

## Review refresh (2026-09-28; awaiting maintainer approval)

The [candidate manifest](../candidate-tree.md) was checked against the live
tracked-plus-nonignored-untracked inventory: **124/124 paths match** (59 tracked,
65 untracked), with no missing or newly proposed paths. The 1,519 ignored paths
are not included. All current working-tree changes remain unstaged on
`claim/event-payload-32-bytes`; there is no configured Git remote. The manifest
now records the refreshed exclusions, content audit and verification limitations.

Fresh isolated native Windows Debug and Release builds succeeded. The complete
CTest suite passed **8/8 in each** configuration, including both real-window
editor checks and kernel purity. An additional Debug editor `--gesture` probe
passed. MSBuild warning MSB8029 concerns incremental builds from the temporary
build directory, not these fresh build outcomes. No hardware audio or human
visual inspection was performed. A bounded content scan found no candidate
user-home paths, obvious credential patterns or email addresses; relative
Markdown links and `git diff --check` passed, and the two instruction files
remain byte-identical. The minimal MIDI fixture matches the test helper's
bytes; the original authorship limitation remains. No new legal determination
about third-party excerpts or fresh upstream retrieval was made.

**Approval received 2026-09-29:** The maintainer explicitly approved the exact
snapshot for the root commit after reviewing the candidate manifest. Immediately
before this ticket update, the live tracked-plus-nonignored-untracked set matched
all 124 proposed paths; the SHA-256 of sorted records
`path + NUL + SHA256(file bytes as lowercase hex) + LF` was
`ca8e6652e0c5104bc55b45f00f197e7478f0928e833eb821ed83389cdc284f11`.
This records the approval that was previously not saved. Marking this ticket
complete changes its bytes, so ticket 02 must re-audit this difference and obtain
renewed approval for the *revised* exact root tree before staging a root commit.
This approval does not authorize publication to a particular GitHub target or
removal of old local branches. No staging, commit, history replacement, repository
creation or push was performed in ticket 01.

## Comments

**2026-11-02: complete; its approval was consumed by ticket 02.** The renewed
approval required above was given on 2026-09-29. After this ticket recorded the
first approval, the revised tree was fingerprinted again in the same record
format. The maintainer approved that revised tree, and it became the public root
commit. [Ticket 02](02-single-root-public-main.md) records the new fingerprint,
the publication and its evidence. The `ca8e6652...` fingerprint above predates this ticket's approval
edit. It could not be reproduced on 2026-11-02 from `main`, `public-root-prep`
or the working tree, and no current tree is expected to match it.
