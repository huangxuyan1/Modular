# 05: Provisional planning delivery

**What to build:** Let the maintainer explore uncertain roadmap or architecture ideas without making them accepted direction until a reviewed planning PR is deliberately merged.

**Blocked by:** 03: Enforced PR and CI gate.

**Status:** ready-for-agent

- [x] Document the planning maturity boundary: short-lived planning branches and draft PRs hold provisional ideas; no permanent planning branch or incidental roadmap edit in a ticket implementation PR.
- [ ] Demonstrate that rejecting an exploration closes its draft PR with a preserved explanation but does not promote its proposed direction to `main`. Do not invent a product decision solely for this demonstration.
  - Waits for the user to reject a real exploration (D6). Nothing is staged.
- [ ] Demonstrate that an independently accepted planning update can be reviewed and run through the consistently reported required check, then enters `main` only when the user chooses to merge.
  - Map ticket 04 (the rate probe) is going up as the first draft planning PR, in another session. Its `CI gate` result, review and the user's merge close this item.
- [x] Keep the versioned local specs, tickets and durable domain docs as the accepted project records; GitHub PRs provide review and exploratory history rather than replacing the local issue tracker.
- [ ] Delete integrated short-lived planning branches and report where rejected exploration and accepted direction can be found.
  - Follows items 2 and 3: once those PRs are merged and closed, observe the accepted branch's deletion and report both PRs. The procedure already says where each kind of record is found.

## Decisions (2026-11-15)

- **D6: no staged demonstration.** The next roadmap decision ticket to be resolved (map ticket 04 onward) goes up as this ticket's first planning PR, and satisfies the accepted-update item once ticket 03's required check exists. The rejection item stays pending until a real exploration is rejected; record it as pending rather than staging a rejection. This ticket may therefore stay open after its other items are done.

## Evidence (2026-11-24)

This branch carries the documentation items only (1 and 4), under the user's go-ahead for this ticket, relayed by the planning session. Items 2, 3 and 5 need real planning PRs, so the ticket keeps `Status: ready-for-agent`.

- **Procedure.** `docs/agents/planning-delivery.md` is the planning counterpart of `ticket-delivery.md`. It records the spec's **Planning maturity**, **Integration** and **Local tracker** bullets and user stories 21–25:
  - each proposal gets its own short-lived planning branch and a draft PR, and there is no permanent planning branch;
  - a ticket implementation PR makes no incidental roadmap or architecture edit, and an accepted planning change gets its own PR rather than waiting for a ticket;
  - a rejected exploration's PR is closed with a comment giving the user's reason, and its content is not accepted direction;
  - an accepted planning PR is marked ready, passes `CI gate`, and enters `main` only when the user merges it;
  - integrated planning branches are deleted: GitHub deletes the remote branch on merge (`delete_branch_on_merge` is on), and the local worktree and branch go as ticket delivery step 10 says.
- **Authority as it stands.** The spec's **Instruction transition** keeps planning commits outside the standing authority. The procedure says so: a planning branch's commits, its push and its draft PR each need the user's request, as do later pushes, marking ready and closing. It notes that one request can name all three. It does not extend the root rule.
- **Accepted records (item 4).** The procedure's "Accepted records" section names the accepted project records on `main`: specs, tickets and wayfinder maps under `.scratch/`, `docs/roadmap.md`, `CONTEXT.md`, the ADRs and the other durable docs. GitHub PRs are review history. `issue-tracker.md` now says the same and links it.
- **Pointers.** `ticket-delivery.md` no longer defers planning to this ticket: its opening and its stop for a new decision link the planning procedure. The wayfinder **Resolve** step in `issue-tracker.md` says its edits are provisional on a planning branch. The root **Commits and delivery** rule names the procedure beside `ticket-delivery.md`, and `cmp` reports `AGENTS.md` and `CLAUDE.md` identical (7,715 bytes each in the working copy). `version-control-discussion.md` lists it among the active workflow documents.
- **Not touched.** `.scratch/roadmap/`, `docs/roadmap.md`, `.scratch/audio-subtype/` and version-control ticket 04 belong to other sessions. No demonstration is staged.
- **Draft PRs run the check.** The CI workflow triggers on `pull_request` types `opened`, `synchronize`, `reopened` and `edited`, with no draft filter, so `CI gate` reports on a draft PR. Item 3 observes it.
