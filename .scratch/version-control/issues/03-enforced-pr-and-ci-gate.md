# 03: Enforced PR and CI gate

**What to build:** Make every post-bootstrap change to public `main` pass a reliably reported PR check, with validation appropriate to the change and protection against bypass.

**Blocked by:** 02: Single-root public main.

**Status:** ready-for-agent

- [ ] Configure one consistently reported required check for pull requests and `main`; documentation-only changes validate links, format and changed-file scope without unnecessary compilation.
- [ ] Code, build and CI-definition changes run native Windows Debug and Release builds and headless CTest, including kernel purity; CI changes cannot be classified as documentation-only.
- [ ] Do not count the real window-launching GUI test as a hosted headless success until it is proven reliable on the runner. Record local actual-app smoke evidence separately when relevant.
- [ ] Exercise representative documentation-only and code/CI PRs to show the required check appears and runs the correct validation, including a failure or skipped-job scenario that cannot falsely appear green.
- [ ] Protect `main` from routine direct pushes and force pushes, require the passing PR check and verify the restriction through actual GitHub behavior. Avoid a mandatory self-review requirement that the sole maintainer cannot satisfy.
- [ ] Record what CI does and does not cover, with reproducible evidence from the real GitHub checks. Do not auto-merge, release or deploy.

## Decisions (2026-11-15)

- **D2: no bypass.** The protection on `main` lists no bypass actor, the owner included. In an emergency, switch the protection off, push, and switch it back on; GitHub's audit log records it, and the ticket that needed it says why.
- **D3: the demonstration PRs are real work.** The code/CI demonstration is the PR that adds the CI definition, since a CI change always takes the full build path. The documentation-only demonstration is a records cleanup that is worth merging on its own:
  - correct the `Status:` line of every finished ticket, but only once ticket 04's `done` value exists; before that, fix wording only;
  - replace roadmap ticket 08's CoreAudio wording with the Windows audio path;
  - correct roadmap ticket 07's statement that ids are not stable across sessions, since ids are now minted at authoring time and survive Save/Open;
  - remove "Preserve existing uncommitted work." from both identical root instruction files: it described the pre-bootstrap dirty tree, and their Commits rule already covers unrelated dirty changes.

  The failure scenario runs on the same PR: push a commit with a deliberately broken link, see the required check report failure, then fix it.
