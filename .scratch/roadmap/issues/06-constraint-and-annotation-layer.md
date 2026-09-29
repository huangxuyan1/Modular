# The constraint mechanism and the annotation layer

Type: grilling
Status: open
Blocked by: 02, 05

## Question

Already ruled: constraints live in the engine, not the DAW, because otherwise an agent or
script editing through the API breaks invariants the DAW depends on. The reason is
two-way projection — the DAW can only be a pure projection if every graph the engine
*permits* is a graph the DAW can *display*; otherwise it needs a fallback rendering for
graphs it doesn't understand, and that is where a shadow source of truth grows.

Also already ruled: the engine hosts a **mechanism**, never DAW content. Apps supply the
content.

What's undecided:

- **Constraints alone don't achieve two-way projection.** They keep the projection *total*,
  but the DAW still needs to know *which* supernode to draw as a track — and nothing in the
  graph records that a supernode is a track. So constraints and the annotation/metadata
  layer already listed as Open in CLAUDE.md are one design area, not two. Confirm that, then
  design them together.
- **The DAW must not hold a track list.** The track list is a query over the graph. What
  makes that query possible?
- **Constraints must survive being violated.** A plugin gets uninstalled, an older project
  loads, a constraint set is tightened. Validation alone is insufficient — is the answer
  repair, quarantine, or refusing to load? This is the schema-migration problem.
- **Multi-tenancy.** If constraints are per-project data and two apps open the same project
  with different constraint sets — a DAW and a podcast editor — what happens? An app that
  doesn't understand a constraint still must not violate it.
- Where does this land, given ticket 02's layer naming? And does it stay clear of the
  kernel-purity rule?

CLAUDE.md already notes this layer is also the home for port-group presentation data, so
whatever is designed here has that second consumer.
