# 02: Single-root public main

**What to build:** Publish the explicitly approved working snapshot as the only initial root commit on a public GitHub `main`, without exposing the rough legacy ancestry or losing unreviewed work.

**Blocked by:** 01: Audited, green baseline candidate.

**Status:** ready-for-agent

- [ ] Require the user's approval of the exact ticket-01 candidate tree and any proposed sanitization or exclusion before replacing history. If the tree has changed since approval, re-audit the difference and obtain renewed approval.
- [ ] Create the new root commit from only the approved snapshot, with no previous commit as ancestor; verify both its ancestry and selected file tree locally.
- [ ] Arrange a public GitHub repository only after the content audit and approved root; obtain user sign-in or repository details only when needed. Show the exact publication target before the outward-facing push.
- [ ] Verify the published `main` points to the approved root with the expected tree and no old commits in its ancestry. Remove old local branch names only after that verification and the user's approval; do not deliberately garbage-collect old Git objects.
- [ ] Report the resulting commit, published target, verification evidence, excluded material and any remaining cleanup accurately. Do not activate standing ticket-commit permission merely by completing the bootstrap.

The initial-root operation is a one-time exception to the future PR-only integration rule, not a precedent for routine direct pushes.
