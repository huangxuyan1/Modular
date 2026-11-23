# 01: Pin a real version-1 save file as a fixture

**What to build:** graph persistence is about to make its first format change, to container
version 2. The rule that every older version keeps opening needs a real version-1 file to
test against: one that today's Save wrote, not text reconstructed by hand. This ticket saves
such a graph with today's version-1 Save, commits it unedited with the test fixtures, and
adds a persistence test that opens it and checks what comes back.

The graph covers every kind of port a saved graph holds today: a mono audio wire, a stereo
audio wire, a value wire and an event wire. It uses only node kinds whose codecs exist on
every platform, so no Windows output device. The test states its expected values literally
and does not recompute them. It writes them with the port type factories: `audio(N)` for an
audio port, and `value(id)` / `event(id)` for the others.

Those assertions mean the same thing before and after ADR-0003's third field lands. That is
the version-1 reading rule in the ADR's persistence clarification: an audio port's single
number is its channel count with subtype 0, and a value or event port's number is its
subtype.

No production code changes. This ticket lands before the writer changes, so the repository
history shows that the version-1 writer produced the file. Ticket 02 must then pass this test
without editing it or the fixture.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] The graph is saved by the current version-1 Save and committed unedited with the test
      fixtures. It contains at least one mono audio wire, one stereo audio wire, one value
      wire and one event wire, and is built only from node kinds available on every platform.
- [ ] A repository line-ending attribute exempts the fixture from conversion, so it stays
      byte-exact across checkouts. Windows checkouts here use automatic CRLF conversion.
- [ ] The fixtures README records the file's provenance: written by version-1 Save at a named
      commit, before any version-2 writer existed, and containing no personal data.
- [ ] A persistence test opens the fixture through the public Open. It checks these against
      literal expected values:
      - every node id;
      - every port id, name and type: audio ports by channel count with subtype 0, value and
        event ports by subtype;
      - every wire;
      - the saved next identity.
- [ ] When the fixture is created, opening it and saving it again with today's code
      reproduces it byte for byte. This is a one-off check recorded in this ticket's
      comments, not a test, because Save will write version 2 after ticket 02.
- [ ] The new test passes on today's code. No production code changes, and no file under
      `nodes/` changes.
- [ ] The Debug and Release suites pass.
