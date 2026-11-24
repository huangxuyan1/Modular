# Modular

## Start here

`AGENTS.md` holds the full startup rules for every agent. `CLAUDE.md` imports it
with `@AGENTS.md` and adds only context specific to Claude Code, so shared rules
belong here. This file is intentionally short. Before changing a layer, read the
relevant linked contract instead of treating this summary as the complete design.

- **Purpose and architecture:** Modular is a minimal, media-neutral graph language
  from which a DAW can emerge; it is not a DAW-specific engine. `kernel/` owns graph,
  compile and block execution; `nodes/` owns media primitives and device boundaries;
  `workspace/` owns shared authoring behavior; `apps/` supplies client policy.
  Do not move transport, clips, playhead or media vocabulary into the kernel.
  Do not design user-space node logic as kernel architecture or add abstractions
  beyond a concrete need. Read `docs/project-foundations.md` for the contracts.
- **Vocabulary and decisions:** `CONTEXT.md` is the canonical glossary;
  `docs/adr/` records durable trade-offs. Check both before changing behavior.
  A proposal, old code or provisional implementation is not an accepted decision.
- **Roadmap:** `docs/roadmap.md` orders the falsifiable claims; read it before kernel
  work. Latest closed: identity (ADR-0004) — graph-scoped ids, RESTRICT deletion,
  supernode faces addressed by id.
- **Current Windows GUI:** Tickets 01–05 (authoring, playback, WAV takes,
  Save/Open) are implemented; Undo/Redo and supernode import remain deferred.
  Read `.scratch/graph-editor/spec.md` for agreed scope,
  `.scratch/graph-editor/planning.md` for ticket/review evidence,
  `docs/graph-editor.md` for app behavior, `docs/node-io.md` for nodes and
  `docs/graph-persistence.md` for Save/Open.
- **Build and tests:** On Windows run
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1`.
  Read `docs/windows.md` for focused checks and environment details, and `docs/web.md`
  for the `web` presets and their CI job. Elsewhere use CMake and CTest. Do not claim
  tests pass without running them.
- **Code:** C++20, two-space indent, media-free kernel, graph-scoped NodeId/PortId,
  RESTRICT on wired primitive deletion, and block-size-invariant processing. Follow
  `docs/project-foundations.md` and the surrounding code for detailed contracts.
  Kernel authoring/compile errors throw; process paths do not.
- **Commits and delivery:** On a ticket the user authorized (an `/implement`
  invocation, or the user's explicit go-ahead for that ticket, which another
  session may relay), work without asking: make scoped checkpoint commits on the
  ticket's own branch, check every commit on it for content unfit to publish
  (local paths, account names, emails; the procedure lists the rest), push that
  branch and open its PR to `main`. Everything else needs the user's request, even
  when a skill suggests otherwise: other commits, pushing `main` or any other
  branch, rewriting pushed history (a force push) or anything like the one-time
  root migration, merging, GitHub settings and unrelated edits. A red check holds
  delivery until it is green; unclear file scope or a new architecture or roadmap
  decision stops it for the user. Leave unrelated dirty changes unstaged and
  intact. Read `docs/agents/ticket-delivery.md` before starting an authorized
  ticket, and `docs/agents/planning-delivery.md` before a planning branch or
  draft PR.

## Skills and phase boundaries

Read the **actual invoked** `SKILL.md` and required component skills; this file and
`docs/agents/matt-skills-catalog.md` are not substitutes. Local skills may be under
the current session's skill catalog or a local skills installation under the
user's home directory (`.agents/skills/<name>/SKILL.md`).
User-invoked skills do not run merely because a router recommends them. Read
`docs/agents/matt-pocock-workflow.md` for the rationale and distinctions; tracker,
triage and domain-doc operations are in `docs/agents/issue-tracker.md`,
`docs/agents/triage-labels.md` and `docs/agents/domain.md`.

- `/ask-matt` recommends a route only. For a scoped repository idea, begin with
  `/grill-with-docs` (both `/grilling` and `/domain-modeling`). A multi-session
  feature proceeds to `/to-spec`, `/to-tickets`, then `/implement` per ticket.
  A small agreed task may go from grilling straight to implementation only when
  the user explicitly selects that route.
- The user controls phase changes by command or unambiguous deliverable request;
  `continue`, `sure` and scope answers do not advance phases. Explicit permission
  covering multiple phases applies to all of them. Stay autonomous within the
  authorized phase; be brief and lead with blocking decisions. Do not write code
  or scaffolding until asked. Discussion notes are not a spec; even a draft spec
  requires `/to-spec`. A separate explicit documentation repair stands on its own.
- During grilling, maintain a decision tree: answers, recommendations, prerequisites
  and unresolved facts separately. Investigate facts yourself. Ask **the entire
  ready frontier** in numbered rounds with concrete scenarios and a recommendation
  under each question (`❓ Q1` / `➡️` where supported); do not ask for blanket
  approval of defaults. Label each `[GUI app]` or `[Modular]`. The user delegates
  node-specific policy choices: decide, record and report them within this phase,
  not as premature implementation. Cross-check code and glossary; surface conflicts.
  Record resolved terms in `CONTEXT.md`; write an ADR only for a hard-to-reverse,
  surprising decision with a real trade-off. Finish only after the frontier is
  empty and the user confirms shared understanding. Then wait for the next phase.
- `/to-spec` synthesizes without restarting the interview. Check public testing
  boundaries unless already agreed; publish the full template at
  `.scratch/<feature>/spec.md` with `Status: ready-for-agent`. Never invent answers.
  `/to-tickets` first agrees independently verifiable vertical slices and blockers,
  then publishes one ticket per file under `.scratch/<feature>/issues/`, blockers
  first. Generated ready tickets do not need triage.
- `/implement` uses agreed seams, behavioral red/green TDD where possible, focused
  checks and a full suite, then `/code-review` on independent Standards and Spec
  axes. Pin the review to the branch's diff from `origin/main` plus any uncommitted
  work.
  Report skipped checks and review limitations honestly.
  Each ticket gets a fresh session; after one, give the exact next `/implement`
  command. Keep context through tickets; hand off when needed, compact last.
- Other invoked workflows own their outputs: `/wayfinder` decision maps/tickets,
  `/loop-me` workflow specs and `/triage` briefs do not need `/to-spec` permission.
  Wayfinder charts without resolving its own tickets; work mode claims a ticket
  before resolving one per session (except research). Planning is the default;
  execution needs explicit user scope. See `docs/agents/issue-tracker.md` and
  `docs/agents/matt-skills-catalog.md` for complete mechanics and other routes.
- Work that changes Modular follows this workflow whichever project asks for it.
  Another project, such as the midishare website, may run without it; its requests
  are inputs to grilling and tickets here, and only the user authorizes a phase
  change or a ticket.

## Agent skills

### Issue tracker

Local markdown: specs and tickets live under `.scratch/<feature-slug>/`.
See `docs/agents/issue-tracker.md`.

### Triage labels

Default five-role vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`,
`ready-for-human`, `wontfix`), plus a local `done` for finished tickets, is
recorded as `Status:` lines in ticket files.
See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: root `CONTEXT.md` (glossary) and `docs/adr/` (decisions).
See `docs/agents/domain.md`.
