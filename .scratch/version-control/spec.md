# Repository version control

Status: ready-for-agent
Date: 2026-09-25

## Problem Statement

The repository has useful, working development in a large dirty checkout, while its
visible commits and branch names describe older milestones rather than the current
state. The user wants the current state to be the starting point of a public GitHub
repository, without publishing the rough old commit ancestry. The present
“commit only when asked” rule protects uncommitted work but does not define where
new tickets or exploratory planning live, how commits reach `main`, or what checks
must pass. Without a coherent workflow, a future change can be mixed with unrelated
work, a provisional roadmap idea can be mistaken for accepted direction, or a
public push can expose material that was never reviewed for publication.

This is **repository Git version control**, not revision history of saved Modular
authored graphs. No Git migration, remote creation, publication, or automatic
commit authority is implied by this spec's publication.

## Solution

Establish a new public `main` consisting initially of one verified root commit
made from a deliberately selected snapshot of the current working state. Inventory
and audit the exact candidate file set before staging or publication. Prove the
candidate builds and tests cleanly in native Windows Debug and Release, and obtain
approval of the exact baseline before replacing history. The old commits do not
appear in the new public ancestry; old branch names go away only after verification
and approval. Do not deliberately garbage-collect old objects.

Thereafter, use one short-lived branch per implementation ticket and separate,
short-lived planning branches for tentative roadmap or architecture work. Agents
can make scoped checkpoint commits on authorized ticket branches after the standing
workflow is recorded; those commits are not approval to merge. Both kinds of
branch enter `main` through pull requests with a reliably reported CI gate, and
the user chooses when to merge. Squash ticket checkpoints into one accepted commit
and delete integrated branches. Use worktrees for concurrent work, not by default.
Rejected explorations remain visible as closed draft PRs, not as accepted roadmap
content. The first root commit is a one-time bootstrap exception to the future
pull-request rule.

## User Stories

1. As a maintainer, I want to see the exact files proposed for the public root commit, so that I can review what will be published.
2. As a maintainer, I want tracked and untracked work inventoried together, so that older work is not silently omitted from the new baseline.
3. As a maintainer, I want generated output and private inputs excluded deliberately, so that the public repository contains intentional project material.
4. As a maintainer, I want machine-local paths, fixtures, repaired investigation files and third-party material audited, so that public release does not expose inappropriate content or misstate its provenance.
5. As a maintainer, I want suitable specs, tickets and reproducible evidence versioned, so that project reasoning survives alongside source.
6. As a maintainer, I want clean Debug and Release verification before baseline approval, so that “working initial state” is supported by evidence.
7. As a maintainer, I want a failing baseline check diagnosed and corrected before publication, so that the root commit is not knowingly presented as green when it is not.
8. As a maintainer, I want to approve the exact baseline before its history is replaced, so that a destructive migration does not run on an inferred scope.
9. As a maintainer, I want the public `main` to begin with one root commit representing that approved state, so that rough old commits do not form its ancestry.
10. As a maintainer, I want the new root verified before old branch names are removed, so that branch cleanup cannot precede a confirmed replacement.
11. As a maintainer, I want old Git objects left to ordinary retention rather than deliberately garbage-collected, so that the cleanup does not make an avoidable irreversible loss.
12. As a maintainer, I want the public GitHub repository created and pushed only after the content audit, so that an accidental public push is not the review process.
13. As a ticket implementer, I want a short-lived branch for each agreed ticket, so that its code and evidence have an attributable boundary.
14. As a ticket implementer, I want scoped checkpoint commits on my branch, so that progress can be recovered without treating every checkpoint as accepted on `main`.
15. As a ticket implementer, I want directly related docs, ticket status and review evidence to travel with the implementation, so that its PR explains what was delivered and verified.
16. As a maintainer, I want unrelated dirty work excluded from ticket commits and reviews, so that one PR cannot quietly absorb another task.
17. As a maintainer, I want a new architecture ruling or roadmap change to stop an implementation ticket for a separate decision, so that project direction is not decided incidentally inside a code PR.
18. As a maintainer, I want sequential tickets to use the primary checkout, so that isolation does not create unnecessary worktrees.
19. As a maintainer, I want concurrent tickets isolated in separate worktrees when needed, so that their branches, builds and working files do not interfere.
20. As a maintainer, I want integrated ticket branches deleted, so that one branch per ticket does not become an ever-growing branch list.
21. As a planner, I want uncertain roadmap and architecture proposals developed on short-lived planning branches and draft PRs, so that exploration is clearly provisional.
22. As a planner, I want rejected proposals preserved as closed PRs with reasons, so that useful exploration is not lost or mistaken for an accepted decision.
23. As a planner, I want only accepted direction promoted to `main`, so that it remains the sole stable source of project direction.
24. As a planner, I want independent planning changes integrated without waiting for an unrelated code ticket, so that the roadmap can evolve when a decision is settled.
25. As a maintainer, I want both ticket and planning changes reviewed through PRs, so that direct pushes do not bypass the agreed workflow.
26. As a maintainer, I want one accepted ticket represented by one squash commit on `main`, so that checkpoint history does not obscure the project's accepted progression.
27. As a maintainer, I want to decide when a reviewed, passing PR is merged, so that green CI does not silently promote a proposal.
28. As a sole maintainer, I want branch protection that blocks routine direct and force pushes without requiring an impossible self-review approval, so that the policy is enforceable and usable.
29. As a contributor, I want every PR and `main` update to report the required CI gate consistently, so that a skipped or missing check cannot look like success.
30. As a contributor, I want documentation-only PRs checked for links, format and file scope, so that a prose change receives relevant validation without unnecessary compilation.
31. As a contributor, I want code, build and CI changes to run Windows Debug and Release builds and headless CTest including kernel purity, so that the project's actual regression suite gates integration.
32. As a contributor, I want CI configuration changes treated as code-path changes, so that a workflow edit cannot bypass the stronger checks.
33. As a contributor, I want the real GUI smoke check recorded locally until hosted execution is proven reliable, so that the PR does not claim CI coverage it does not have.
34. As a maintainer, I want a documented standing ticket-commit rule that does not apply to the initial-root migration, so that agents know both their routine authority and its boundary.

