# Triage Labels

The skills speak in terms of five canonical triage roles. This file maps those roles to the actual label strings used in this repo's issue tracker.

| Label in mattpocock/skills | Label in our tracker | Meaning                                  |
| -------------------------- | -------------------- | ---------------------------------------- |
| `needs-triage`             | `needs-triage`       | Maintainer needs to evaluate this issue  |
| `needs-info`               | `needs-info`         | Waiting on reporter for more information |
| `ready-for-agent`          | `ready-for-agent`    | Fully specified, ready for an AFK agent  |
| `ready-for-human`          | `ready-for-human`    | Requires human implementation            |
| `wontfix`                  | `wontfix`            | Will not be actioned                     |
| —                          | `done`               | Implemented, verified and reviewed       |

When a skill mentions a role (e.g. "apply the AFK-ready triage label"), use the corresponding label string from this table.

`done` is local to this repository; no skill role maps to it. An implementation ticket's own branch sets it once every checklist item is ticked with recorded evidence, so `main` shows it only when that PR merges (see [ticket delivery](ticket-delivery.md)). Wayfinder decision tickets keep their own values, such as `resolved`; see [issue-tracker.md](issue-tracker.md#wayfinding-operations).

Edit the right-hand column to match whatever vocabulary you actually use.
