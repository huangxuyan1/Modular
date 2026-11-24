# 05: Provisional planning delivery

**What to build:** Let the maintainer explore uncertain roadmap or architecture ideas without making them accepted direction until a reviewed planning PR is deliberately merged.

**Blocked by:** 03: Enforced PR and CI gate.

**Status:** ready-for-agent

- [x] Document the planning maturity boundary: short-lived planning branches and draft PRs hold provisional ideas; no permanent planning branch or incidental roadmap edit in a ticket implementation PR.
- [ ] Demonstrate that rejecting an exploration closes its draft PR with a preserved explanation but does not promote its proposed direction to `main`. Do not invent a product decision solely for this demonstration.
  - The handling is documented (`docs/agents/planning-delivery.md`, "The user decides", **Rejected**). Only the real occurrence is pending: a planning PR the user rejects (D6). Nothing is staged. A later records PR records it.
- [ ] Demonstrate that an independently accepted planning update can be reviewed and run through the consistently reported required check, then enters `main` only when the user chooses to merge.
  - Map ticket 04 (the rate probe) is going up as the first draft planning PR, in another session. Its `CI gate` result, review and the user's merge close this item, recorded by a later records PR.
- [x] Keep the versioned local specs, tickets and durable domain docs as the accepted project records; GitHub PRs provide review and exploratory history rather than replacing the local issue tracker.
- [ ] Delete integrated short-lived planning branches and report where rejected exploration and accepted direction can be found.
  - The handling is documented (`docs/agents/planning-delivery.md`, **Cleanup** and "Finding past planning"). Only the real occurrence is pending: after items 2 and 3, observe both planning branches deleted and report the merged and the closed PR. A later records PR records it.

## Decisions (2026-11-15)

- **D6: no staged demonstration.** The next roadmap decision ticket to be resolved (map ticket 04 onward) goes up as this ticket's first planning PR, and satisfies the accepted-update item once ticket 03's required check exists. The rejection item stays pending until a real exploration is rejected; record it as pending rather than staging a rejection. This ticket may therefore stay open after its other items are done.

## Evidence (2026-11-24)

This branch carries the documentation items only (1 and 4), under the user's go-ahead for this ticket, relayed by the planning session. Items 2, 3 and 5 need real planning PRs, so the ticket keeps `Status: ready-for-agent`.

- **Procedure.** `docs/agents/planning-delivery.md` is the planning counterpart of `ticket-delivery.md`. It records the spec's **Planning maturity**, **Integration** and **Local tracker** bullets and user stories 21–25:
  - each proposal gets its own short-lived planning branch and a draft PR, and there is no permanent planning branch;
  - a ticket implementation PR makes no incidental roadmap or architecture edit, and an accepted planning change gets its own PR rather than waiting for a ticket;
  - a rejected planning PR is closed unmerged as a draft (returned to draft first if it was marked ready), with a comment giving the user's reason, and its branch is deleted. The closed PR keeps the exploration and its rationale, its content is not accepted direction, and the decision ticket it tried to resolve stays open and unclaimed on `main` to be attempted again;
  - an accepted planning PR is marked ready, passes `CI gate`, and enters `main` only when the user merges it;
  - every decided planning branch is deleted: GitHub deletes the remote branch on merge (`delete_branch_on_merge` is on), the rejection step deletes it on close, and the user removes the local worktree and branch or asks a session to. GitHub keeps a PR's commits after its branch is gone: PRs 1–8 are merged and their head branches deleted, yet on 2026-11-24 `git ls-remote origin` still listed `refs/pull/1/head` to `refs/pull/8/head`. That is shown for merged PRs only; item 2 observes it for a closed one;
  - a planning branch leaves this ticket alone, so a later records PR ticks items 2, 3 and 5.
