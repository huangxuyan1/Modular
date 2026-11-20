# 04: Ticket-branch delivery

**What to build:** Give an authorized implementation ticket a short-lived, reviewable branch path from scoped checkpoint commits through user-approved squash integration, without absorbing unrelated work.

**Blocked by:** 03: Enforced PR and CI gate.

**Status:** ready-for-agent

- [ ] Record in both identical root agent-instruction files the standing authority for scoped checkpoint commits on an explicitly authorized ticket branch, and for pushing that branch and opening its PR after a public-content check of the branch diff (D5 below), and preserve the separate approval gates for initial-root migration, unrelated edits, pushing anything else and merging. Verify the instruction files remain identical.
- [ ] Add `done` to the implementation-ticket status values in `docs/agents/triage-labels.md` (D4 below). A ticket's own branch sets `Status: done`, so the status becomes true on `main` when that PR merges. Wayfinder decision tickets keep `resolved`. Correct any finished ticket that still shows an open status, unless ticket 03's cleanup PR already did.
- [ ] Use one short-lived branch per ticket. Keep the implementation, directly related documentation, ticket status and review evidence together; do not settle a new architecture decision or alter the roadmap incidentally on the code branch.
- [ ] Use the primary checkout for sequential work and an additional worktree only when concurrent work needs isolation. Demonstrate the isolation and cleanup path without disturbing unrelated files.
- [ ] Exercise an agreed ticket's end-to-end workflow: scoped checkpoint commits, actual change review (including uncommitted changes), the required GitHub check, user's decision to merge, one accepted squash commit on `main` and source-branch deletion.
- [ ] Show that failed checks, ambiguous file scope or a new architecture decision stop automatic progression instead of silently committing or merging.

The standing commit rule takes effect only once approved and recorded; it does not retroactively authorize committing the pre-bootstrap dirty tree.

## Decisions (2026-11-15)

- **D4: a status for finished tickets.** No implementation status value means "done", which is why finished tickets still read `ready-for-agent`. Add `done`. Setting it on the ticket's own branch keeps the status and the work in one PR, so `main` never claims a ticket is finished before its work has merged.
- **D5: standing authority reaches the PR, not the merge.** Without it, every ticket stops to ask before pushing its own branch. The authority covers scoped checkpoint commits, pushing the authorized ticket branch and opening its PR. The repository is public, so every push publishes: before pushing, run the same public-content check the root commit received, on the branch diff. Pushing `main` and merging stay with the user. This amends the spec's **Instruction transition**; see its Amendments section.
