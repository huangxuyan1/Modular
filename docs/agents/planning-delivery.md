# Planning delivery

How a provisional roadmap or architecture proposal travels on its own planning
branch and draft PR, to `main` when the user accepts it or to a closed PR when
the user rejects it. Implementation tickets follow
[ticket delivery](ticket-delivery.md) instead; the steps named below are its
[Steps](ticket-delivery.md#steps). The decisions behind both are in
`.scratch/version-control/spec.md` (**Planning maturity**, **Integration** and
**Local tracker**).

## Accepted records

`main` is the stable view of accepted direction. Its accepted project records
are versioned local Markdown: specs, tickets, review notes and wayfinder maps
under `.scratch/` (see [issue-tracker.md](issue-tracker.md)),
`docs/roadmap.md`, `CONTEXT.md`, the ADRs in `docs/adr/` and the other durable
docs under `docs/`. GitHub PRs are review history: an open draft PR holds a
provisional idea and a closed one holds rejected exploration. Neither is
accepted direction, and neither replaces the local tracker. A proposal's
content becomes accepted only when the user merges its PR.

## Authority

The standing authority in the root **Commits and delivery** rule covers
authorized implementation tickets only, not planning commits. A planning
branch's commits, its push and its draft PR each need the user's request; a
single request may name all three explicitly. Later pushes, marking the PR
ready, and closing it with its branch also wait for the user.

## Steps

1. **Branch.** Each proposal gets its own short-lived planning branch, started
   from a freshly fetched `origin/main` as in ticket delivery's **Branch** step.
   There is no permanent planning branch.
2. **Content.** Write the proposal as the change it would make to the accepted
   records: the map ticket's answer and status, `docs/roadmap.md`, an ADR or
   `CONTEXT.md`. The branch carries only this proposal. A wayfinder claim and
   resolution made on a planning branch (see
   [issue-tracker.md](issue-tracker.md#wayfinding-operations)) stay provisional
   until its PR merges: its `claimed` and `resolved` statuses and its line in the
   map's Decisions so far. Until then `main` shows the ticket unclaimed, so
   check the open PRs (`gh pr list`) before claiming a map ticket.
3. **Checks.** Run `python scripts/check_docs.py` and `git diff --check`. A
   branch that changes more than Markdown also runs the focused checks and full
   suite `docs/windows.md` names.
4. **Public-content check.** Before every push, as in ticket delivery's
   **Public-content check** step.
5. **Draft PR.** `git push -u origin <branch>`, then
   `gh pr create --draft --base main`. The body states the question, the
   proposal and what it would change in the accepted records. `CI gate` runs on
   a draft PR as on any other. Revisions the user asks for go to the same branch.
6. **The user decides.**
   - **Accepted.** The branch states the direction as the user accepted it. At
     the user's request, mark the PR ready (`gh pr ready`; GitHub cannot merge
     a draft) and bring the branch up to date with `main` as in ticket
     delivery's **`CI gate`** step. `CI gate` must pass. The proposal enters
     `main` only when the user merges it, as one squash commit.
   - **Rejected.** At the user's request, close the PR unmerged with a comment
     that gives the user's reason, then delete its remote branch:
     `gh pr close <number> --comment "<reason>"` and
     `git push origin --delete <branch>`. The closed PR keeps the rejected
     exploration and its rationale; GitHub keeps its commits after the branch
     is gone. Nothing from it enters `main`, and its content is not accepted
     direction. The decision ticket it tried to resolve stays open and
     unclaimed on `main`, because its resolution never merged, and a new
     planning branch may attempt it again.
7. **Cleanup.** Every decided planning branch is deleted: GitHub deletes the
   remote branch on merge, and the rejection step above deletes it on close.
   The local worktree and branch go as in ticket delivery's **Cleanup** step.

## Ticket PRs carry no planning

A ticket implementation PR makes no incidental roadmap or architecture edit.
A new decision met during a ticket stops it (ticket delivery,
[Holds and stops](ticket-delivery.md#holds-and-stops)). It reaches `main`
through its own planning PR when the user asks for one. In the other direction,
an accepted planning change gets its own PR rather than waiting for an
unrelated ticket to carry it.

## Finding past planning

- **Accepted direction:** the records on `main` listed under
  [Accepted records](#accepted-records).
- **Open proposals:** `gh pr list --draft`. An accepted proposal is marked
  ready and stays open until the user merges it.
- **Rejected exploration:** a rejected planning PR is closed while still a
  draft, so
  `gh pr list --state closed --search "draft:true is:unmerged" --limit 100`
  lists them. Each PR's closing comment gives the reason.

## Version-control ticket 05

Items 2, 3 and 5 of `.scratch/version-control/issues/05-provisional-planning-delivery.md`
close on the first real rejection, acceptance and branch cleanup under this
procedure. A planning branch carries only its proposal and leaves that ticket
alone. A later records PR, made when the user asks, ticks those items with the
planning PRs as evidence.
