# Matt Pocock workflow review and persistent agreement

Reviewed 2026-09-12 against the installed skill files and this conversation.
The essential operational rules are inlined in both root `AGENTS.md` and
`CLAUDE.md` for startup discovery. Detailed project decisions and conventions now
live in `docs/project-foundations.md`; this document records workflow evidence and
distinctions. It is not a feature spec or implementation ticket.

The follow-up audit covers **all 37 installed Matt Pocock skills**, including
wayfinder and all eight installed skills under `in-progress`. The complete
[source-linked catalog](matt-skills-catalog.md) records each workflow, invocation
mode, output and stopping condition. Coverage comes from the installation manifest,
not the router's incomplete list; the extra `find-skills` installation is from
Vercel. Supporting workflow documents/templates and Codex invocation metadata were
also checked. This describes the installed versions, not uninstalled upstream work.

The links below point to public upstream skill sources; the September 2026 review
examined a local installation, which may differ from upstream after that date.
The 12 upstream skill/document paths linked below were checked against the public
repository tree on 2026-09-26.

## What went wrong

1. During `/grill-with-docs`, the assistant read "if it is easy, do it" as an immediate
   switch to implementation. The user clarified that they were still discussing scope.
   The assistant wrote node code, tests, and a CLI without completing that phase.
2. After acknowledging the mistake, the assistant created a draft `spec.md` without
   a `/to-spec` invocation. Calling it a draft did not make it a grilling artifact.
3. The assistant later summarized grilling as five recommended defaults and a blanket
   question. The actual skill requires a decision tree, dependency-aware rounds,
   numbered questions with recommendations, user answers, and a recomputed frontier.
4. Workflow summaries omitted testing-boundary agreement in `/to-spec`, approval of
   the breakdown in `/to-tickets`, and the actual TDD/review process for implementation.
5. Codex's project instructions contained only a request to read `CLAUDE.md`. That
   exposed a pointer, not the project rules themselves, in the initial instructions.
6. The first repair covered the main feature flow but reduced wayfinder to a sentence
   and did not inventory every installed skill. It also made the spec/ticket gate
   too broad: wayfinder owns decision tickets, loop-me writes workflow specs during
   its interview, and triage can produce an agreed agent brief. Those are explicit
   outputs of their own invoked workflows.

Successful compilation and tests did not establish either authorization for those
phase changes or acceptance of the assistant's product decisions.

## Skill requirements and the user's additional rule

