# Ticket delivery

How an authorized implementation ticket travels from its own branch to one
squash commit on `main`. The **Commits and delivery** rule in the root
`AGENTS.md`/`CLAUDE.md` grants the authority; this file is the procedure.
Planning branches and draft PRs are version-control ticket 05's and not covered
here. The decisions behind it are in `.scratch/version-control/spec.md`.

## Authority

A ticket is **authorized** when the user invokes `/implement` on it or gives
an explicit go-ahead for that ticket, directly or relayed by a session the
user is driving. For an authorized ticket the agent proceeds without asking to:

- make checkpoint commits scoped to the ticket on the ticket's own branch;
- push that branch as fast-forward pushes, after the public-content check below;
- open its PR to `main`, and push later commits to it (review fixes, catching up
  with `main`).

Everything else still needs the user's request, each time: pushing `main` or any
other branch, a force push or other history rewrite (the one-time root migration
was the only sanctioned one), merging, GitHub settings and protection, commits on
planning branches, and edits outside the ticket's scope.

## Steps

1. **Branch.** One short-lived branch per ticket, started from current
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
5. **Status.** When every checklist item is ticked with recorded evidence, set
   `Status: done` on this branch, so `main` shows it only once the work merges.
   Otherwise leave the status and note what remains under each open item.
6. **Public-content check.** The repository is public; every push publishes.
   Read the whole `git diff origin/main...HEAD` and the commit messages for local
   paths (home or user directories), account and machine names, email addresses,
   secrets, private inputs, generated output and third-party material of unclear
   provenance. Commit metadata is published too:
   `git log origin/main..HEAD --format='%an <%ae>'` should show the account's
   GitHub noreply address. Fix and recommit before pushing.
7. **Push and open the PR.** `git push -u origin <branch>`, then
   `gh pr create --base main`. The PR title becomes the squash commit's subject;
   for the ticket that closes a roadmap claim, it is the claim
   (`docs/roadmap.md`). The body says what changed, which checks ran, what the
   review found and what stays open.
8. **`CI gate`.** It must pass, and the branch must be up to date with `main`.
   When `main` moves, merge `origin/main` into the branch and push; the squash
   discards that merge commit.
9. **Merge.** The user decides, or a planning session acting on the user's
   delegation after checking the head commit and a passing `CI gate`. Nothing
   merges automatically. GitHub squash-merges and deletes the remote branch.
10. **Cleanup.** After the squash commit is on `main`, remove the ticket's
    worktree (`git worktree remove <path>`) and its local branch. A squash merge
    leaves the branch unmerged in Git's view, so deleting it takes
    `git branch -D <branch>`.

## Stops

Automatic progression holds at the current step, with work left as it is, and
the agent reports to the user, when:

- **a check is red** (local suite, a review finding, `CI gate`, the
  public-content check) and the fix lies outside the ticket's scope. A red PR is
  never offered for merge;
- **file scope is unclear**: a needed change touches files that belong to
  another session or ticket, or the checkout holds dirty files of unknown origin;
- **the ticket needs a new architecture decision or roadmap change**. That
  becomes its own planning decision; the ticket branch carries none.
