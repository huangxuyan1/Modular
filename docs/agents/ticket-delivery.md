# Ticket delivery

How an authorized implementation ticket travels from its own branch to one
squash commit on `main`. The **Commits and delivery** rule in the root
`AGENTS.md`/`CLAUDE.md` says which tickets are authorized and what stays with
the user; this file is the procedure. Planning branches and draft PRs follow
[planning delivery](planning-delivery.md) instead. The decisions behind it are
in `.scratch/version-control/spec.md`.

## Steps

1. **Branch.** One short-lived branch per ticket, started from a freshly fetched
   `origin/main`. Sequential tickets use the primary checkout. When another
   session works at the same time, use a separate worktree (`git worktree add`),
   so branches, builds and working files stay apart.
2. **Scope.** The implementation, directly related documentation, the ticket's
   status and evidence and its review record travel together on the branch.
   Stage the ticket's paths by name; unrelated dirty files stay unstaged and
   untouched.
3. **Checkpoints.** Commit whenever progress is worth keeping. The squash merge
   turns every checkpoint into one commit on `main`.
4. **Checks.** Run the focused checks and full suite `docs/windows.md` names for
   the change; a documentation-only change runs `python scripts/check_docs.py`
   and `git diff --check`. Then run `/code-review` over
   `git diff origin/main...HEAD` plus any uncommitted work, and record the result
   in the ticket.
5. **Status.** Set `Status: done` on this branch as
   [triage-labels.md](triage-labels.md) defines it. Under each item that stays
   open, note what remains.
6. **Public-content check.** The repository is public, and a push publishes
   every checkpoint, not only the net diff. Read `git log -p origin/main..HEAD`
   for local paths (home or user directories), account and machine names, email
   addresses, secrets, private inputs, generated output and third-party material
   of unclear provenance. Run
   [`python scripts/check_public_content.py`](../../scripts/check_public_content.py)
   for the mechanical check: it scans added lines for home paths and emails and
   checks every commit's author and committer emails. It does not replace reading
   each checkpoint. Fix what you find before pushing; a commit that was never
   pushed may be amended or squashed, since nothing public changes.
   Content that is already pushed goes to the user.
7. **Push and open the PR.** `git push -u origin <branch>`, then
   `gh pr create --base main`. Later commits, such as review fixes, go to the same
   branch as fast-forward pushes. The PR title becomes the squash commit's
   subject; for the ticket that closes a roadmap claim, it is the claim
   (`docs/roadmap.md`). The body says what changed, which checks ran, what the
   review found and what stays open.
8. **`CI gate`.** It must pass, and the branch must be up to date with `main`.
   When `main` moves, merge `origin/main` into the branch and push; the squash
   discards that merge commit.
9. **Merge.** The user decides when to merge; a session merges only when the
   user asks it to. Nothing merges automatically. GitHub squash-merges and
   deletes the remote branch.
   For an approved merge, use `scripts/merge_when_green.sh <pr> <head-sha> "<subject>"`.
10. **Cleanup.** Once the squash commit is on `main`, the user retires the
    ticket's worktree and local branch, or asks a session to:
    `git worktree remove <path>`, then `git branch -D <branch>`. A squash merge
    leaves the branch unmerged in Git's view, so `-d` refuses it.

## Holds and stops

- **A red check** (local suite, a review finding, `CI gate`, the public-content
  check) holds delivery at its current step until it is green. Fix it within the
  ticket's scope and re-run. When the fix lies outside that scope, stop.
- **Unclear file scope** stops delivery: a needed change touches files that
  belong to another session or ticket, or the checkout holds dirty files of
  unknown origin.
- **A new architecture decision or roadmap change** stops delivery. It becomes
  its own planning decision, delivered as [planning delivery](planning-delivery.md)
  describes when the user asks; the ticket branch carries none. Choosing or
  re-cutting the next roadmap claim is such a change, even in the ticket that
  closes the current one.

A stop leaves the work as it is and reports to the user.