## Implementation Decisions

- **History bootstrap is exceptional:** The intended published history has exactly
  one initial root commit from the approved current snapshot, with no old commit
  ancestry. The present branch and dirty tree are not themselves permission to
  stage, overwrite, reset, delete, commit or push. Establish and obtain approval of
  the precise candidate baseline before performing the history operation. Verify
  the resulting root and tree before removing old branch names; do not explicitly
  prune old Git objects. A clean public history does not require retaining visible
  legacy branches or deliberately keeping private recovery refs indefinitely.
- **Public-content boundary:** Inventory tracked, untracked and ignored material
  relevant to the candidate baseline. Classify project records, generated files,
  personal inputs, fixtures, machine-specific references, repaired evidence and
  third-party material. Review each included file and resolve unsuitable content
  by an explicit sanitize-or-exclude decision before publication. Do not silently
  drop earlier uncommitted work or treat ignore rules as a publication audit.
- **Verification before publication:** A clean native Windows Debug and Release
  build and complete available checks are required before labeling the snapshot
  a working baseline. Diagnose failures rather than documenting a knowingly failing
  root as healthy. GitHub publication follows the audit, root verification and the
  user's approval of the exact baseline; it is not initiated by writing this spec.
- **Ticket boundary:** Implement each agreed ticket on its own short-lived branch.
  Keep code, directly related documentation, ticket status and review evidence
  together. Do not slip roadmap or architecture decisions into the ticket branch;
  pause for a separate planning decision if implementation exposes a real gap.
  Agents may create scoped checkpoint commits during authorized ticket work only
  after the standing workflow is recorded and active. Ambiguous scope or unrelated
  changes do not fall under that authority.
- **Worktrees:** Use the primary checkout for sequential ticket work. Create an
  additional worktree when concurrent work requires isolation; retire worktrees
  and source branches when their work is integrated. A branch is the unit of ticket
  history, not a permanent workspace.
- **Planning maturity:** Use short-lived planning branches and draft PRs for
  provisional roadmap and architecture work, not a long-lived planning branch.
  A closed rejected PR retains the exploration and rationale; its contents are
  not accepted project direction. Accepted independent planning changes get a
  planning PR rather than waiting to piggyback on a ticket. `main` is the stable
  view of accepted decisions.