- **User ruling (2026-11-24).** Relayed by the planning session: item 2 stays open until a real rejection, but the procedure that handles one is complete in this ticket ("you could keep them but have the logic to handle that done"). The rejection handling above, and the notes under items 2 and 5, follow it.
- **Authority as it stands.** The spec's **Instruction transition** keeps planning commits outside the standing authority. The procedure says so: creating a planning branch and committing to it, pushing it and opening its draft PR each need the user's request, as do later pushes, marking ready, and closing with its branch. A single request may name all three explicitly. The root rule is not extended.
- **Accepted records (item 4).** The procedure's "Accepted records" section names the accepted project records on `main`: specs, tickets, review notes and wayfinder maps under `.scratch/`, `docs/roadmap.md`, `CONTEXT.md`, the ADRs and the other durable docs. GitHub PRs are review history. `issue-tracker.md` now says the same and links it.
- **Pointers.** `ticket-delivery.md` no longer defers planning to this ticket: its opening and its stop for a new decision link the planning procedure. In `issue-tracker.md`, the wayfinder **Claim** and **Resolve** steps say a claim and a resolution on a planning branch do not show on `main` until its PR merges. The root **Commits and delivery** rule names the procedure beside `ticket-delivery.md`, and `cmp` reports `AGENTS.md` and `CLAUDE.md` identical (7,594 bytes each as committed). `version-control-discussion.md` lists it among the active workflow documents.
- **Not touched.** `.scratch/roadmap/`, `docs/roadmap.md`, `.scratch/audio-subtype/` and version-control ticket 04 belong to other sessions. No demonstration is staged.
- **Draft PRs run the check.** The CI workflow triggers on `pull_request` types `opened`, `synchronize`, `reopened` and `edited`, with no draft filter, so `CI gate` reports on a draft PR. Item 3 observes it.

### Review (2026-11-24)

`/code-review` ran twice on separate Standards and Spec axes over `git diff origin/main...HEAD`, with nothing uncommitted: first at `1ae3739`, then at `b685b32`. The user's ruling on rejection handling arrived during the first round and went into `b685b32` with that round's fixes. The second round's fixes were checked with `check_docs.py` and `git diff --check`, not by a third review round.

Fixed after the first round:
- The Authority section read a request for a draft PR as also authorizing its commits and push, and gave map ticket 04 as the example. It now says only that one request may name all three explicitly (Spec).
- The procedure cited ticket-delivery steps by number, which a renumbering would break without any check failing. It now names them and links that file's steps (Standards).
- The rejected-exploration query also listed closed ticket PRs, and the open-proposals note missed an accepted PR awaiting merge (both axes).
- The accepted records left out review notes (Spec).
- A claim made on a planning branch does not show on `main`, so another session could take the same map ticket. The procedure's **Content** step and the wayfinder **Claim** step now state this (Spec). A check of open PRs before claiming was added, then withdrawn in the second round.
- The Checks step sent non-Markdown changes to ticket delivery's Checks step, which brings in `/code-review` for that case only. It now names the documentation checks and the `docs/windows.md` suite (Standards).
- The Evidence gave the root files' working-copy size, the mistake ticket 04's review also fixed (Standards).

Fixed after the second round:
- Authority said "a planning branch's commits" where the brief names the branch itself. It now says creating a planning branch and committing to it (Spec).
- The rejected-exploration query assumed a rejected PR is still a draft, which a PR already marked ready would break. The **Rejected** case now returns it to draft before closing (Spec).
- The check of open PRs before claiming was a convention neither the spec, the brief nor the ruling contains, and **Frontier** did not follow it. The procedure now states only the fact (Spec). Whether an open planning PR counts as a claim is raised with the user. Standards had asked to extend the check instead.
- A new attempt at a rejected decision ticket could miss the earlier PR. The draft PR's body now names its decision ticket's file, and a new attempt starts from the closed PR's reason (Standards).
- **Cleanup** sent rejected branches to ticket delivery's **Cleanup** step, which begins once a squash commit is on `main`. It now gives the commands itself (Standards).
- The "Version-control ticket 05" section cited items by number and would go stale. It names the items and says to drop the section once the ticket is `done` (Standards).
- The Evidence claimed `main` was the only remote branch; PR 9's branch has since appeared. The fact is now dated and limited to merged PRs (Standards).

Left as is:
- The "Version-control ticket 05" section repeats the Evidence. The ruling asks the procedure to say how the occurrence is recorded (Standards).
- Pointers that restate their target's gist: the accepted records and the Claim and Resolve notes in `issue-tracker.md`, the stop in `ticket-delivery.md` and the root rule's gates in Authority. Each pointer needs its one-line gist to fire, and the brief requires the procedure itself to state these rules (Standards).
- "The other durable docs under `docs/`" includes historical records such as `version-control-discussion.md`. They are accepted as the history they say they are (Spec, nit).
- Research branches, named in the wayfinder notes of `matt-skills-catalog.md`. The spec covers ticket and planning branches only, so they stay under the root rule's "everything else" (Standards). Raised with the user.
- Whether planning PRs run `/code-review`. The spec says only that PRs are reviewed, so the procedure leaves it open (both axes). Raised with the user.
- Whether an open planning PR counts as a claim on its decision ticket. See above; raised with the user.
