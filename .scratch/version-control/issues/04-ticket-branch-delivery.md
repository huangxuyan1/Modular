# 04: Ticket-branch delivery

**What to build:** Give an authorized implementation ticket a short-lived, reviewable branch path from scoped checkpoint commits through user-approved squash integration, without absorbing unrelated work.

**Blocked by:** 03: Enforced PR and CI gate.

**Status:** ready-for-agent

- [x] Record in both identical root agent-instruction files the standing authority for scoped checkpoint commits on an explicitly authorized ticket branch, and for pushing that branch and opening its PR after a public-content check of the branch diff (D5 below), and preserve the separate approval gates for initial-root migration, unrelated edits, pushing anything else and merging. Verify the instruction files remain identical.
- [x] Add `done` to the implementation-ticket status values in `docs/agents/triage-labels.md` (D4 below). A ticket's own branch sets `Status: done`, so the status becomes true on `main` when that PR merges. Wayfinder decision tickets keep `resolved`. Correct any finished ticket that still shows an open status, unless ticket 03's cleanup PR already did.
- [x] Use one short-lived branch per ticket. Keep the implementation, directly related documentation, ticket status and review evidence together; do not settle a new architecture decision or alter the roadmap incidentally on the code branch.
- [ ] Use the primary checkout for sequential work and an additional worktree only when concurrent work needs isolation. Demonstrate the isolation and cleanup path without disturbing unrelated files.
  - Isolation is shown; cleanup is still to be observed. See "Isolation and cleanup" below.
- [ ] Exercise an agreed ticket's end-to-end workflow: scoped checkpoint commits, actual change review (including uncommitted changes), the required GitHub check, user's decision to merge, one accepted squash commit on `main` and source-branch deletion.
  - Remains: the first ticket delivered under the standing rule after this ticket merges. See "End to end" below.
- [ ] Show that failed checks, ambiguous file scope or a new architecture decision stop automatic progression instead of silently committing or merging.
  - The rule is recorded and a red check is shown to block. A stop for unclear scope or a new architecture decision waits for a real occurrence. See "Stops" below.

The standing commit rule takes effect only once approved and recorded; it does not retroactively authorize committing the pre-bootstrap dirty tree.

## Decisions (2026-11-15)

- **D4: a status for finished tickets.** No implementation status value means "done", which is why finished tickets still read `ready-for-agent`. Add `done`. Setting it on the ticket's own branch keeps the status and the work in one PR, so `main` never claims a ticket is finished before its work has merged.
- **D5: standing authority reaches the PR, not the merge.** Without it, every ticket stops to ask before pushing its own branch. The authority covers scoped checkpoint commits, pushing the authorized ticket branch and opening its PR. The repository is public, so every push publishes: before pushing, run the same public-content check the root commit received, on the branch diff. Pushing `main` and merging stay with the user. This amends the spec's **Instruction transition**; see its Amendments section.

## Evidence (2026-11-23)

This ticket ran on its own branch and worktree under the user's go-ahead for it ("Start both", relayed by the planning session). The standing rule it records takes effect only once its PR merges.

- **Instruction files.** Both root files replace the **Commits** bullet with **Commits and delivery**. It grants the standing authority of D5 and keeps every other gate with the user: pushing `main` or any other branch, history rewrites such as the root migration, merging, GitHub settings and unrelated edits. The `/implement` bullet now pins review to the branch's diff from `main` plus uncommitted work, and the triage-label summary names `done`. `cmp` reports the two files identical, as does `scripts/check_docs.py`. Each is 7,481 bytes, within Codex's default 32 KiB budget.
- **Procedure.** `docs/agents/ticket-delivery.md` holds the steps, the public-content check and the stops. Three agent docs that restated "commit only when asked" now point at it: `matt-pocock-workflow.md`, `matt-skills-catalog.md` and `version-control-discussion.md`. The last is a historical record and now says so.
- **`done`.** Added to `docs/agents/triage-labels.md`. Ticket 03's cleanup PR changed no `Status:` line, because `done` did not exist yet. This branch sets `done` on 14 tickets. Each has every checklist item ticked, and its recorded evidence shows the work finished and on `main`:
  - graph-editor 01–05: implementation notes and review records `01-review.md` to `05-review.md`, published in the root commit;
  - identity 01–03: the claim closed on 2026-11-20, and ticket 03 merged as PR 4;
  - supernode 01–03: the claim closed on 2026-07-30;
  - version-control 01–03: 01 and 02 completed the bootstrap, and 03 merged as PRs 2 and 3.

  Unchanged: this ticket and version-control 05, which have open items; roadmap tickets, which keep their wayfinder values; specs and `planning.md`, which are not tickets. No finished ticket was in doubt.
- **One branch per ticket.** This branch carries the rule, the procedure, the status corrections and this ticket's evidence and review together. It changes no roadmap entry, ADR or architecture decision.

### Isolation and cleanup

- **Isolation.** While this ticket ran, `git worktree list` showed it in its own worktree on its own branch. The audio-subtype session worked beside it in another worktree, on another branch, writing `.scratch/audio-subtype/issues/`. Both started from `main` at `71fc29b`, and the primary checkout stayed on `main`. This branch's diff has no path under `.scratch/audio-subtype/`.
- **Cleanup, not yet shown.** No head branch of merged PRs 1–6 is left on GitHub; PR 3's was deleted two seconds after its merge (`head_ref_deleted` in its event log). Three local worktrees and their branches from those PRs remain; they belong to other sessions, so this ticket leaves them alone. Cleanup is shown when this ticket's own worktree and local branch are removed after it merges (step 10 of the procedure).

### End to end

Ticket 03's PR 2 passed through every stage before the standing rule existed: checkpoint commits on its own branch, `/code-review` on both axes, `CI gate`, the user's merge, squash commit `a8c433b` with the single parent `0d9396a`, and its branch deleted. `main` was not yet protected then. What remains is the same run under the standing rule and the active protection, by the first ticket delivered after this one merges. The audio-subtype feature's first ticket is next. Its squash commit and branch deletion close this item.

### Stops

The rule is in both root files and in the procedure's "Stops" section. A red check blocks merging: ticket 03 showed `CI gate` failing on a broken link and on a cancelled build, and the protection on `main` requires it. Nothing merges automatically. A stop for unclear file scope or a new architecture decision has not yet happened under the standing rule. Staging one would be invented work, so this item waits for a real occurrence.

### Choices made in this ticket

- **No force push.** Catching up with `main` merges `origin/main` into the ticket branch, and the squash drops that merge commit. A force push counts as a history rewrite and needs the user's request.
- **Commit identity.** Commit metadata is published with every push, so the public-content check covers author addresses. This branch's commits use the account's GitHub noreply address, as the squash commits on `main` do.
