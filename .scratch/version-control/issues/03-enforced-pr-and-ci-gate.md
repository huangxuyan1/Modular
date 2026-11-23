# 03: Enforced PR and CI gate

**What to build:** Make every post-bootstrap change to public `main` pass a reliably reported PR check, with validation appropriate to the change and protection against bypass.

**Blocked by:** 02: Single-root public main.

**Status:** ready-for-agent

- [ ] Configure one consistently reported required check for pull requests and `main`; documentation-only changes validate links, format and changed-file scope without unnecessary compilation.
- [x] Code, build and CI-definition changes run native Windows Debug and Release builds and headless CTest, including kernel purity; CI changes cannot be classified as documentation-only.
- [x] Do not count the real window-launching GUI test as a hosted headless success until it is proven reliable on the runner. Record local actual-app smoke evidence separately when relevant.
- [x] Exercise representative documentation-only and code/CI PRs to show the required check appears and runs the correct validation, including a failure or skipped-job scenario that cannot falsely appear green.
- [ ] Protect `main` from routine direct pushes and force pushes, require the passing PR check and verify the restriction through actual GitHub behavior. Avoid a mandatory self-review requirement that the sole maintainer cannot satisfy.
- [x] Record what CI does and does not cover, with reproducible evidence from the real GitHub checks. Do not auto-merge, release or deploy.

The first and fifth items stay open until the user approves the `main` protection: the `CI gate` check is configured and reports on every PR and push to `main`, but nothing requires it yet.

## Decisions (2026-11-15)

- **D2: no bypass.** The protection on `main` lists no bypass actor, the owner included. In an emergency, switch the protection off, push, and switch it back on; GitHub's audit log records it, and the ticket that needed it says why.
- **D3: the demonstration PRs are real work.** The code/CI demonstration is the PR that adds the CI definition, since a CI change always takes the full build path. The documentation-only demonstration is a records cleanup that is worth merging on its own:
  - correct the `Status:` line of every finished ticket, but only once ticket 04's `done` value exists; before that, fix wording only;
  - replace roadmap ticket 08's CoreAudio wording with the Windows audio path;
  - correct roadmap ticket 07's statement that ids are not stable across sessions, since ids are now minted at authoring time and survive Save/Open;
  - remove "Preserve existing uncommitted work." from both identical root instruction files: it described the pre-bootstrap dirty tree, and their Commits rule already covers unrelated dirty changes.

  The failure scenario runs on the same PR: push a commit with a deliberately broken link, see the required check report failure, then fix it.

## Evidence (2026-11-23)

The workflow is `.github/workflows/ci.yml`; `docs/windows.md`, "Continuous integration", records what it covers and what it does not. The code/CI PR is [PR 2](https://github.com/huangxuyan1/Modular/pull/2). The documentation-only PR is [PR 3](https://github.com/huangxuyan1/Modular/pull/3), stacked on PR 2's branch so the workflow runs before it reaches `main`. Each run below is reproducible with `gh run view <run> --log`.

| Run | PR and head | Path | What the run shows |
|---|---|---|---|
| 44689772590 | PR 2, `c036649` | code | `docs_only=false`; on `windows-2022`, MSVC 19.44 built every target in Debug and in Release, and each passed 6/6 CTest: kernel, node, authoring and MIDI-file tests, CLI smoke and kernel purity. `CI gate` succeeded. |
| 45144843696 | PR 2, `4f017ee`, rebased on `main` `0d9396a` | code | The same, including the identity ticket's new kernel tests: 6/6 in Debug and Release. `CI gate` succeeded. |
| 44689982152, 45144867268 | PR 3, `ae3dcfb` and rebased `a7139fd` | docs-only | `docs_only=true`; `check_docs.py` reported 0 problems in 64 Markdown files; `git diff --check` was clean; `windows` was skipped; `CI gate` succeeded without compiling. |
| 45144947740 | PR 3, `20279bb`, deliberately broken link | docs-only | Docs checks failed with `.scratch/roadmap/issues/08-realtime-callback-probe.md:19: link target not in the repository: ../../docs/node-io.md`. `CI gate` failed with "Docs checks did not succeed." |
| 45145054128 | PR 3, `1b7d092`, link fixed | docs-only | `CI gate` succeeded. |

- **A skip cannot pass as green.** `CI gate` runs with `if: always()`. It fails unless classification and the docs checks succeed. It accepts a skipped `windows` job only when classification reported documentation-only; on the code path, anything but success fails. A change to `.github/`, an empty diff or a base commit CI cannot find always takes the code path. The live failure is run 45144947740. A skipped build on the code path was not forced live, because that would mean breaking the workflow itself.
- **Merge state.** While run 45144947740 was failing, PR 3 reported `mergeStateStatus: UNSTABLE`, not `BLOCKED`: its base is PR 2's branch, and no check is required there. Blocking on `main` is verified with the protection item.
- **GUI tests stay local.** CI excludes the `desktop`-labelled `graph_editor_app` and `graph_editor_gesture`, which open the real window. Local full suites passed 8/8, including both desktop tests: Debug and Release on 2026-11-20 at `c036649`, and Debug on 2026-11-23 at `4f017ee` plus PR 3's documentation changes.
- **Runner image.** `windows-2025` and `windows-latest` now carry Visual Studio 2026, while `CMakePresets.json` names the Visual Studio 2022 generator. The job therefore pins `windows-2022`; when GitHub retires that image, the preset and the runner change together.
- **After PR 2 merges.** Rebase PR 3 onto `main` with `git rebase --onto origin/main <PR 2 head> docs/records-cleanup`, retarget it to `main`, and confirm that its run takes the documentation-only path again.
