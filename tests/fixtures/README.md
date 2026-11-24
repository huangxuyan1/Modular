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

`graph-version-1.modular` is a 1,070-byte saved graph in container
`MODULAR_GRAPH 1`. It is built from primitive nodes available on every platform:
a manual trigger, a MIDI block, a sine synth, a constant, a mono gain, a stereo
gain and a stereo mixer. Its five wires are a trigger event, a MIDI event, mono
audio, a float value and stereo audio. One further node was added and deleted, so
`NEXT` is above every surviving id. It contains node kinds, ids, port names,
settings, layout coordinates and two MIDI note messages; it has no paths, names
or other personal data. The authoring test "a graph saved by the version 1 writer
opens with its identities port types and wires" checks it against literal values;
every later container version must still open it.

The version-1 Save (`saveGraphFile`) wrote it at commit
`dcc97483c23283cbab9c24030c219798d020fb67`, before any version-2 writer existed,
from a throwaway test case that was not kept. Opening the file and saving it again
with the same code reproduced it byte for byte. It is committed exactly as Save
wrote it, and `.gitattributes` exempts it from line-ending conversion.
