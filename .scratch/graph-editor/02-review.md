# Ticket 02 implementation review

Date: 2026-09-14
Ticket: [Play MIDI through placed Start/Stop nodes](issues/02-play-midi-through-placed-start-stop-nodes.md)
Spec: [Windows flat graph editor](spec.md), limited to ticket 02.
Disposition: complete, uncommitted. Tickets 03-05 remain uninvoked.

## Review boundary

Starting HEAD: `4163dc85b8ef742ae93829af969301dbfd6a4b1b`.
Commits made: none. Earlier uncommitted identity, node-I/O and ticket-01 work
was captured before implementation in
`%TEMP%/modular-ticket02-baseline-20260914`.

The independent Standards and Spec agents reviewed the complete tracked and
untracked source difference, not a HEAD-only diff:

```powershell
git -c core.autocrlf=false diff --no-index -- "$env:TEMP/modular-ticket02-baseline-20260914" "$env:TEMP/modular-ticket02-review-current"
```

The patch is `%TEMP%/modular-ticket02-review.patch`. Both agents initially hit
the account usage limit without returning reviews; after the user's resumption,
both completed their reviews and independently checked the fixes in the live files.

## Standards

Initial result: one P2 documented-contract finding, no additional actionable
heuristic smells. Removing an unwired middle allocator output through public
Graph operations shortened its output list without resizing the execution state,
allowing voice allocation to address a nonexistent output.

Resolution: prepare now rebuilds held-note and age state from surviving outputs
and rejects zero voices. A real-graph regression crashed before the fix with a
vector bounds assertion. After the fix, the edited allocator renders exactly the
same audio as a fresh allocator with the surviving voice count at block sizes
127 and 331. Existing identity/RESTRICT and allocation regressions remain green.

The independent reviewer confirmed the correction satisfies the authoring/edit
contract, which requires a fresh Engine, and found no remaining actionable issues.

Final Standards result: **0 remaining findings**.

## Spec

Initial result: one P2 finding against the requirement that compile/readiness
failures be visible. A new Run could fail before the GUI polled, ending in the
same `Failed` state as its predecessor. Cached presentation state could then
leave an intervening movement/edit message visible instead of this attempt's error.

Resolution: clear the cached presentation state on every Run/Edit attempt before
polling. Actual-app coverage retries an unready MIDI graph after a node click
overwrites the status. The original timing race was established by code review;
the test did not reproduce it deterministically, so it is behavioral regression
coverage rather than a claimed deterministic red/green reproducer.

The independent reviewer confirmed the fix and the allocator correction, and
found no other missing, incorrect or unrequested ticket behavior.

Final Spec result: **0 remaining findings**.

## Verification

- Full native Debug and Release builds and CTest suites passed again after the
  review corrections: **7/7 entries in each configuration**. Totals: 41 kernel,
  10 node, 7 authoring and 15 MIDI-file cases, plus actual-app, CLI and purity checks.
- Public Graph/Engine/node tests cover manual wiring, exact Stop/retrigger and
  completion cleanup, same-frame Stop precedence, bit-identical rendered audio,
  idle execution replacement, failed import repair, resource release on successful
  and failed preparation, worker failure reporting, and editable voice identities.
- Actual-app tests cover palette placement, MIDI loading/repair, wiring, node
  buttons, mode restrictions, movement during Run, visible failures and normal close.
  Rendering captures were inspected; port labels were corrected to avoid overlap.
- Separate actual-app `--audio` checks passed in both Debug and Release before the
  review corrections. Default-device peak was **0.499969** after wired Start and
  **1e-12** after wired Stop; output was silent before Start. A second Run played
  and normal close during processing succeeded. The check confirms an unmuted,
  nonzero-volume default endpoint and measures its output stream; it does not
  measure acoustic pressure or constitute human listening. Ordinary CTest uses no device.
- Earlier uncommitted work is preserved. `AGENTS.md` and `CLAUDE.md` are identical.

Standards: 0 remaining findings, no worst issue remains. Spec: 0 remaining
findings, no worst issue remains.
