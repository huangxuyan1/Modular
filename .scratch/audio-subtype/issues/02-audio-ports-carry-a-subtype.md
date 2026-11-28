# 02: Audio ports carry a subtype

**What to build:** the audio subtype claim, end to end. An 80-band spectral stream and an
80-channel speaker bus cannot be connected to each other by mistake, and no file under
`nodes/` changes to make that true.

**Graph author.** Connecting an output declared `audio(80, kMel)` to an `audio(80)` speaker
bus input is refused at connect time, with the same type-mismatch error any other mismatched
wire gets. Every existing graph connects exactly as before.

**Node author.** Meaning is added by one optional argument to the audio port declaration
already in use. The subtype defaults to 0, so no existing declaration changes. An audio port
with zero channels is now an authoring error.

**User.** Every graph saved so far still opens, with every port's channel count intact. The
next ordinary Save writes container version 2, which stores an audio port's subtype and
channel count separately. The editor keeps labelling audio ports `audio<N>`.

ADR-0003 and its persistence clarification settle the design, and this feature's spec
records every decision and the test seams. Two user decisions from 2026-11-23 apply here:
- **Zero channels:** an audio port with 0 channels throws, in every build. This replaces the
  ADR's word "assert".
- **Screenshot:** the deviation in the screenshot criterion below.

**Blocked by:** 01 (Pin a real version-1 save file as a fixture).

**Status:** needs-info

**Kernel**

- [x] A port type has a domain, an opaque subtype and an audio-only channel count. Equality,
      and therefore connect, compares all three.
- [x] The audio port factory keeps its call shape and takes an optional subtype that
      defaults to 0. Value and event ports have channel count 0.
- [x] Audio subtype 0 is a concrete "unspecified audio" type, not a wildcard, on outputs and
      inputs alike.
- [x] The Engine sizes every audio buffer from the channel count and never reads the subtype.
      That covers the silence width, output allocation and input channel counts.
- [x] Declaring an audio port with 0 channels throws `invalid_argument` in every build
      configuration, and so does reconfiguring one to 0 channels. Boundary nodes and
      reconstruction during Open are covered.
- [x] Kernel tests, with `kMel` defined only in the test:
      - `audio(80, kMel)` → `audio(80)` throws;
      - `audio(80, kMel)` → `audio(80, kMel)` and `audio(80)` → `audio(80)` both connect;
      - an equal subtype with a different width still throws;
      - a boundary node exposing `audio(80, kMel)` gives the supernode's face that full type,
        and that face refuses an `audio(80)` input;
      - a subtype change on a wired port is refused, and leaves every port type unchanged.
- [x] The existing mono, stereo and mixer render tests pass unchanged.

**Graph persistence**

- [x] Save always writes `MODULAR_GRAPH 2`.
      - Its port records carry the subtype and channel count as separate numbers; value and
        event ports write channel count 0.
      - No other record changes, and every existing validation rule applies to both versions.
- [x] Open reads versions 1 and 2. A version-1 audio port's number becomes its channel count
      with subtype 0, and a value or event port's number stays its subtype. Any other version
      fails as unsupported.
- [x] Ticket 01's fixture test passes with neither the test nor the fixture edited. The
      reopened fixture saves as version 2 and reopens with identical ids, names, types and
      wires.
- [x] A test-only node and codec with an `audio(80, kMel)` output round-trip through Save and
      Open with the subtype intact. The reopened port still refuses an `audio(80)` input.
- [x] A version-2 file with a zero-channel audio port fails to open and leaves the caller's
      graph unchanged.
- [ ] The only edit to an existing test: the corrupt-input test's unsupported-version case
      moves from version 2 to version 3.
      **Open, for the user:** a second existing test needed an edit. "stored MIDI file times
      keep the digits that recover each double as earlier saves wrote them" was added by
      PR 29 on 2026-11-24, after this ticket. It compares Save's whole output with literal
      version-1 bytes, so any version-2 writer fails it. It now expects version 2 output with
      the same CONTENT; see the comments.
- [x] Node codecs keep their kind versions and encodings.
- [x] Opening a version-1 file never rewrites it; it changes only through the user's next
      Save.

**Editor**

- [x] The editor's port label reads the channel count and still shows `audio<N>`. It does not
      display a non-zero subtype.
- [x] The screenshot from the `desktop` editor test shows non-zero `audio<N>` labels. The
      test keeps its existing mono graph, so the labels read `audio<1>`.
      **Deviation from the spec, approved by the user on 2026-11-23:**
      - the spec asked for a stereo node showing `audio<2>`;
      - any non-zero width shows that the label reads the channel count, not `audio<0>`;
      - a stereo graph would move the test's click coordinates.

**Documentation**

- [x] The graph persistence documentation:
      - describes container version 2;
      - explains how version 1 is read;
      - states the versioning precedent: a format change bumps the container version, every
        older version keeps opening, and each version has a fixture written by its own Save;
      - drops its note that the third port-type field is not yet in the codebase.
- [x] The graph editor documentation no longer says audio has no separate subtype field.

**Close**

- [x] `git diff --stat` against the base shows no file under `nodes/`.
- [x] The kernel purity check passes.
- [x] The Debug and Release suites pass, including both `desktop`-labelled editor tests.
- [x] The claim closes with a commit whose subject is the claim, and the roadmap records the
      claim as proven, as the identity claim's close did.
