# 03: Enforced PR and CI gate

**What to build:** Make every post-bootstrap change to public `main` pass a reliably reported PR check, with validation appropriate to the change and protection against bypass.

**Blocked by:** 02: Single-root public main.

**Status:** done

- [x] Configure one consistently reported required check for pull requests and `main`; documentation-only changes validate links, format and changed-file scope without unnecessary compilation.
- [x] Code, build and CI-definition changes run native Windows Debug and Release builds and headless CTest, including kernel purity; CI changes cannot be classified as documentation-only.
- [x] Do not count the real window-launching GUI test as a hosted headless success until it is proven reliable on the runner. Record local actual-app smoke evidence separately when relevant.
- [x] Exercise representative documentation-only and code/CI PRs to show the required check appears and runs the correct validation, including a failure or skipped-job scenario that cannot falsely appear green.
- [x] Protect `main` from routine direct pushes and force pushes, require the passing PR check and verify the restriction through actual GitHub behavior. Avoid a mandatory self-review requirement that the sole maintainer cannot satisfy.
- [x] Record what CI does and does not cover, with reproducible evidence from the real GitHub checks. Do not auto-merge, release or deploy.

The first and fifth items closed on 2026-11-23, when the user approved the `main` protection and it was applied; see "Protection evidence".

## Decisions (2026-11-15)

- **D2: no bypass.** The protection on `main` lists no bypass actor, the owner included. In an emergency, switch the protection off, push, and switch it back on; GitHub's audit log records it, and the ticket that needed it says why.
- **D3: the demonstration PRs are real work.** The code/CI demonstration is the PR that adds the CI definition, since a CI change always takes the full build path. The documentation-only demonstration is a records cleanup that is worth merging on its own:
  - correct the `Status:` line of every finished ticket, but only once ticket 04's `done` value exists; before that, fix wording only;
  - replace roadmap ticket 08's CoreAudio wording with the Windows audio path;
  - correct roadmap ticket 07's statement that ids are not stable across sessions, since ids are now minted at authoring time and survive Save/Open;
  - remove "Preserve existing uncommitted work." from both identical root instruction files: it described the pre-bootstrap dirty tree, and their Commits rule already covers unrelated dirty changes.

  The failure scenario runs on the same PR: push a commit with a deliberately broken link, see the required check report failure, then fix it.

## Decisions (2026-11-23)

- **Window tests are off by default.** The real-window tests flashed windows on the user's screen during agent runs. The user chose the recommended option: "sure i take your recommendation Label + default off (Recommended)". The tests keep the `desktop` CTest label this ticket introduced, because the name describes the requirement.
  - CI, the build script and agents run the headless selection by default (`-LE desktop`).
  - `scripts/build_windows.ps1` runs the window tests only with `-IncludeDesktop`; `ctest --preset windows -L desktop` runs them alone.
  - They run only when a change touches the editor app, and the user is told first.
  - The rule also goes into the "Build and tests" bullet of both root instruction files.
- **Superseded later the same day: back on by default locally.** The editor's `desktop` tests now run on a private Win32 desktop, so nothing shows on the user's screen. The user then chose: "Yes, on by default (Recommended)".
  - Local runs include them again; hosted CI still excludes them with `--label-exclude desktop`.
  - Another session's PR makes the change in `scripts/build_windows.ps1` and the docs.
  - The instruction-file rule above was never added: it is no longer needed.

## Evidence (2026-11-23)

