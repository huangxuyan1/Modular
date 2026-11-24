# Issue tracker: Local Markdown

Issues and specs (you may know a spec as a PRD) for this repo live as markdown files in `.scratch/`.

Versioned on `main`, these files are accepted project records. GitHub PRs are review history and do not replace them; see [planning delivery](planning-delivery.md#accepted-records).

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`
- The spec is `.scratch/<feature-slug>/spec.md`
- Implementation issues are one file per ticket at `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01` — never a single combined tickets file
- Triage state is recorded as a `Status:` line near the top of each issue file (see `triage-labels.md` for the role strings)
- Comments and conversation history append to the bottom of the file under a `## Comments` heading

## When a skill says "publish to the issue tracker"

Create a new file under `.scratch/<feature-slug>/` (creating the directory if needed).

## When a skill says "fetch the relevant ticket"

Read the file at the referenced path. The user will normally pass the path or the issue number directly.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a file with one **child** file per ticket.

- **Map**: `.scratch/<effort>/map.md` — Destination, Notes, Decisions so far, Not yet specified, and Out of scope. This is an index of named links and gists; answers stay in child tickets. Find open children by scanning the issue directory, rather than duplicating their bodies in the map.
- **Child ticket**: `.scratch/<effort>/issues/NN-<slug>.md`, numbered from `01`, with the question in the body. A `Type:` line records `research`/`prototype`/`grilling`/`task`. Local status values are `open`, `claimed`, `resolved`, and `out-of-scope` (closed). These are wayfinding states, distinct from implementation triage states.
- **Blocking**: a `Blocked by: NN, NN` line near the top. A ticket is unblocked when every listed blocker is closed (`resolved` or `out-of-scope`). When a blocker is scoped out, review dependent questions/edges: closure does not supply the missing answer.
- **Frontier**: scan `.scratch/<effort>/issues/` for files that are open, unblocked, and unclaimed; first by number wins.
- **Claim**: set `Status: claimed` and save before any work. A claim on a planning branch does not show on `main` until its PR merges, so first check the open PRs for the ticket; see [planning delivery](planning-delivery.md).
- **Resolve**: append the answer under an `## Answer` heading, set `Status: resolved`, then append a context pointer (gist + link) to the map's Decisions-so-far in `map.md`. On a planning branch these edits are provisional until its PR merges; see [planning delivery](planning-delivery.md).
- **Scope out**: record why the ticket lies beyond the destination, set `Status: out-of-scope`, and link it by name under the map's Out of scope. Do not add it to Decisions so far or leave it eligible for the frontier.
- **Create then wire**: create precise questions first, even if blocked; add blocking references in a second pass. Keep questions too vague to specify under Not yet specified. Charting then stops, except for research agents; work sessions resolve at most one ticket except research.

The explicit `open` and `out-of-scope` values above are this repo's local
representation of the skill's unclaimed-open and scoped-out-closed states. They
complete the bundled local template, which only names `claimed`/`resolved` and
omits Destination/Out of scope from its map shorthand. Existing ticket states are
not migrated automatically by a workflow audit.
