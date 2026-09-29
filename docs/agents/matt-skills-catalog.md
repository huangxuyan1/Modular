# Complete installed Matt Pocock skill workflows

Audited 2026-09-12. Scope: **all 37 installed skills attributed to
`mattpocock/skills`** in
the installation manifest in the reviewer's local skills directory, including
their supporting workflow documents and bundled templates. This is an audit of
the installed versions, not a claim about every future or uninstalled upstream
skill. The manifest records these installations on 2026-09-07.

The manifest groups them as 18 engineering, 7 productivity, 8 in-progress, and
4 miscellaneous skills. `in-progress` is the source directory classification,
not an inferred quality rating. The additional installed
[find-skills](https://github.com/vercel-labs/skills) comes from
`vercel-labs/skills`, not Matt Pocock. It discovers and recommends skills; this
audit did not install or update anything.

The tables below cover every Matt skill once. **User** means the installed source
sets `disable-model-invocation: true`; its `agents/openai.yaml` also disables
implicit invocation. **Model/user** means it can be reached by a relevant request
or as another invoked skill's component. Capability does not grant permission to
cross the user's phase boundaries. The metadata agreed for all 37 skills.

Read the installed `SKILL.md` when applying one; these upstream links are
references, not a substitute for the reviewed installation. This catalog preserves routing,
outputs, interactions, and stopping conditions; it does not replace the source.
Reading a skill during this audit does not activate it, launch its agents, or
authorize its deliverables. See [the workflow agreement](matt-pocock-workflow.md)
for the user's overrides and [the local tracker](issue-tracker.md) for operations.

Public skill links below point to upstream source locations, not a frozen copy of
the locally installed September 2026 versions reviewed here. Consult the active
installation or session catalog before invoking a skill. All 37 upstream
paths linked here were checked against the public repository tree on 2026-09-26.

## Engineering: 18

| Skill | Invocation | Workflow, output, and stopping point |
| --- | --- | --- |
| [ask-matt](https://github.com/mattpocock/skills/blob/main/skills/engineering/ask-matt/SKILL.md) | User | Diagnose the situation and recommend the next skill. Scoped idea: grilling, then spec/tickets for a multi-session build, then implementation. Large uncertain effort: wayfinder. Incoming raw request: triage. Reproducible failure: diagnosis. A runnable design question can detour through prototype. Routing suggests user-invoked phases; it does not fire them. Its list is not the complete installed inventory. |
| [code-review](https://github.com/mattpocock/skills/blob/main/skills/engineering/code-review/SKILL.md) | Model/user | Establish a fixed comparison point and nonempty scope, locate the originating spec, then run independent Standards and Spec subagents. Standards use repo rules and smell heuristics; Spec uses the actual requested behavior. Report both axes separately, each review bounded to 400 words. Missing baseline/spec requires clarification or an explicit skipped axis. The source's HEAD comparison excludes dirty work; scope this checkout's actual changes explicitly. |
| [codebase-design](https://github.com/mattpocock/skills/blob/main/skills/engineering/codebase-design/SKILL.md) | Model/user | Shared vocabulary: module, interface, implementation, depth, seam, adapter, leverage, locality. An interface includes invariants, ordering and error behavior, not just methods. Use the deletion test and real dependency needs to assess depth. DEEPENING distinguishes in-process, local substitutes, owned remote services and true external dependencies. DESIGN-IT-TWICE explores a selected candidate through 3+ contrasting agent designs, then compares and recommends; the vocabulary alone does not authorize refactoring. |
| [diagnosing-bugs](https://github.com/mattpocock/skills/blob/main/skills/engineering/diagnosing-bugs/SKILL.md) | Model/user | First execute a tight loop that visibly reproduces the user's exact symptom; only then hypothesize. Minimize the reproducer, show 3-5 ranked falsifiable hypotheses, and vary one thing per probe. Performance work needs measured baselines/profiles. Write the regression before fixing at the appropriate real behavioral seam. Finish by rerunning the original scenario and regression and removing diagnostic scaffolding. If no usable loop exists, report attempts and obtain the missing access/evidence instead of guessing. |
| [domain-modeling](https://github.com/mattpocock/skills/blob/main/skills/engineering/domain-modeling/SKILL.md) | Model/user | Cross-check terminology against code and existing glossary/ADRs, expose contradictions, and record resolved terms as discussion proceeds. CONTEXT is a concise domain glossary with preferred terms and avoided aliases. Offer an ADR only for a hard-to-reverse, surprising decision involving a real trade-off. Create documents lazily when something warrants them. Neither code details nor unresolved recommendations belong in the glossary. |
| [grill-with-docs](https://github.com/mattpocock/skills/blob/main/skills/engineering/grill-with-docs/SKILL.md) | User | Wrapper that applies BOTH grilling and domain-modeling. Explore a decision tree, investigate facts, ask dependency-aware rounds with recommendations, wait for actual answers, and update durable domain knowledge. Completion requires the frontier to be exhausted and the user to confirm shared understanding. It does not synthesize a feature spec or begin implementation by itself. |
| [implement](https://github.com/mattpocock/skills/blob/main/skills/engineering/implement/SKILL.md) | User | Implement the agreed task using TDD where possible at pre-agreed seams; run focused checks regularly and the full suite at the end, then code-review. The source says to commit afterward; Modular's explicit rule is commit only when asked. Keep earlier uncommitted work intact. This is the per-task route, distinct from implement-spec's whole-DAG orchestration. |
| [improve-codebase-architecture](https://github.com/mattpocock/skills/blob/main/skills/engineering/improve-codebase-architecture/SKILL.md) | User | Apply codebase-design and inspect domain docs and real change hotspots, using an exploration subagent. Present a temporary HTML report of deepening candidates with before/after diagrams and a strongest recommendation. Wait for the user's candidate choice before designing interfaces. Then grill plus domain-modeling; use Design It Twice when exploring alternative interfaces. A survey is not authorization to implement its recommendations. |
| [prototype](https://github.com/mattpocock/skills/blob/main/skills/engineering/prototype/SKILL.md) | Model/user | State the concrete question first. Logic branch: one shareable HTML file, portable logic, readable state, free play and guided walkthroughs. UI branch: usually three structurally different variants on an existing page, switchable by URL and controls. Use in-memory/stub mutations; no prototype tests or speculative abstraction. Obtain human reaction, preserve the artifact as primary evidence, and capture the answer. Source cleanup includes integrating the winner and a throwaway branch; here production work/commits remain bounded by user authorization. |
| [research](https://github.com/mattpocock/skills/blob/main/skills/engineering/research/SKILL.md) | Model/user | Give a bounded question to a background agent; investigate high-trust primary sources and trace claims to original evidence. Capture a cited Markdown note using the repo's existing notes convention. Findings feed decisions; they do not become user preferences or authorize implementation. |
| [resolving-merge-conflicts](https://github.com/mattpocock/skills/blob/main/skills/engineering/resolving-merge-conflicts/SKILL.md) | Model/user | Read merge/rebase state and the original commits/issues behind both sides. Resolve each hunk while preserving both intents where possible; when incompatible, follow the merge goal and record the trade-off without inventing behavior. Run project checks and finish the merge/rebase. The source defaults to staging everything and committing; here scope staging to the merge and honor actual user authorization for completion, preserving unrelated dirty work. |
| [setup-matt-pocock-skills](https://github.com/mattpocock/skills/blob/main/skills/engineering/setup-matt-pocock-skills/SKILL.md) | User | Inspect existing project instructions, tracker, triage labels and domain layout first. Discuss choices where needed, draft the setup, obtain agreement, then write without replacing surrounding rules. Supplies local/GitHub/GitLab tracker templates. Modular already uses local Markdown and single-context domain docs. Its default preference for only an existing CLAUDE file is overridden by the user's explicit request for complete, identical AGENTS and CLAUDE copies. |
| [tdd](https://github.com/mattpocock/skills/blob/main/skills/engineering/tdd/SKILL.md) | Model/user | Agree public test seams before writing tests. For one behavior at a time, prove a failing test, then add the minimum implementation to pass. Do not batch all tests first. Assert independently known observable outcomes through real interfaces; mock external boundaries, not internal collaborators. Refactoring belongs to the later review stage in this installed version. Pre-existing tests do not retroactively prove TDD. |
| [to-spec](https://github.com/mattpocock/skills/blob/main/skills/engineering/to-spec/SKILL.md) | User | Synthesize an already explored idea; inspect code as needed, but do not restart the product interview. Propose the highest useful existing public testing seams and check with the user unless already agreed. Publish the full template: problem, solution, user stories, implementation decisions, testing decisions, out of scope, further notes. Avoid brittle file paths/line numbers. Local output is `.scratch/<feature>/spec.md`, ready-for-agent. |
| [to-tickets](https://github.com/mattpocock/skills/blob/main/skills/engineering/to-tickets/SKILL.md) | User | Read the agreed spec and code, identify necessary prefactoring, and propose narrow end-to-end tracer-bullet slices with explicit blockers. Ask the user to review granularity, dependencies and possible merges/splits before publishing one ready ticket per file, blockers first. Leave the parent intact. Wide mechanical refactors may use expand/migrate/contract with bounded batches; that exception is not permission to default to horizontal layer tickets. |
| [triage](https://github.com/mattpocock/skills/blob/main/skills/engineering/triage/SKILL.md) | User | Select incoming raw issues, or external PRs if the tracker enables that surface; an explicitly named PR is inspectable regardless of author. Read history/diff, check code and prior rejections, recommend category/state and get direction. Verify behavior before grilling; use grilling/domain-modeling when requirements remain unclear. Publish a behavioral agent brief, focused information request, or closure as agreed. Prefix tracker messages with the skill's AI disclosure. Only rejected enhancements enter the rejection knowledge base; already-implemented requests do not. Generated ready tickets skip triage. |
| [wayfinder](https://github.com/mattpocock/skills/blob/main/skills/engineering/wayfinder/SKILL.md) | User | A shared map for large uncertain work across sessions. Name the destination; distinguish sharp questions, in-scope fog and work beyond that destination. Chart decision tickets, then claim and resolve one per session, except parallel research. Charting stops after mapping and launching research. Planning is the default; explicitly agreed Notes can include execution. Full mechanics below. |
| [wizard](https://github.com/mattpocock/skills/blob/main/skills/engineering/wizard/SKILL.md) | Model/user | Generate a Bash wizard for steps only the human can perform. Establish prerequisites, values, sources/destinations and confirmations, verify exact dashboard instructions, then copy the supplied library unchanged and author stages. Open each URL before asking for input, conceal secrets and store only where required. Validate with bash syntax checking, shellcheck where available and a static trace; hand the script to the user instead of running the interactive procedure. Check Bash availability on Windows. |

## Productivity: 7

| Skill | Invocation | Workflow, output, and stopping point |
| --- | --- | --- |
| [grill-me](https://github.com/mattpocock/skills/blob/main/skills/productivity/grill-me/SKILL.md) | User | Stateless wrapper for grilling alone. Use for an idea or decision without repository domain-document maintenance. It has the same interview completion condition as grilling, not an automatic implementation handoff. |
| [grilling](https://github.com/mattpocock/skills/blob/main/skills/productivity/grilling/SKILL.md) | Model/user | Maintain the design tree and ask the entire currently ready frontier in a numbered round with clear questions and recommendations. Investigate unknown facts through bounded agents while continuing independent questions. Wait for answers, update dependencies, repeat. Stop only when all branches are resolved and the user confirms shared understanding. Never answer the human's side yourself. |
| [handoff](https://github.com/mattpocock/skills/blob/main/skills/productivity/handoff/SKILL.md) | User | Write a portable Markdown handoff in OS temp, outside the repo. Explain task, state, decisions, open work and suggested skills; point to existing artifacts rather than copying them wholesale. Omit secrets and unnecessary personal information. This transfers context, not permissions or completion status invented by the author. |
| [teach](https://github.com/mattpocock/skills/blob/main/skills/productivity/teach/SKILL.md) | User | Establish a concrete learning mission and prior knowledge. Maintain trusted resources, learning records and preferences; confirm changes to the mission. Build short lessons/exercises and compact references with readable HTML and reusable assets. Adapt difficulty, retrieval and feedback to the learner; record demonstrated understanding rather than mere exposure. Surface useful communities while respecting opt-outs. The bundled formats define mission, resources, learning records and glossary. |
| [to-questionnaire](https://github.com/mattpocock/skills/blob/main/skills/productivity/to-questionnaire/SKILL.md) | User | Establish the recipient's role/expertise/relationship first, then what decisions or facts are needed. Fill remaining gaps and write `to-questionnaire-<slug>.md` with purpose, context, answering guidance, grouped questions and answer space. Keep one idea per question and put the most important first. The user can send it; writing the document is not authorization for the agent to message someone. Answers can return to grilling or spec synthesis. |
| [wait-what](https://github.com/mattpocock/skills/blob/main/skills/productivity/wait-what/SKILL.md) | User | Re-explain the previous message plainly, with necessary context and the project's established vocabulary. This is a communication repair at the current phase, not a new planning or execution phase. |
| [writing-for-agents](https://github.com/mattpocock/skills/blob/main/skills/productivity/writing-for-agents/SKILL.md) | Model/user | Write checkable ordered procedures and precise context pointers. Balance always-loaded context against the effort of discovering references; inline material when pointers fail the use case. Put supporting detail near its trigger and split phases when later instructions cause premature action. SKILL-MECHANICS distinguishes user/model invocation and routers. The user's request for complete startup instructions wins over generic duplication/minimal-context preferences. |

## In-progress: 8

| Skill | Invocation | Workflow, output, and stopping point |
| --- | --- | --- |
| [claude-handoff](https://github.com/mattpocock/skills/blob/main/skills/in-progress/claude-handoff/SKILL.md) | User | Prepare a portable task summary and launch an actual named Claude background session in the current directory via `claude --bg --name ...`. Unlike handoff, its artifact is the running session, not a Markdown file. It depends on the Claude harness being available and authorized; do not pretend it is a Codex session command. |
| [implement-spec](https://github.com/mattpocock/skills/blob/main/skills/in-progress/implement-spec/SKILL.md) | User | Coordinate an entire agreed spec and ticket dependency graph into one PR. Establish the branch/draft PR, give implementers isolated worktrees, let a merger integrate completed tickets, and refresh the unblocked frontier as work lands. Keep shared context concise and referenced. Finish with overall review/fixes, ready the PR and clean up task worktrees. This is explicit parallel execution, not an alias for implement or a default route for this local-only tracker. |
| [loop-me](https://github.com/mattpocock/skills/blob/main/skills/in-progress/loop-me/SKILL.md) | User | Stateful grilling about recurring work and what can be delegated. Learn the user's world before proposing loops and retain relevant NOTES. As decisions resolve, create/edit the intended `workflows/*.md` workflow specs directly. Define triggers, outcomes and human checkpoints; perform as much useful work as possible before a decision-ready human review. No mandatory AI, schedule or checkpoint. Done when an implementer can build each workflow without new questions; implementation is separate. |
| [retro](https://github.com/mattpocock/skills/blob/main/skills/in-progress/retro/SKILL.md) | User | Read the requested session, or the current session by default, and apply writing-for-agents. Diagnose recurring environmental friction such as navigation, checks, standards discovery and poor tool information. Recommend concrete improvements ranked by severity. The default deliverable is recommendations, not silently applied changes. Here the user separately authorized persistent documentation updates. |
| [setup-ts-deep-modules](https://github.com/mattpocock/skills/blob/main/skills/in-progress/setup-ts-deep-modules/SKILL.md) | User | Apply codebase-design to a TypeScript package layout. Inspect package manager/config, establish the package root, install/merge dependency-cruiser rules, and wire existing checks. Package-root files are entry points; implementation subfolders are private and tests use entry points. Preserve existing configuration and avoid unrelated alias/tsconfig changes. Prove legal imports pass, an illegal deep import fails, and removing it restores passing. This is not a C++ kernel convention. |
| [writing-beats](https://github.com/mattpocock/skills/blob/main/skills/in-progress/writing-beats/SKILL.md) | User | Turn a fixed fragment pile into writing one chosen beat at a time. Establish output location and audience knowledge, offer 2-3 genuinely different reachable starts, then write only the selected beat. Reread the article, offer the next 2-3 beats whose prerequisites are grounded, and repeat. Never write ahead of the user's selection; stop at a natural ending, not merely when every fragment has been used. |
| [writing-fragments](https://github.com/mattpocock/skills/blob/main/skills/in-progress/writing-fragments/SKILL.md) | User | Explore by capturing raw ideas immediately. Establish the file path if missing; maintain one H1 and fragments separated by horizontal rules, without imposing an outline or article structure. Silently append as the discussion produces material. Reread before every write to preserve concurrent user edits; rewrite existing fragments only when asked. The output is the raw pile. |
| [writing-shape](https://github.com/mattpocock/skills/blob/main/skills/in-progress/writing-shape/SKILL.md) | User | Read the complete fixed fragment pile and leave it untouched. Establish a separate article and audience knowledge. Offer 2-3 openings, agree a direction, then co-develop and save only the next agreed paragraph/block. Reread before writing, test transitions, choose appropriate prose/list/table forms and surface missing examples. The user decides when the piece is done; do not add publishing scaffolding. |

## Miscellaneous: 4

| Skill | Invocation | Workflow, output, and stopping point |
| --- | --- | --- |
| [git-guardrails-claude-code](https://github.com/mattpocock/skills/blob/main/skills/misc/git-guardrails-claude-code/SKILL.md) | Model/user | Choose project/global scope, install the bundled Bash hook and merge a PreToolUse/Bash entry into Claude settings while preserving existing hooks. Adjust patterns as requested and verify a fake dangerous-command payload is blocked. The script needs Bash and jq and uses pattern matching; it is not universal Git protection and does not configure Codex approvals. |
| [migrate-to-shoehorn](https://github.com/mattpocock/skills/blob/main/skills/misc/migrate-to-shoehorn/SKILL.md) | Model/user | Inspect TypeScript test assertions, install the dependency if needed, replace partial assertions with fromPartial and deliberately invalid data with fromAny, and type-check. Keep changes in tests, not production. Determine existing requirements from code where possible. This is a specialized migration, not a prerequisite for Modular. |
| [scaffold-exercises](https://github.com/mattpocock/skills/blob/main/skills/misc/scaffold-exercises/SKILL.md) | Model/user | Create the course's numbered sections/exercises and problem/solution/explainer variants, with useful readmes and valid links. Preserve numbering during moves, use the prescribed source stubs, and run the course linter. Its generic commit step is overridden by commit-only-when-asked; never stage unrelated work. Applicable to a course workspace, not the graph engine merely because the skill is installed. |
| [setup-pre-commit](https://github.com/mattpocock/skills/blob/main/skills/misc/setup-pre-commit/SKILL.md) | Model/user | Detect the JS package manager, install/init Husky, lint-staged and Prettier, preserve existing config, and connect existing typecheck/test scripts. If scripts are missing, omit and report rather than inventing them. Verify the staged-file formatter/hook setup. Its stage/commit default does not authorize staging this dirty C++ checkout or committing without a request. |

## Wayfinder mechanics that must survive handoffs

Wayfinder is appropriate when uncertainty is larger than one session, not merely
because an old roadmap contains many tickets. The **destination** defines what
this effort must decide; it can be a spec, a locked decision, or an explicitly
agreed change. The installed skill defaults to planning. An effort's Notes can
include execution only when that is the user's chosen scope, not as a way for the
agent to bypass the current phase.

The canonical map contains Destination, Notes, Decisions so far, Not yet specified,
and Out of scope. It is an index: one named link and gist per completed decision;
the answer itself lives in its ticket. Query open child tickets instead of copying
all their contents into the map. Refer to maps and tickets by linked names in
human-facing text, not bare numbers.

| Classification | Test | Representation |
| --- | --- | --- |
| Decision ticket | Can the question be stated precisely now? | Create a child ticket even if its answer depends on an unresolved blocker |
| Not yet specified | Is the question in scope but still too vague to state precisely? | Retain a coarse patch of fog; do not prematurely slice it into deliverables |
| Out of scope | Does this work lie beyond the chosen destination? | Record it outside the frontier; it does not automatically graduate later |

The four ticket types are research (agent-driven external knowledge), prototype
(concrete artifact plus human reaction), grilling (human conversation using both
grilling and domain-modeling), and task (manual/agent work needed before a decision
can be made). A task earns its place by enabling a decision. HITL means the human
must speak for themselves; the agent cannot resolve that ticket by accepting its
own recommendation. A ticket is intended to fit one roughly 100K-token session;
adapt execution to the actual harness limits without changing the question silently.

**Chart mode:** settle the destination through grilling/domain-modeling, then survey
the decision frontier breadth-first. If no fog remains and the journey fits one
session, stop and discuss the next route without forcing a map. Otherwise create
the map, create the questions that are already precise, and wire dependencies in a
second pass after identities exist. Launch the research subagents to retain cited
findings on research branches with ticket pointers. Then stop: charting does not
hand-resolve tickets. Research is the parallel-resolution exception; honor blockers
and the actual branch/commit permissions rather than running dependent work early.

**Work mode:** load the low-resolution map, use the named ticket or pick the first
open, unblocked, unclaimed child, and save the claim before doing the work. Read
related details on demand. Apply the ticket-type skills and the map's Notes.
Record the answer, close the ticket and add the map's named pointer. Create and
wire newly precise questions, remove their old fog entries, and revise invalidated
tickets. Resolve at most one ticket per session except research. Do not roll
automatically into the next human decision.

If an existing ticket proves to be beyond the destination, close it with a reason
and link it under Out of scope, not Decisions so far. Returning to such work means
a redrawn destination/new effort, not automatic continuation. A fully clear route
normally hands off to the appropriate spec/ticket or small-task flow. Invocation
of wayfinder does not silently invoke those user-controlled phases.

For Modular, use `.scratch/<effort>/map.md` and one child file per question under
`issues/`. Local claim/resolution/dependency conventions are in
[issue-tracker.md](issue-tracker.md). No new map or roadmap tickets were created
by this audit, and old tickets were not closed or relabelled.

## Cross-workflow rules and corrections

- **Artifact gates depend on the invoked workflow.** The feature flow waits for
  to-spec/to-tickets; wayfinder owns decision tickets; loop-me writes workflow
  specs during its interview; triage can publish its agreed agent brief. A global
  ban on every spec/ticket during planning would misread these skills.
- **User-invoked means the router recommends.** Installed invocation metadata and
  writing-for-agents/SKILL-MECHANICS agree. Generic acknowledgements or answers to
  scope questions do not invoke later phases. Explicit multi-phase authorization
  remains valid; do not ask again for steps already approved.
- **Use the appropriate entry point.** A scoped feature, foggy multi-session effort,
  incoming request, reproducible bug and architecture survey need different routes.
  The router's diagnosis-to-architecture suggestion applies when investigation
  exposes a missing seam; diagnosing-bugs does not universally run architecture work.
- **Context has its own route.** Keep grilling/spec/tickets together while the
  reasoning is relevant; prefer fresh context for implementing each ticket. At a
  boundary, Continue is considered first, Clear when irrelevant, Handoff for a
  real transfer, bounded subagents for independent work, Compact as a fallback.
  Automatic harness compaction does not reset decisions or authorization.
- **Skill defaults do not override this user's instructions.** Commit only when
  asked, preserve dirty work, and maintain identical complete AGENTS/CLAUDE copies.
  A prototype's integration/commit defaults do not bypass a planning-only scope.
- **An installed tool-specific skill is not an environment requirement.** Claude
  background sessions and hooks are Claude-specific; Bash scripts need Bash;
  TypeScript/package-manager/course skills do not become C++ setup tasks. Validate
  the relevant platform and configured tracker when the skill is actually invoked.

The GUI feature remains in grill-with-docs. This catalog is persistent workflow
memory and does not answer its outstanding product questions.