- **Integration:** After the bootstrap, all ticket and planning changes use PRs.
  Reviewed ticket branches can contain multiple checkpoints, but merge by squash
  to a single accepted commit on `main`. The user selects when a reviewed PR with
  passing required checks merges, then its source branch is deleted. Protect
  `main` against routine direct and force pushes; do not require a reviewer
  approval that a sole maintainer cannot provide on their own PR. There is no
  automatic merge merely because CI passes.
- **CI contract:** A consistently reported required check runs for PRs and `main`.
  Documentation-only changes validate links, formatting and changed-file scope.
  Code, build-system and CI-definition changes run native Windows Debug and
  Release builds plus headless CTest, including kernel purity. Changes to CI
  itself must not qualify as documentation-only. Avoid a path-filter design where
  the required check is silently absent or indefinitely pending. The Windows
  app test opens a real window and is not asserted to be headless; keep a local
  GUI smoke result as evidence until the hosted runner has demonstrated reliable
  execution. No automated release or deployment is part of this workflow.
- **Instruction transition:** The current repo rule remains “commit only when
  asked” until the approved workflow explicitly updates both identical root
  instruction files. The eventual standing rule applies to scoped checkpoint
  commits on authorized ticket branches, not to the initial-root operation,
  arbitrary planning commits, unrelated working-tree edits, merging or pushing.
  Do not apply it retrospectively to this dirty checkout.
- **Local tracker:** Project specs, tickets and relevant review notes remain
  versioned local Markdown rather than being silently migrated to GitHub Issues.
  GitHub PRs provide integration and provisional-planning review, not a second
  source of truth for accepted roadmap content.

## Testing Decisions

The user confirmed the testing boundary on 2026-09-25:

- **Primary acceptance seam:** Exercise real Git and GitHub behavior, not a new
  harness around implementation details. Inspect the candidate root commit's
  ancestry and selected tree, then exercise a representative PR to verify the
  required check reports, protection rejects a bypass and the user can merge an
  accepted PR. Observe branch deletion and the squash result. Avoid testing only
  configuration text while claiming the remote policy is enforced.
- **Baseline checks:** Run clean native Windows Debug and Release builds and the
  existing complete CTest suite before root approval; report exact passes,
  failures and any checks not run. The existing test suite and kernel-purity
  check are prior art. The real GUI test may need a desktop and must be reported
  separately if it cannot run in the unattended environment.
- **CI behavior:** Prove a documentation-only PR reports a meaningful required
  check without compiling unnecessarily; prove code or CI changes trigger Debug
  and Release headless tests. A failed check must block integration, and a skipped
  job must not masquerade as a green required check. Inspect actual check results
  on GitHub, not just a local workflow parse.
- **Public behavior over internals:** Assert visible ancestry, selected file tree,
  PR check outcomes, merge protection and local GUI evidence, rather than internal
  shell call counts or incidental script structure. Use the public CMake/CTest
  entry points already exercised in this repo. Add only the minimum validation
  tooling needed to make the selected checks reliable.
- **Public-content audit:** Before a public push, inspect the precise candidate
  file list and contents for unintended private inputs, local identifiers,
  generated outputs and third-party provenance. A clean test run is not a
  substitute for this audit. Publication is outward-facing and requires the
  user's explicit approval of the exact baseline.

## Out of Scope

- Version history, branching, collaboration or merging for saved Modular graphs.
- Rewriting or deleting Git history, configuring GitHub, pushing the baseline,
  creating branches or activating standing commit authority **in the spec phase**.
- Moving the local issue tracker to GitHub Issues.
- A permanent planning branch, permanent per-ticket worktrees or keeping all
  checkpoint commits on `main`.
- Running the real GUI test as a required hosted CI check without first proving
  that its desktop requirements work reliably there.
- Automated releases, deployment, multi-maintainer review requirements or
  unsupervised merge to `main`.
- Proactively garbage-collecting old Git objects to erase the former local
  history. The intended public history still contains only the new root.

## Further Notes

The decision source is the completed repository-version-control discussion.
The current checkout is on an older-named feature branch, `main` points to an
earlier milestone, and there is no remote configured as of this spec; substantial
tracked and untracked work must be classified rather than assumed clean. Some
existing documentation records historical verification or machine-local paths;
these are audit subjects, not proof that a clean baseline check just passed.
The current Git commit restriction remains active until deliberately reconciled
with the future workflow in both root agent instruction files. This spec is ready
for ticket breakdown, not permission to implement any of its steps.
