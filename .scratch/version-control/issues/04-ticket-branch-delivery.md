# 04: Ticket-branch delivery

**What to build:** Give an authorized implementation ticket a short-lived, reviewable branch path from scoped checkpoint commits through user-approved squash integration, without absorbing unrelated work.

**Blocked by:** 03: Enforced PR and CI gate.

**Status:** ready-for-agent

- [ ] Record in both identical root agent-instruction files the standing authority for scoped checkpoint commits on an explicitly authorized ticket branch, and preserve the separate approval gates for initial-root migration, unrelated edits, publishing and merging. Verify the instruction files remain identical.
- [ ] Use one short-lived branch per ticket. Keep the implementation, directly related documentation, ticket status and review evidence together; do not settle a new architecture decision or alter the roadmap incidentally on the code branch.
- [ ] Use the primary checkout for sequential work and an additional worktree only when concurrent work needs isolation. Demonstrate the isolation and cleanup path without disturbing unrelated files.
- [ ] Exercise an agreed ticket's end-to-end workflow: scoped checkpoint commits, actual change review (including uncommitted changes), the required GitHub check, user's decision to merge, one accepted squash commit on `main` and source-branch deletion.
- [ ] Show that failed checks, ambiguous file scope or a new architecture decision stop automatic progression instead of silently committing or merging.

The standing commit rule takes effect only once approved and recorded; it does not retroactively authorize committing the pre-bootstrap dirty tree.
