# Test fixtures

`one-note.mid` is a 35-byte minimal Standard MIDI File containing one test
note-on (middle C, velocity 100), a running-status note-off at tick 480, and
end-of-track at tick 960. It is format 0 with 480 ticks per quarter note. Its
bytes match the sequence built by `oneNote()` in `tests/test_midi_file.cpp` and
contain no song, artist or other metadata.

The file predates the public-baseline audit; its original authorship cannot be
independently established. It is retained because it consists solely of
standard-format framing and the minimal test events above. The proposed public
tree records this limitation rather than claiming the fixture was newly made.