The workflow is `.github/workflows/ci.yml`; `docs/windows.md`, "Continuous integration", records what it covers and what it does not. The code/CI PR is [PR 2](https://github.com/huangxuyan1/Modular/pull/2). The documentation-only PR is [PR 3](https://github.com/huangxuyan1/Modular/pull/3), stacked on PR 2's branch so the workflow runs before it reaches `main`. Each run below is reproducible with `gh run view <run> --log`.

| Run | PR and head | Path | What the run shows |
|---|---|---|---|
| 44689772590 | PR 2, `c036649` | code | `docs_only=false`; on `windows-2022`, MSVC 19.44 built every target in Debug and in Release, and each passed 6/6 CTest: kernel, node, authoring and MIDI-file tests, CLI smoke and kernel purity. `CI gate` succeeded. |
| 45144843696 | PR 2, `4f017ee`, rebased on `main` `0d9396a` | code | The same, including the identity ticket's new kernel tests: 6/6 in Debug and Release. `CI gate` succeeded. |
| 44689982152, 45144867268 | PR 3, `ae3dcfb` and rebased `a7139fd` | docs-only | `docs_only=true`; `check_docs.py` reported 0 problems in 64 Markdown files; `git diff --check` was clean; `windows` was skipped; `CI gate` succeeded without compiling. |
| 45144947740 | PR 3, `20279bb`, deliberately broken link | docs-only | Docs checks failed with `.scratch/roadmap/issues/08-realtime-callback-probe.md:19: link target not in the repository: ../../docs/node-io.md`. `CI gate` failed with "Docs checks did not succeed." |
| 45145054128 | PR 3, `1b7d092`, link fixed | docs-only | `CI gate` succeeded. |
| 45146164840, attempt 1 | PR 2, `d44c04d`, cancelled during the build | code | Both `windows` jobs were cancelled while building. `CI gate` still ran and failed with `build=cancelled`: "Windows Debug/Release build and tests: cancelled." |
| 45146164840, attempt 2 | PR 2, `d44c04d`, re-run | code | 6/6 in Debug and Release; `CI gate` succeeded. |
| 45146555184 | PR 3, `c1fbb83`, after the review fixes | docs-only | Same result under the revised workflow and checker: "links, whitespace and changed-file scope passed". |

- **A skip cannot pass as green.** `CI gate` runs with `if: always()`. It fails unless classification and the docs checks succeed. It accepts a skipped `windows` job only when classification reported documentation-only; on the code path, anything but success fails. A change to `.github/`, an empty diff or a base commit CI cannot find always takes the code path. Two failures were shown live: a broken link (run 45144947740), and a code-path build cancelled mid-run (run 45146164840, attempt 1). `if: !cancelled()` would have left the gate skipped instead, and branch protection counts a skipped required check as passing. The workflow also re-runs when a PR is edited, so a PR retargeted to a new base is classified again rather than keeping a pass computed against its old base.
- **Format means whitespace.** Docs checks cover internal links and anchors, byte-identical instruction files, and `git diff --check` (whitespace errors and conflict markers in changed lines). No Markdown style linter runs.
- **Merge state.** While run 45144947740 was failing, PR 3 reported `mergeStateStatus: UNSTABLE`, not `BLOCKED`: its base is PR 2's branch, and no check is required there. Blocking on `main` is verified with the protection item.
- **GUI tests stay local.** CI excludes the `desktop`-labelled `graph_editor_app` and `graph_editor_gesture`, which open the real window. This ticket changes CI and documentation, not the editor, so actual-app evidence is not relevant to it. Full local suites that happened to run earlier passed 8/8, but they are not part of this ticket's evidence. On 2026-11-23 the user asked that agents stop running the window tests, which flash on their screen, so later local checks for this ticket ran only the headless selection. The decision above made that the default.
- **Runner image.** `windows-2025` and `windows-latest` now carry Visual Studio 2026, while `CMakePresets.json` names the Visual Studio 2022 generator. The job therefore pins `windows-2022`; when GitHub retires that image, the preset and the runner change together.
- **Review.** `/code-review` ran on 2026-11-23 on separate Standards and Spec axes, covering both PRs. Fixed as a result:
  - a retargeted PR kept a stale documentation-only pass;
  - pushes to `main` could cancel each other while queued;
  - the checker skipped wrapped links and drive paths, and accepted `..` above the root;
  - fences closed on info-string lines;
  - HTML anchors were supported but unused;
  - the check was called "required" too early;
  - the out-of-scope "Since answered" line in roadmap ticket 07.

  Left as is:
  - the base-commit guard repeated in two jobs;
  - coverage restated in the workflow comments and `docs/windows.md`;
  - indented code blocks and GitHub's full slug rules, which the checker only approximates. These can only cause false failures, never a false pass.
- **After PR 2 merged.** PR 2 was squash-merged as `a8c433b`, with the same tree as its head `536dd4a`. PR 3 was rebased onto `main`, retargeted to it, and passed on the documentation-only path (run 45148729045, base `a8c433b`). The `ticket/vc03-ci-gate` branch was then deleted.

## Protection evidence (2026-11-23)

The user approved all four proposals: "Approve all four (Recommended)", relayed by the planning session, then confirmed directly with "Yes, apply all four".

- **Repository settings.** Squash merging is the only method allowed (`allow_merge_commit` and `allow_rebase_merge` are false), and `delete_branch_on_merge` is true.
- **Ruleset `main`** (id 27887846, `enforcement: active`, targets `refs/heads/main`). Its rules:
  - `deletion` and `non_fast_forward`;
  - `pull_request` with 0 required approvals, no code-owner, last-push or thread-resolution requirement, and `allowed_merge_methods: [squash]`;
  - `required_status_checks` requiring `CI gate` from GitHub Actions (`integration_id` 15368), with `strict_required_status_checks_policy: true`, so a branch must be up to date with `main`.

  GitHub added `require_extra_approval_for_unattributed_changes: true` by default. Its documentation limits it to unattributed Copilot pull requests and says it has no effect when zero approvals are required.
- **No bypass (D2).** The ruleset reports `bypass_actors: []` and `current_user_can_bypass: never` for the owner.
- **A direct push is rejected.** Commit `badb049` was empty and had `main` (`819301b`) as its parent, so it would have been harmless if accepted. Pushing it straight to `main` failed with `GH013: Repository rule violations found for refs/heads/main`, listing "Changes must be made through a pull request." and "Required status check "CI gate" is expected." Afterwards `main` was still `819301b`.
- **A stale branch cannot merge.** After PRs 5 and 6 merged, PR 3 at `b8597d8` reported `mergeStateStatus: BEHIND` until it was rebased onto `819301b`.
- **Not exercised live:** a force push or a deletion of `main`, because either would be destructive if accepted. Both rules are active according to `gh api repos/<owner>/Modular/rules/branches/main`.
