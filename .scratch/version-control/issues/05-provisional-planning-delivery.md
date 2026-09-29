# 05: Provisional planning delivery

**What to build:** Let the maintainer explore uncertain roadmap or architecture ideas without making them accepted direction until a reviewed planning PR is deliberately merged.

**Blocked by:** 03: Enforced PR and CI gate.

**Status:** ready-for-agent

- [ ] Document the planning maturity boundary: short-lived planning branches and draft PRs hold provisional ideas; no permanent planning branch or incidental roadmap edit in a ticket implementation PR.
- [ ] Demonstrate that rejecting an exploration closes its draft PR with a preserved explanation but does not promote its proposed direction to `main`. Do not invent a product decision solely for this demonstration.
- [ ] Demonstrate that an independently accepted planning update can be reviewed and run through the consistently reported required check, then enters `main` only when the user chooses to merge.
- [ ] Keep the versioned local specs, tickets and durable domain docs as the accepted project records; GitHub PRs provide review and exploratory history rather than replacing the local issue tracker.
- [ ] Delete integrated short-lived planning branches and report where rejected exploration and accepted direction can be found.
