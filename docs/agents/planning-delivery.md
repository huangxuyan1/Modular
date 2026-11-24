# Planning delivery

How a provisional roadmap or architecture proposal travels on its own planning
branch and draft PR, to `main` when the user accepts it or to a closed PR when
the user rejects it. Implementation tickets follow
[ticket delivery](ticket-delivery.md) instead. The decisions behind both are in
`.scratch/version-control/spec.md` (**Planning maturity**, **Integration** and
**Local tracker**).

## Accepted records

`main` is the stable view of accepted direction. Its accepted project records
are versioned local Markdown: specs, tickets and wayfinder maps under
`.scratch/` (see [issue-tracker.md](issue-tracker.md)), `docs/roadmap.md`,
`CONTEXT.md`, the ADRs in `docs/adr/` and the other durable docs under
`docs/`. GitHub PRs are review history: an
open draft PR holds a provisional idea and a closed one holds rejected
exploration. Neither is accepted direction, and neither replaces the local
tracker. A proposal's content becomes accepted only when the user merges its PR.

## Authority

The standing authority in the root **Commits and delivery** rule covers
authorized implementation tickets only, not planning commits. A planning
branch's commits, its push and its draft PR each need the user's request. One
request can name all three, as "open a draft planning PR for map ticket 04"
does. Later pushes, marking the PR ready and closing it also wait for the user.

## Steps

1. **Branch.** Each proposal gets its own short-lived planning branch, started
   from a freshly fetched `origin/main` as in ticket delivery step 1. There is
   no permanent planning branch.
2. **Content.** Write the proposal as the change it would make to the accepted
   records: the map ticket's answer and status, `docs/roadmap.md`, an ADR or
   `CONTEXT.md`. A wayfinder resolution made on a planning branch (see
   [issue-tracker.md](issue-tracker.md#wayfinding-operations)) is provisional
   until its PR merges, including its `resolved` status and its line in the
   map's Decisions so far. The branch carries only this proposal.
3. **Checks.** A documentation-only change runs `python scripts/check_docs.py`
   and `git diff --check`; anything else runs what ticket delivery step 4 names.
4. **Public-content check.** Before every push, as in ticket delivery step 6.
5. **Draft PR.** `git push -u origin <branch>`, then
   `gh pr create --draft --base main`. The body states the question, the
   proposal and what it would change in the accepted records. `CI gate` runs on
   a draft PR as on any other. Revisions the user asks for go to the same branch.
6. **The user decides.**
   - **Accepted.** The branch states the direction as the user accepted it. At
     the user's request, mark the PR ready (`gh pr ready`; GitHub cannot merge
     a draft) and bring the branch up to date with `main` as in ticket delivery
     step 8. `CI gate` must pass. The proposal enters `main` only when the user
     merges it, as one squash commit.
   - **Rejected.** At the user's request, close the PR with a comment that
     gives the user's reason (`gh pr close <number> --comment "<reason>"`).
     Nothing from it enters `main`, and its content is not accepted direction.
7. **Cleanup.** Integrated planning branches are deleted: GitHub deletes the
   remote branch on merge, and the local worktree and branch go as in ticket
   delivery step 10. Closing a rejected PR leaves its branch; the closed PR keeps
   its commits and discussion either way, and deleting that branch is a push the
   user decides.

## Ticket PRs carry no planning

A ticket implementation PR makes no incidental roadmap or architecture edit.
A new decision met during a ticket stops it (ticket delivery, "Holds and
stops"). It reaches `main` through its own planning PR when the user asks for
one. In the other direction, an accepted planning change gets its own PR
rather than waiting for an unrelated ticket to carry it.

## Finding past planning

- **Accepted direction:** the records on `main` listed under
  [Accepted records](#accepted-records).
- **Open proposals:** `gh pr list --draft`.
- **Rejected exploration:** `gh pr list --state closed --search "is:unmerged"`;
  each PR's closing comment gives the reason.