The installed [ask-matt router](https://github.com/mattpocock/skills/blob/main/skills/engineering/ask-matt/SKILL.md)
describes a route. Its main phase skills have `disable-model-invocation: true`.
The user explicitly clarified that phase transitions are theirs to request. This
clarification is recorded as a project working agreement, not attributed to an
imaginary sentence in every skill.

An explicit phase command or an unambiguous request for its deliverable changes
phase. Generic acknowledgements, "continue", and answers to scope questions do not.
An explicit authorization covering several phases remains valid for those phases;
do not demand the same permission again. Within the current phase, perform the
authorized work rather than asking for routine implementation choices or file edits.

The user's request to review and persist this workflow authorizes these documentation
changes. It does not switch the GUI feature out of grilling.

Apply the artifact gate to the selected workflow, not to every file containing
the word "spec" or "ticket". In the current feature flow, wait for to-spec and
to-tickets. An explicitly invoked wayfinder or loop-me instead follows its own
artifact rules. That distinction does not permit the agent to switch workflows
on its own. The installed user-invocation flags agree with Codex's corresponding
`allow_implicit_invocation: false` settings.

## Main flow, with actual completion conditions

| Phase | What to do | Completion / transition |
| --- | --- | --- |
| `/ask-matt` | Choose the appropriate route for the situation | Explain the next applicable phase; routing does not execute later skills |
| `/grill-with-docs` | Apply both grilling and domain-modeling; investigate facts, ask decision rounds, retain resolved domain knowledge | Empty frontier with no silently assumed branch, followed by the user's confirmation of shared understanding |
| `/to-spec` | Synthesize the existing discussion and codebase knowledge using the skill's template; check proposed public testing boundaries with the user | Publish the spec to the configured tracker, marked `ready-for-agent`; phase must have been requested |
| `/to-tickets` | Propose independently verifiable vertical slices and genuine blockers; iterate with the user | Publish only the agreed breakdown, one ticket per file, blockers first |
| `/implement` | Work the requested ticket at the agreed test boundaries; use TDD where possible, then two-axis code review | Report implemented behavior, validation, review findings, and remaining limitations; commit only when the user requests it |

For a multi-session build, keep the route through spec and tickets. For an explicitly
chosen small single-session task, the router permits grilling -> implementation.
Do not force every separate documentation correction through a feature-build flow.

### Grilling is an interview with state

The [grill-with-docs wrapper](https://github.com/mattpocock/skills/blob/main/skills/engineering/grill-with-docs/SKILL.md)
delegates to [grilling](https://github.com/mattpocock/skills/blob/main/skills/productivity/grilling/SKILL.md) and
[domain-modeling](https://github.com/mattpocock/skills/blob/main/skills/engineering/domain-modeling/SKILL.md).

The grilling skill says: "Ask the whole frontier in one round: number each question
and give your recommended answer." It also says: "Do not act on it until the user
confirms you have reached a shared understanding."

Maintain the tree in discussion notes, recording which answers unlock which later
questions. Ask all questions that are ready now, with concrete scenarios and clear
recommendations. Investigate factual uncertainties through bounded subagents when
needed, as the skill directs. Continue independent questions while they work.
Record user decisions separately from recommendations and update the tree after
each answer. Do not ask the user to supply facts available in the repository.

Use the skill's question presentation when supported by the conversation surface:

```text
❓ Q1 — Question title: concrete decision/scenario
➡️ Recommended answer and its reason
```

A short answer accepting the workflow is not acceptance of every proposed feature.
Keep questions relevant to the selected milestone; revisiting every speculative
roadmap ticket is not what exhausting this feature's decision tree means.

Domain-modeling actively checks words and claims against the glossary and code.
Record resolved terms promptly in `CONTEXT.md`, as a glossary without implementation
detail. Offer an ADR only when all three conditions hold: hard to reverse, surprising
without context, and a real trade-off. Ordinary UI defaults do not automatically
require ADRs. Discussion notes retain proposals and the tree; they are not specs.

### Spec and ticket details that were previously skipped

[to-spec](https://github.com/mattpocock/skills/blob/main/skills/engineering/to-spec/SKILL.md) is synthesis, not a renewed
product interview. Its explicit interaction is to check the testing seams with the
user. Prefer an existing, high public boundary; reuse a prior agreement rather than
asking again. Its template includes Problem Statement, Solution, User Stories,
Implementation Decisions, Testing Decisions, Out of Scope, and Further Notes.
Do not silently invent requirements to turn an unfinished interview into a ready spec.

[to-tickets](https://github.com/mattpocock/skills/blob/main/skills/engineering/to-tickets/SKILL.md) calls for vertical
tracer-bullet slices, not a backend phase followed by a GUI phase merely because
those are folders. Each ticket must be independently demoable or verifiable and
fit a fresh context window. Present titles, blockers, and end-to-end deliveries;
get agreement on granularity and dependencies before publishing. The skill defines
an expand-contract exception for wide mechanical refactors; use it only when the
change actually needs it. Do not close or modify the parent issue while publishing.

The tracker is already configured in [issue-tracker.md](issue-tracker.md): local
Markdown, `.scratch/<feature>/spec.md`, one file per ticket under `issues/`, explicit
blocking edges and triage status. Existing [triage labels](triage-labels.md) and
[domain layout](domain.md) satisfy the setup prerequisites; there is no need to
re-run setup or ask the same setup questions.

### Implementation and review

[implement](https://github.com/mattpocock/skills/blob/main/skills/engineering/implement/SKILL.md) calls for
[TDD](https://github.com/mattpocock/skills/blob/main/skills/engineering/tdd/SKILL.md) where possible at pre-agreed seams,
regular focused checks, a full suite at the end, and
[code-review](https://github.com/mattpocock/skills/blob/main/skills/engineering/code-review/SKILL.md) afterward.

TDD requires a genuinely failing behavioral test before its implementation, one
vertical slice at a time. Public behavior is the target; internal mocks, expected
values derived from the implementation, and writing all tests in a batch miss that
contract. The installed skill places refactoring at review rather than inside its
red-green cycle. Existing prematurely written tests must not be described as evidence
that TDD was followed.

Code review runs independent Standards and Spec subagents and reports the axes
separately. Pin a valid baseline and the actual change scope. The installed default
`git diff <fixed-point>...HEAD` excludes uncommitted work; this dirty checkout needs
an explicitly scoped comparison that includes ticket changes before claiming they
were reviewed. Never commit unrelated work just to make that default diff nonempty.
If the review cannot cover the actual changes, state that limitation.

`implement` says to commit. This repository says to commit only when the user asks;
the repository/user rule overrides the generic skill instruction.

## Context and other routes

The router asks to retain the discussion through grilling, spec, and tickets, then
use fresh implementation context per ticket. Its
[phase-boundary decision tree](https://github.com/mattpocock/skills/blob/main/skills/engineering/ask-matt/PHASE-BOUNDARIES.md)
considers Continue first, Clear when the context is irrelevant, Handoff for transfer,
a subagent for bounded independent work, and Compact as the fallback. This is a
boundary decision; avoid deliberately discarding relevant context mid-phase. The
harness may compact automatically, so persist decisions and phase state without
claiming the assistant can prevent it.

Use a prototype detour when a concrete question needs runnable evidence, with handoff
out and back when the work changes session/directory. It remains an explicitly scoped
prototype, not production implementation authorized by calling it an experiment.
Wayfinder is for a large effort whose uncertainty spans sessions. Triage handles
incoming raw issues, not tickets just generated by `/to-tickets`. Diagnosis first
establishes a working reproducer; research supplies sourced facts. Architecture
improvement presents candidates before the user selects one to design. These are
different entry points, not mandatory extra stages for every feature.

### Wayfinder is a distinct planning workflow

The [installed skill](https://github.com/mattpocock/skills/blob/main/skills/engineering/wayfinder/SKILL.md) starts by
naming the destination. It distinguishes precise decision questions (ticket them,
even if blocked), in-scope uncertainty too vague to ticket (fog), and work beyond
that destination (out of scope). Old futuristic work does not become a prerequisite
just because a ticket exists. The map is an index of named decision links, not a
duplicate store of every ticket's details.

Chart mode grills the destination and breadth-first frontier, creates the map and
sharp tickets, wires dependencies in a second pass, launches research agents, and
stops without hand-resolving tickets. If there is no fog and the journey is small,
it stops without forcing a map. Work mode claims a named or first eligible frontier
ticket before work, uses its type-specific skills, records the answer and map
pointer, and graduates newly precise fog. At most one ticket is resolved per
session except research; human-in-the-loop tickets require actual human answers.

Research, prototype, grilling and prerequisite task are distinct ticket types.
Closing something beyond the destination puts it under Out of scope, not Decisions
so far. Planning is the default; only a user-chosen scope written into Notes can
include execution. A clear route normally hands off to spec/tickets or an agreed
small-task route, without automatically invoking either. See the
[full mechanics](matt-skills-catalog.md#wayfinder-mechanics-that-must-survive-handoffs)
and [local operations](issue-tracker.md#wayfinding-operations).

The follow-up also records workflows absent from the earlier review: whole-spec
parallel implementation, recurring-work interviews, retrospectives, teaching,
questionnaires, writing fragments/beats/shape, and specialized Claude/TS/course
setup. Their intended outputs and interactions differ; none is automatically
activated by this audit or by `/ask-matt` recommending it.

## Instruction persistence and the explicit setup exception

The user asked for self-contained startup instructions for Codex and Claude Code.
Root `AGENTS.md` and `CLAUDE.md` contain identical essential rules; longer project
contracts are linked from both to `docs/project-foundations.md`. Both files must
remain byte-identical. This is repository guidance, not account-wide memory.

The generic [setup skill](https://github.com/mattpocock/skills/blob/main/skills/engineering/setup-matt-pocock-skills/SKILL.md)
prefers editing only the existing instruction file. The user's explicit request for
self-contained Codex instructions overrides that default. The writing-for-agents
preference for avoiding duplicate sources is handled by maintaining identical copies;
it does not override the user's request to include the content directly.

Official [Codex instruction discovery](https://learn.chatgpt.com/docs/agent-configuration/agents-md)
loads selected instruction files at run startup, with a default combined budget of
32 KiB. A textual pointer does not itself inline another document. Keep the root copy
under that default budget. File checks do not prove that an existing session has
reloaded its startup instructions.

## State at the 2026-09-12 review (historical)

- The active feature phase was `/grill-with-docs` then; it later completed, and
  tickets 01–05 were implemented. See the current
  [planning notes](../../.scratch/graph-editor/planning.md).
- The premature spec is preserved under the feature's `archive/`, outside the active
  `spec.md` location. It was withdrawn, not accepted as a feature spec.
- Earlier identity work and provisional node-I/O implementation were uncommitted
  and untouched by that documentation repair.

Historical follow-up validation, 2026-09-12 (before the 2026-09-24
condensation): catalog rows and inline routes covered all 37 manifest entries;
invocation modes matched the installed sources. AGENTS and CLAUDE were then
byte-identical at 32,030 bytes each, below the 32 KiB default. The Project context
section and everything below it were unchanged from the pre-audit copy. Hashes
matched for all 39 protected source/test/build/domain files, and all 61 local
links in the catalog/review resolved. `git diff --check` passed. Those checks
were not rerun by this historical note; the application suite was not rerun.
