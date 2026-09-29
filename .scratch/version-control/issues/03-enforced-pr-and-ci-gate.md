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
