# Repository version-control discussion

Status: grilling complete; shared understanding confirmed 2026-09-24.
This is a decision record for a later workflow/spec phase, **not an active Git policy**.
It describes the rules as they stood on 2026-09-24. No reset, branch deletion,
commit, remote creation, push or CI change was authorized by this interview. The
active workflow is the root **Commits and delivery** rule and
[ticket delivery](ticket-delivery.md), recorded by version-control ticket 04.

## Destination

Start the public GitHub repository from today's working source and project records
as **one initial root commit on `main`**, with no old commits in its ancestry.
Then use a reviewable, maintainable Git workflow for implementation tickets and
planning decisions. This concerns repository Git history, not revision history of
saved Modular graphs.

## Decisions so far

- **Bootstrap gate:** Inventory every candidate tracked and untracked file, review
  its suitability for a public repository (including machine-local paths,
  third-party material, fixtures, repaired MIDI evidence and private inputs), and
  exclude generated files. Run clean Windows Debug and Release checks; diagnose
  and fix any failure before declaring the initial baseline working. Obtain the
  user's approval of the exact baseline before the destructive history replacement.
  Create/publish the public GitHub repository only after the content audit and
  verified root commit. Remove old branch names only after the new root is verified
  and approved; do not deliberately garbage-collect old objects. Old commits are
  not ancestors of the new public `main`, nor kept as visible workflow branches.
- **Ticket branches:** One short-lived branch per implementation ticket, deleted
  after integration. Sequential work uses the primary checkout; use a separate
  worktree when concurrent work needs isolation, not by default. Scope checkpoint
  commits to the agreed ticket. Implementation, directly related documentation,
  ticket status and review evidence travel on that branch. Do not change the
  roadmap or settle a new architecture decision incidentally on a ticket branch;
  pause for an explicit planning decision when one is genuinely needed.
- **Planning maturity:** Explore uncertain roadmap/architecture proposals on
  short-lived planning branches with draft PRs, not a long-lived planning branch.
  Closed PRs retain rejected exploration and its reasons. Promote only accepted
  direction to `main` via a planning PR; avoid a drifting second source of truth.
  Independently accepted planning changes do not wait for an unrelated code ticket.
- **Integration:** Both ticket and planning changes use PRs after the one-time
  initial-root bootstrap. Ticket branches may have scoped checkpoint commits;
  squash them to one accepted commit on `main`. The user chooses when to merge
  after review and passing checks. Delete the source branch after merge. Block
  routine direct pushes and force pushes to `main`; avoid a mandatory approval rule
  that a sole maintainer cannot satisfy on their own PR.
- **CI gate:** One consistently reported required GitHub check on PRs and `main`.
  Documentation-only PRs validate links/format and changed-file scope; code,
  build and CI configuration changes run Windows Debug and Release builds and
  headless CTest, including kernel purity. Keep the real GUI smoke test as a
  recorded local check until hosted CI execution has been demonstrated reliable;
  do not claim it is CI-covered prematurely. No automatic release/deployment yet.
- **Project records:** Version agreed local specs and tickets under `.scratch/`
  and suitable reproducible investigation evidence along with source. Exclude
  generated files and private inputs after classifying the exact file set.

## Phase boundary and follow-up

The interview is complete; no design branch is silently assumed. A later explicit
request is needed to synthesize a workflow/spec and subsequently authorize Git/CI
implementation. Before agents start committing ticket branches under the proposed
standing rule, reconcile it with the currently active “commit only when asked” rule
in both byte-identical root instruction files. The initial-root operation is a
separate, explicitly reviewed and approved step, not covered by standing ticket
commit authority. Operational details (exact CI jobs, protection settings and
migration commands) belong to that later phase, not this discussion note.
