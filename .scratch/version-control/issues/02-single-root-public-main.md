# 02: Single-root public main

**What to build:** Publish the explicitly approved working snapshot as the only initial root commit on a public GitHub `main`, without exposing the rough legacy ancestry or losing unreviewed work.

**Blocked by:** 01: Audited, green baseline candidate.

**Status:** ready-for-agent

- [x] Require the user's approval of the exact ticket-01 candidate tree and any proposed sanitization or exclusion before replacing history. If the tree has changed since approval, re-audit the difference and obtain renewed approval.
- [x] Create the new root commit from only the approved snapshot, with no previous commit as ancestor; verify both its ancestry and selected file tree locally.
- [x] Arrange a public GitHub repository only after the content audit and approved root; obtain user sign-in or repository details only when needed. Show the exact publication target before the outward-facing push.
- [x] Verify the published `main` points to the approved root with the expected tree and no old commits in its ancestry. Remove old local branch names only after that verification and the user's approval; do not deliberately garbage-collect old Git objects.
- [x] Report the resulting commit, published target, verification evidence, excluded material and any remaining cleanup accurately. Do not activate standing ticket-commit permission merely by completing the bootstrap.

The initial-root operation is a one-time exception to the future PR-only integration rule, not a precedent for routine direct pushes.

## Comments

### Execution (2026-09-29; recorded 2026-11-02)

This work was done on 2026-09-29 but not recorded at the time. The account below
comes from that session's transcript and the current Git state.

- **Approval.** The maintainer approved the ticket-01 snapshot. Recording that
  approval changed ticket 01, so the revised 124-path tree was fingerprinted
  again: `0ab43e433c5feb9f29fd8e63a3e08daacc508a1a37049ddde5a5ec7f82927722`.
  This is the SHA-256 of sorted `path + NUL + SHA256(file bytes) + LF` records.
  The maintainer approved that revised tree at 16:03 UTC and asked to proceed
  with ticket 02 without further approval prompts. That approval did not name
  a publication target; see Publication below. No sanitization or exclusion was
  added beyond the [candidate manifest](../candidate-tree.md).
- **Root commit.** A temporary index built from the approved working bytes
  produced `65e60dcbfd0ba87354d2329a56cd6cf57f79741d`, "Establish approved
  public root", with tree `262cf8387d297c4e4772217c83eff651c8858967`. Local
  `main` was moved to it from `8d29051` ("Close sprint 2: grouping the
  milestone graph changes not one sample"), a commit in the legacy ancestry.
  The earlier parentless candidate `public-root-prep` (`f93ece3`) was not
  reused because its ticket 01 and manifest differ from the approved bytes.
- **Publication.** The target shown before the push was a new public
  repository, https://github.com/huangxuyan1/Modular, on the authenticated
  `huangxuyan1` account. The agent's permissions blocked repository creation,
  so the maintainer created the empty public repository at that target. The
  push was blocked in auto mode and rerun after the maintainer switched the
  session to manual permission mode. Only `refs/heads/main` was pushed, at
  20:22 UTC. The `origin` remote was then added.

### Verification (2026-11-02)

- `git rev-list --parents main` shows one commit with no parent. It has no
  merge base with the legacy tip `4163dc8` or the earlier candidate `f93ece3`.
  `git ls-tree -r main` lists 124 regular-file blobs, exactly the manifest's
  path list.
- `git ls-remote origin` shows only `HEAD` and `refs/heads/main`, both at
  `65e60dc`. The GitHub API reports the following: the repository is public;
  `main` is its default and only branch; commit `65e60dc` has no parents and
  tree `262cf83`, which holds 124 blobs. It also reports no tags, workflows or
  pull requests. `main` is not protected yet; ticket 03 covers that.
- Excluded material is absent from the published tree, as the manifest lists:
  `artifacts/midi-repair/`, `build/`, `.cache/`, generated WAV files,
  `compile_commands.json`, `.vscode/` and `.claude/settings.local.json`.
- Before this record update, the working tree held the approved bytes: the
  fingerprint above recomputed to the same value. Under this checkout's Git
  configuration, every file matched its `main` blob. One file differs
  byte-for-byte: two lines of `tests/WindowsPlaybackProbe.h` end in CRLF on
  disk. Git normalized them to LF in the commit because the system
  configuration sets `core.autocrlf=true`. The published content is otherwise
  identical, and `git status` does not report the line-ending difference.
- The checkout moved from `claim/event-payload-32-bytes` onto `main` without
  touching working files, using `git symbolic-ref HEAD refs/heads/main` and
  then a mixed `git reset`. Nothing was staged beforehand. `git status` then
  reported a clean tree with no untracked files.
- `AGENTS.md` and `CLAUDE.md` are byte-identical (6,492 bytes) and still say
  to commit only when the user asks. Standing ticket-commit authority is not
  active.
- Builds and tests were not rerun for the publication or for this record,
  because no source changed. The baseline evidence remains ticket 01's
  2026-09-28 runs, which passed 8/8 in Debug and in Release.

### Branch cleanup (2026-11-02)

After the verification above, and with the maintainer's approval,
`git branch -D` removed `claim/event-payload-32-bytes` (was `4163dc8`, the
11-commit legacy ancestry) and `public-root-prep` (was `f93ece3`). The only local branch is now
`main`. No branch references the old commits any more. `4163dc8` and the old
`main` tip `8d29051` remain in local reflogs. Nothing was garbage-collected, and
old objects are left to Git's ordinary retention. Five local `refs/codex/...`
checkpoint refs from another tool point at tree snapshots, not commits. They do
not retain the old commit history and were left alone.

### Landing (2026-11-15)

With the maintainer's approval, this record (tickets 01 and 02 and the
manifest's status line) was pushed directly to `main` as the final bootstrap
step. Every later change reaches `main` through a pull request. Local `main`
was then set to track `origin/main`.
