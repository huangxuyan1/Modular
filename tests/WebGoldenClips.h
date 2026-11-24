#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "GoldenRender.h"
#include "SoundFontTestFiles.h"

// The web render surface's golden clips (map ticket 08, decisions 5 and 6): a
// soundfont and three Standard MIDI Files, written in memory, with what the render
// core renders them to at three rates. The render core's tests check those values
// natively and under Node, and the module's smoke test checks the preview's through
// JavaScript, from files web_smoke_fixture writes, so every build renders the same
// input against the same values. A change that alters what the web surface renders
// updates the values in the same commit.
namespace webgolden {

using Bytes = sf2test::Bytes;

// --- the soundfont -------------------------------------------------------------

// A tone of `points` points: a triangle of `period` points with a little noise
// from a linear congruential generator. Integers only, so the soundfont has the
// same bytes in every build, as one made with std::sin need not.
inline std::vector<std::int16_t> tone(std::size_t points, int period,
                                      std::uint32_t seed) {
  std::vector<std::int16_t> result(points);
  const int half = period / 2;
  for (std::size_t i = 0; i < points; ++i) {
    const int phase = static_cast<int>(i % static_cast<std::size_t>(period));
    const int triangle = phase < half ? -14000 + 28000 * phase / half
                                      : 14000 - 28000 * (phase - half) / (period - half);
    seed = seed * 1664525u + 1013904223u;
    result[i] = static_cast<std::int16_t>(triangle + static_cast<int>(seed >> 23) - 256);
  }
  return result;
}

// One preset, the file's only one, with what the soundfont node does when the
// golden values were set:
//
// - every note plays a stereo pair of looping tones, panned -500 and +500;
// - keys 72 and up add a chime on a third voice, played once to its end;
// - the pair is recorded at 32 kHz and the chime at 25 kHz, neither a graph rate,
//   and both are rooted away from the keys the clips play (55 and 84);
// - a full volume envelope, and a cutoff the modulation envelope sweeps.
inline Bytes soundFont() {
  using sf2test::range;
  using sf2test::set;
  namespace gen = sf2test::gen;
  sf2test::File file;
  file.samples = {{"left", tone(8000, 97, 11), 32000, 55},
                  {"right", tone(8000, 89, 13), 32000, 55},
                  {"chime", tone(3000, 31, 17), 25000, 84}};
  file.samples[0].loop = sf2test::Loop{499, 7499};
  file.samples[1].loop = sf2test::Loop{503, 7503};
  const sf2test::Zone envelopes{
      {set(gen::kAttackVolEnv, -7200), set(gen::kHoldVolEnv, -9000),
       set(gen::kDecayVolEnv, -1200), set(gen::kSustainVolEnv, 120),
       set(gen::kReleaseVolEnv, -3000), set(gen::kInitialFilterFc, 9600),
       set(gen::kAttackModEnv, -6000), set(gen::kDecayModEnv, -2400),
       set(gen::kSustainModEnv, 500), set(gen::kModEnvToFilterFc, 2400)}};
  file.instruments = {
      {"pair", {envelopes,
                sf2test::Zone{{set(gen::kPan, -500), set(gen::kSampleModes, 1),
                               set(gen::kSampleId, 0)}},
                sf2test::Zone{{set(gen::kPan, 500), set(gen::kSampleModes, 1),
                               set(gen::kSampleId, 1)}}}},
      {"chime", {sf2test::Zone{{range(gen::kKeyRange, 72, 127), set(gen::kPan, 250),
                                set(gen::kInitialAttenuation, 60),
                                set(gen::kSampleId, 2)}}}}};
  file.presets = {{"golden", 0, 0, {sf2test::Zone{{set(gen::kInstrument, 0)}},
                                    sf2test::Zone{{set(gen::kInstrument, 1)}}}}};
  return sf2test::build(file);
}

// --- Standard MIDI Files -------------------------------------------------------

// A channel message, or a meta event from its 0xff on, at a tick.
struct Event {
  std::uint32_t tick;
  std::vector<std::uint8_t> bytes;
};

inline Event noteOn(std::uint32_t tick, int key, int velocity, int channel = 0) {
  return {tick, {static_cast<std::uint8_t>(0x90 | channel),
                 static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)}};
}

inline Event noteOff(std::uint32_t tick, int key, int channel = 0) {
  return {tick, {static_cast<std::uint8_t>(0x80 | channel),
                 static_cast<std::uint8_t>(key), 0}};
}

inline Event tempo(std::uint32_t tick, std::uint32_t microsecondsPerQuarter) {
  return {tick, {0xff, 0x51, 3, static_cast<std::uint8_t>(microsecondsPerQuarter >> 16),
                 static_cast<std::uint8_t>(microsecondsPerQuarter >> 8),
                 static_cast<std::uint8_t>(microsecondsPerQuarter)}};
}

namespace detail {

inline void bigEndian(Bytes& out, std::uint32_t value, int size) {
  for (int i = size - 1; i >= 0; --i)
    out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

// A variable-length quantity: seven bits a byte, most significant first.
inline void variable(Bytes& out, std::uint32_t value) {
  std::uint8_t groups[5];
  int count = 0;
  do {
    groups[count++] = static_cast<std::uint8_t>(value & 0x7f);
    value >>= 7;
  } while (value != 0);
  while (count > 1) out.push_back(static_cast<std::uint8_t>(groups[--count] | 0x80));
  out.push_back(groups[0]);
}

} // namespace detail

// A format 0 file at 480 ticks per quarter, ending at `endTick`. Events at one
// tick keep the order given. With no tempo event, a quarter lasts 500,000 us, so
// 960 ticks make a second.
inline Bytes midiFile(std::vector<Event> events, std::uint32_t endTick) {
  std::stable_sort(events.begin(), events.end(),
                   [](const Event& a, const Event& b) { return a.tick < b.tick; });
  Bytes track;
  std::uint32_t at = 0;
  for (const auto& event : events) {
    detail::variable(track, event.tick - at);
    at = event.tick;
    track.insert(track.end(), event.bytes.begin(), event.bytes.end());
  }
  detail::variable(track, endTick - at);
  track.insert(track.end(), {0xff, 0x2f, 0});
  Bytes file{'M', 'T', 'h', 'd'};
  detail::bigEndian(file, 6, 4);
  detail::bigEndian(file, 0, 2); // format 0
  detail::bigEndian(file, 1, 2); // one track
  detail::bigEndian(file, 480, 2);
  file.insert(file.end(), {'M', 'T', 'r', 'k'});
  detail::bigEndian(file, static_cast<std::uint32_t>(track.size()), 4);
  file.insert(file.end(), track.begin(), track.end());
  return file;
}

// --- the clips -----------------------------------------------------------------

// The timing tie. Q:1/4=90 in whole microseconds, 666,667 a quarter, with a note
// every 40 ticks through tick 15,000: the grid map ticket 08's probe replayed. At
// 48 kHz tick 15,000 is exactly frame 1,000,000.5, which a 64-bit and a 128-bit
// long double rounded to opposite sides. Then the tempo moves to 140 a quarter and
// the notes go on for two more beats. Each note is let go as the next starts, and
// keys 72 and up start three voices.
inline Bytes timingClip() {
  std::vector<Event> events{tempo(0, 666667)};
  int key = 0;
  for (std::uint32_t tick = 0; tick <= 16000; tick += 40) {
    if (tick > 0) events.push_back(noteOff(tick, key));
    if (tick == 15040) events.push_back(tempo(tick, 428571));
    if (tick == 16000) break;
    key = 60 + static_cast<int>(tick / 40 * 5 % 19);
    events.push_back(noteOn(tick, key, 64 + static_cast<int>(tick / 40 % 4) * 20));
  }
  return midiFile(events, 16000);
}

// A dense passage on four channels, notes 22, 22, then 7 ticks apart, each held
// 71 ticks, then a chord of three notes held and let go together. Below key 72 a
// note starts two voices and from it three, so a pool of four voices takes notes,
// with their 6 ms fade, all through it, and drops the note-offs of notes it took.
inline Bytes poolClip() {
  std::vector<Event> events;
  for (std::uint32_t i = 0; i < 32; ++i) {
    const std::uint32_t tick = 17 * i + 5 * (i % 3);
    const int channel = static_cast<int>(i % 4);
    const int key = 57 + static_cast<int>(7 * i % 24);
    events.push_back(noteOn(tick, key, 40 + static_cast<int>(29 * i % 88), channel));
    events.push_back(noteOff(tick + 71, key, channel));
  }
  for (const int key : {60, 64, 67}) events.push_back(noteOn(701, key, 96));
  for (const int key : {60, 64, 67}) events.push_back(noteOff(907, key));
  return midiFile(events, 960);
}

// The voice limit the pool clip renders with.
inline constexpr std::uint32_t kPoolVoiceLimit = 4;

// A note preview: middle C held 0.25 s at velocity 105. The file ends as it is
// let go, so its release is the rendering's tail.
inline Bytes previewClip() {
  return midiFile({noteOn(0, 60, 105), noteOff(240, 60)}, 240);
}

// --- the golden values ---------------------------------------------------------

// What the render core renders each clip to, with the soundfont above, at 44.1, 48
// and 22.05 kHz. A change that alters them updates them in the same commit; the
// render core's tests print the new values.
inline const std::vector<golden::Render> kTimingGolden{
    {44100, 964044, {
        0xee80d705a612dd93ull, 0x2b6080bdad766fe5ull, 0xe889a4454b539c95ull,
        0x9890d6f438952e90ull, 0xb196fb543c7e9ae7ull, 0x79f01e8623cec032ull,
        0x03902d2858ad9fefull, 0x87281aa4d48a95abull, 0xd8491a65dbf49ed7ull,
        0x2a7c1e0a29135735ull, 0x1e078bb30cf324b1ull, 0x909483f14e1520abull,
        0x8de891536f396c18ull, 0x66d9cd2effdb7398ull, 0x59c912a1297bfa6full,
        0x2a82174e32e13d5aull, 0x2623cc4fb953da1cull, 0x45747a4a1d44fcb1ull,
        0x8a54505b078c6bc2ull, 0x825731ab701afdbeull, 0x0ab03d668527a552ull,
        0x366ba2d3a99df763ull, 0x6edcd92228337711ull, 0x819e0c741093cc0bull,
        0x71f3fa031fb2072eull, 0x3b252c07338078dbull, 0xa6ea15707d6c8c46ull,
        0x58c84a6b81eb2079ull, 0xe9a4cb46df17386bull, 0xcd18169b29493c61ull,
        0x2bab0ae1d8bdc168ull, 0x2971543294756e0bull, 0x2ae2fd33a836b3f3ull,
        0x94fbc8994a24fc3eull, 0xc3f27b4f7faff942ull, 0x22ec1228ec7bc55aull,
        0x7fad154269051145ull, 0x03951c220580f9f0ull, 0xf1cf20cda2262c9full,
        0x836072aeb0727ec6ull, 0xf811c1707fcdeea6ull, 0x5486fa0f1f31ce36ull,
        0x22c03f03907db310ull, 0x6faaab4d8f21ee21ull, 0xda271673bcb34cd5ull,
        0xb1a3ee166ea0eefbull, 0x28115b81eea95cf7ull, 0xb1f1cb7b74bcc22eull,
        0x0b41c2f16e68d386ull, 0xf75eb5018e7377c8ull, 0x4c319bfe618e62c6ull,
        0x9221a4e4717ec5cfull, 0xea300d336e897d38ull, 0x0a0fe2d04a8c5371ull,
        0x864c64aa28ebc695ull, 0x72653078fa99799cull, 0xaa31bc0f50769618ull,
        0xabd2246aa1a5d63bull, 0x7c8f95c660970c63ull, 0xc42105b1c113e5b3ull,
        0xd1b87676b8c0bc52ull, 0x54cbdb7ffc18007eull, 0xa4bfc5e0073d95faull,
        0xa6f985984ef82b6cull, 0x9c9f65f72cf1707dull, 0x1dbc2fd7dcefedd2ull,
        0x21e3a0a9440fb8f8ull, 0xc20c4809e3540a43ull, 0xbc10a6284c3e31e9ull,
        0x50d6f5fb6e4c2dc7ull, 0x67e5fc7d403a7a9cull, 0xaf81a5b26156af5cull,
        0xe1c13a0a16dd841full, 0x02c8eda7b94c2714ull, 0xdf58d7d0f4563520ull,
        0xa81ca6de7215f0ddull, 0xac2c113db8dc20ffull, 0x6f682ee4f9631699ull,
        0x55c2c7ca2df8b316ull, 0xc9552cee1ba09bc6ull, 0xcd5932fa17f58cc6ull,
        0x9b255389b2354d9full, 0xd9ffcdfa2c3e4f9dull, 0x4911666d8111fed0ull,
        0x2cf83dd77ba9f867ull, 0x4c3a0ed7d9f7adaaull, 0xf0c01ece6ecf5396ull,
        0x9b616d88f3c8b74aull, 0x38c74f13d8a9c4b5ull, 0x86f4d65fd6c8c4a8ull,
        0x1706fb52acd24c35ull, 0x3ebc924fcbda76baull, 0x62b588fa66c37a4bull,
        0xd0a92b24dbe40cceull, 0x9815cc6595f9113cull, 0x38ee67d47824e2f0ull,
        0x4ec87af1d9739bc4ull, 0x7646a62ca129b915ull, 0xa92cbc2fbf86036bull,
        0xdd050599cef4c95dull, 0x552117c38caaa75full, 0xec38c481355129d0ull,
        0x1ca3968ca12cf30aull, 0x0a2013e850b76100ull, 0x4a1a438e1b37270full,
        0x9e7435788a6e59bfull, 0xff0bf44314df63faull, 0x95b4ffaf2066f9c5ull,
        0x204c65df86dfccd3ull, 0xd9e87a8c9452787dull, 0xc06ba7ba3d1f7940ull,
        0x8243ddc7791ab2edull, 0xc7a702ab7c5fd713ull, 0x8c1fc54eed7ec031ull,
        0xc15483cafd3b66daull, 0xc1b2145be76f00b4ull, 0x474acd3534efd60aull,
        0xda92b3e4d365e400ull, 0x840114e2e1c4271full, 0xf5d10729ced270eeull,
        0x9f5de16a76904a55ull, 0xacb5591bb9abed67ull, 0xae4a594a66826dd7ull,
        0x1e88b8945b6a1270ull, 0x3c910b974100c742ull, 0xd64c5924279a4e65ull,
        0x7da067a9043f81b6ull, 0x9221d609c2283418ull, 0xf7edf1c29299e37eull,
        0x32620a02dd88c227ull, 0x6b359d9426c184deull, 0xaf961b054563270eull,
        0xf84a6aea9e37aa8full, 0x0a91ffee81d723f2ull, 0xa84c3d684406a732ull,
        0xc6adc9780af91dc9ull, 0x3cad5f9e6d231b50ull, 0xc8259ffa745389c2ull,
        0x97404eb623dea15eull, 0xd42f39e8729fa745ull, 0x70e09fc2851b2b5eull,
        0x31038f373deb0424ull, 0x559cb1f576d735e4ull, 0x2261699b46d2c716ull,
        0xbe4ca79aced37433ull, 0xec85f7ca53a63887ull, 0xeb8a22d9fc20524full,
        0xaf6be535934000a0ull, 0xc0f58e0ff8fdadf4ull, 0x1482980bef2a3590ull,
        0x2f3e58509577c93bull, 0xf677de65f0e52eaeull, 0xecea8e4a8b395c41ull,
        0x4e2be0a3667084a9ull, 0x4396a7af16772c0eull, 0xb218f0ad6ca42682ull,
        0x4ccac5ccf6eb4ffdull, 0x243ebc330ed1b927ull, 0x05433cf20ef4ab19ull,
        0xd6ed0e6e5fc5e5f5ull, 0x6e5738f870049792ull, 0x308db3160d8fedddull,
        0xb787762ecb3374c9ull, 0x834b928bae20a052ull, 0x23b8bd8b72ba497eull,
        0x2ceed8a2a7eee9a2ull, 0x4b6e58f747af3d26ull, 0x0bc032369938d0acull,
        0xe6d6ba9e68abc7b3ull, 0xf60e761f0d541109ull, 0x1d3cfdf2a90a7a32ull,
        0x1cb27a085e5ffa2cull, 0x5715b9587469c229ull, 0x1eb8a75854869863ull,
        0xb627d232548d95adull, 0x65af6873ffece078ull, 0x33d6619b7f5e36bcull,
        0x8aefbd2996b15d9eull, 0xa8d295fc0b8153c7ull, 0x4dc82c00027fa036ull,
        0xdc8130f7facc81efull, 0x8167e17b6816b322ull, 0x479c2111468b8149ull,
        0x8df6ccfb8423aeabull, 0xc4750ea150903548ull, 0x53011af123e6bd73ull,
        0x6301c4008d33c179ull, 0x8a9a515bb8f6e7ccull, 0xafbedc3594929517ull,
        0x0fca18970a83fcebull, 0x5cbbddf458f384c3ull, 0x2dcdb72069c20493ull,
        0x96f6b9ac5e3b7e29ull, 0x6adb228d654f38b6ull, 0xb5e34f19cd3a0c1dull,
        0xccdcf9bb83444b03ull, 0xc733f3baa8449503ull, 0x1772b91595aaf5e1ull,
        0x73091e9f3fb6f796ull, 0xf2305d090c936aefull, 0x9fa51072033b57e4ull,
        0xd804d7331990dad1ull, 0x99997ded68456cc6ull, 0x14c5995184ed6bc4ull,
        0x786897806adcf2dfull, 0x2e8abd29140009c0ull, 0xbbef2fca60ccea3bull,
        0x85cc339a63714a8dull, 0xd0595eec78411a1bull, 0xb47622195424c531ull,
        0xec5a9307e0f6e21bull, 0x830a142e7ef954caull, 0xfd06e29e9561556bull,
        0x888cd3dd1323f56bull, 0x9e903a684c91e71full, 0x3ee51c9c4a749334ull,
        0x1a591a3ab7343142ull, 0x96994948ad1ef673ull, 0xef92d7b37bf03176ull,
        0x060125743b17cfdbull, 0xc41b2a602a1d4991ull, 0xfff12d6473c4b832ull,
        0xdd583304498d3b7eull, 0xe8830b9f37656444ull, 0x593d1db3208a1c73ull,
        0x6b49d05c016773c9ull, 0xcc0d65226eb4c0c2ull, 0x1f989718d7abcf21ull,
        0x9a465498795ae9d0ull, 0x8182b702cbfee7ceull, 0x498ed655a63c830dull,
        0x0317fc09e317ea11ull, 0x8fe9f29f2479a31full, 0x8a43d7c66af3a490ull,
        0x428d4476c1a78a1full, 0x0f302a7bb3ad5681ull}},
    {48000, 1049301, {
        0x2d049500e8d52ca5ull, 0xdb0f986521c47b5cull, 0x77a4bc2b232c9653ull,
        0xe1016880049128dcull, 0x6690b20285aa609full, 0xba0978e6e68def12ull,
        0xe4790055da2bfaf8ull, 0x7e7b5cc9d010c7f6ull, 0xd513f3499dd608e7ull,
        0xd0f7b4fc0fe983e9ull, 0x23c689827d05f03cull, 0x3192681a92386b85ull,
        0x6e5a0c57bd07201dull, 0xe8ff2bf43e494b87ull, 0x969f703eb88f95fcull,
        0x321dd876502779e7ull, 0x254bc8f2cb9cecc0ull, 0xd7b815064ddea8a9ull,
        0x176b132745c1aeb5ull, 0x446a7afdb083b49aull, 0x1ad9482a61e3277bull,
        0x6653806079f92f3bull, 0x4748671bcb8343fbull, 0x5bb641bf0c6a5fb8ull,
        0xd122f8e7a1e3a12dull, 0x6d8a8a8c5dddaa17ull, 0x409861acc4d311f7ull,
        0x35a4c43a0aa83fb7ull, 0xa241ed6f1d6de38bull, 0x487cfec85c784cebull,
        0x83fa639726e131e3ull, 0xbfb4c3bace414089ull, 0x61db0dbedd3e84bfull,
        0x634d250b7eecc264ull, 0x962162f59f0d1854ull, 0x6678cae5efeaf104ull,
        0x9a96371cac50396aull, 0x7e55b03a76ff47d5ull, 0x15034f087e20d362ull,
        0x724b45f42e1fdcc6ull, 0xe0a0cfb7f305eb59ull, 0x71d8282e1dfd39d3ull,
        0x6d47794954a38b9aull, 0x24432f9d7b48fd1dull, 0xe6efa4b784c239c2ull,
        0x00ba78b4b55aa2b3ull, 0x173902e6cf881d6aull, 0xe1e01269217e971full,
        0x08c35bee7ef9b4c0ull, 0x14ae6416e080672full, 0x6bd26d1f3cc2e584ull,
        0xd9f0d35211af2999ull, 0x5f08fc325ac49624ull, 0x098051cbf29016b2ull,
        0x63fb70c0a312b22cull, 0x6dce19dab7e49181ull, 0x2f16668dc4861aa3ull,
        0x0e89d54380983738ull, 0xcc02b637e96aab25ull, 0xf896ec96c0cb566aull,
        0xb06aab6638e16325ull, 0xfa965a7e2357c48dull, 0xa08df100b416e443ull,
        0x09bd3239ebd57fc1ull, 0x2cc583bcadd78c2eull, 0x279c7b695c85307cull,
        0x542122c10acaa859ull, 0x4a885bc81517adddull, 0xfd1defecc13e22cfull,
        0x42e03f3f70ebddc5ull, 0xd01a31bc36c8c21dull, 0x25daca0c82f8cb19ull,
        0x0d77de5ff6de3cb5ull, 0x121590ac5c975187ull, 0x2bb7fa8700af8b2dull,
        0x5d4ef5c0a856e109ull, 0x7b00db5a6588ad2bull, 0x40b9c86ac70d180eull,
        0x16b4adda7ab913cbull, 0xd82103afe2317a39ull, 0x8a88f7333eed4606ull,
        0x65098a4b51fa6f5bull, 0x62a04c755e6ec8d2ull, 0x6fdba709492ebd84ull,
        0xf62bfd34cccc9ae3ull, 0xe119df255acf0aedull, 0x2e8721a5acfc20a0ull,
        0x2e70c8dba8111cb8ull, 0xf2e24edd59d7211cull, 0xe2c72190b8868851ull,
        0xabca4c877146fb7eull, 0x0aaf832f003c7ba4ull, 0xa13b7947ce5dfe6cull,
        0x98b3c68a2e74591aull, 0x32c63e71a1232bc6ull, 0xb5474c6b7dee34acull,
        0x75ad6cd53ae221e0ull, 0xbc31d365d0fe6204ull, 0x4d21b6aaeabd9521ull,
        0x0ce22b898a06716full, 0xcf43e0491c1424b8ull, 0xe9aba5ac817caf43ull,
        0x26c3ec0167f8cae9ull, 0x4420c695682b3979ull, 0xc9746685836e3526ull,
        0x4a40bddba806d6aaull, 0x8d0d8c1713e831e2ull, 0x7e04f0d32c6aa370ull,
        0xeffb95a4cfb13cecull, 0x8315ada30108b493ull, 0x1bf9a0a8a1fcf834ull,
        0x0aecaa4abdc2b0c9ull, 0x4347014e0d06875eull, 0xf6812e743eb94983ull,
        0xc4a2f0cd2e4eecfaull, 0x0b4ae80481bad9ceull, 0x1197724d133f4056ull,
        0xcd7fd975e6af4661ull, 0x646824c3d635a57aull, 0xa28978b6542cc201ull,
        0xcb0277d4c7945713ull, 0xabe48c5f2cf1aacbull, 0x5663b71d3a6b60ddull,
        0x2b25ba872c9b2ab0ull, 0x9ad2ce4125e5201full, 0x81147b42cdcbf594ull,
        0x2cdc37dae6136839ull, 0x63242b57f45d0647ull, 0x1637397e38c1741dull,
        0x871ba53f23ec09ccull, 0x54953b7806e133b4ull, 0x304971779232fd8cull,
        0xbff26cb04f14bcceull, 0x7dd69fc665d5c292ull, 0x3721e8d499f06b25ull,
        0x6889d95d378562caull, 0x89975a2a510da1cfull, 0xeccd081119494101ull,
        0xbd6a4c9fca01c3caull, 0x7966870135431513ull, 0xb24ef7cb0423f061ull,
        0x9cbfdec20aa31e1bull, 0x73228a89df334ff9ull, 0x62a24d962e4cdaceull,
        0x6f0d98720d3c7a46ull, 0xcb88549d03bff2eeull, 0x980471e754fd6140ull,
        0xdf55e8d4f2b8bddeull, 0x913207351036bc37ull, 0x002fa0dd1d32c88cull,
        0x231ade4ae41c90dfull, 0xc6ce40ee8901c140ull, 0x503e9666cbfbb7cdull,
        0x04dd4596ae1eac67ull, 0x31a5ed6ca5e4dc95ull, 0x576e3c672b87d5a4ull,
        0xfef53a2f42720ef9ull, 0xf71b115afcd870dfull, 0x8c580f1e4204f1ffull,
        0xf6ff1fa165a60e32ull, 0xbfb3978fc6171b4eull, 0x968117ebfd931214ull,
        0x43af197d7e7229acull, 0x764ad267ae0e1301ull, 0x3a130a8c70f5c026ull,
        0xa381a63cd52395c3ull, 0xb773a1dc4af8e672ull, 0x66f8a55f2f5d2c54ull,
        0xd273551365558ee6ull, 0x6054f9e06a20fd4dull, 0xaa918179e0d9e4c3ull,
        0xd905d1b832e4be4full, 0xf9262c55db4f95edull, 0x164b1e3e3f2cf1b2ull,
        0xfbaad8e73bb026b8ull, 0x3c3270a1986e6175ull, 0x10bc81a557660371ull,
        0xa1fbd805b8035624ull, 0x0468854e943e30d4ull, 0xa1ea191fb27d4d2cull,
        0xef625d53b3da89efull, 0x7f571117345735f1ull, 0x7d7c58a3e94c14e9ull,
        0xc4c9ade017589684ull, 0x4a922d041304c188ull, 0x224fde563abc9fcdull,
        0xa071d0fc2e2529e7ull, 0xae592b85d7a29217ull, 0x07bc8707852681a4ull,
        0x6324638f3159f3f5ull, 0x8e10e9951103b208ull, 0x8d04e54c9ac28e4aull,
        0xa4107dfa850c6721ull, 0x0afa4f373a222180ull, 0x8ca04d79266157faull,
        0x37e2d03b646f3c93ull, 0xce25453b4e20a4d8ull, 0x05296299d5b68003ull,
        0xc38c5718a258cc04ull, 0xc9622093476ad9c0ull, 0xfcbe55175d5e8dd9ull,
        0x85e021ac5ff77db0ull, 0xbd50a25f7ec1ba18ull, 0x4c3652d3872c0d13ull,
        0xedab519394fd7c3aull, 0xa9377a67219393e6ull, 0xe77d124fd44c0e83ull,
        0x7657a044e40fb604ull, 0x7984cf620febba84ull, 0x26f9c9fe6fb74889ull,
        0x93c2b94c359ef6deull, 0x39c206d1f7880b50ull, 0x5aedb9b302ee44f4ull,
        0x910c2b2baf488494ull, 0xd1725a97cb8c12ebull, 0x26c73f4a9f52efbbull,
        0x41f47d347ce4bb3cull, 0x38ea1c514833ec0full, 0x167acf654350193aull,
        0xaa0f208c37f3f937ull, 0x0dd17415297d9e63ull, 0x6339a7258156cd38ull,
        0x13e0c024abe251eaull, 0x5426eba78fe82f14ull, 0x4cec53f464b8611full,
        0x48114deba80604b6ull, 0x2fa0eb73e6cca433ull, 0x2558114eb4300d5eull,
        0x5b1f686e42e9247aull, 0x3b8cbb2df2f12f0aull, 0x6b6a30d1cfeb0f6eull,
        0x3514f7eb297067bbull, 0xc83edd08b23a8b03ull, 0xd89bf7a03e3d7186ull,
        0x3fdc87ce5740bb9full, 0xe5d9d2281b2b543full, 0x9800a370b3df4343ull,
        0xa8e26e52553e0d1dull, 0x4320a01f7d46f69dull, 0x87f077efd6fd85ffull,
        0x84dbfe0c82494906ull, 0x1e45f23757826c50ull, 0x342bf6a0b48f54fdull,
        0x37b71a852506bddfull, 0x64d76ce82ba721a6ull, 0x942b638990684ab9ull,
        0x5950d54b31c45455ull, 0x9b3c7c7295e12dafull, 0xa2c7a536f45663e4ull,
        0x8f5965a8e82933ccull, 0x274be2cd2a03371cull, 0x480e2c7f31b1a256ull,
        0x3f1f152bad4d1f9dull, 0xa9fb1c10ec1fd0f7ull, 0x9bbb7f1223b10b68ull,
        0xd3b4c8a7bce889bdull, 0x7d189a0172939940ull}},
    {22050, 482023, {
        0xf01aad3c530c24baull, 0x25b78f4d4b6bdbbaull, 0xe1adcebe08c21e33ull,
        0x657973058a2931c4ull, 0x8a87658b7689e1ceull, 0x4a9ae60704259dfcull,
        0x6159d2265c644f36ull, 0xa1159cf2bf477c72ull, 0x2d471d687c3d2293ull,
        0x25d70836ecf2d228ull, 0x6179d8bc2ebd2259ull, 0xa1d0fa35b6882f23ull,
        0x7b03dc40ae01f760ull, 0xf4b349fe04f46534ull, 0x1b928654053d560eull,
        0x91c1035c772652aaull, 0x7a8c59ec1ffa3436ull, 0xc9480ee34ebf80f4ull,
        0xdf70637a5e1f24fbull, 0x8e96fa6428dbb45dull, 0x66f0e2714bd68399ull,
        0x17964769fe268c7full, 0x6ed6cd4e1425de92ull, 0x3c13e88903a43763ull,
        0x92b5f32c55aafa66ull, 0x178e78f9a7c377efull, 0xd6518ff1672986bbull,
        0x365dd2bc34b0136full, 0x17ec74cd325f1f83ull, 0xa876d97c738747f9ull,
        0xceb3683ce8ad24d4ull, 0xd5282e4129944ea0ull, 0xe45cb15736179c65ull,
        0xaa9094b206fb6937ull, 0x61946aa5d86701d3ull, 0x9ff60782197f4ce2ull,
        0x6dcfbedb5d50ebeeull, 0xe9da231081e67cd7ull, 0x798a5df9c9dcf383ull,
        0x02487bd7632daf01ull, 0xa3f07b0a629d4b39ull, 0x182c33740b0fd515ull,
        0x92f72eac89c7fa41ull, 0x9e3ee907bbfa9173ull, 0xc33bfb96c1106f5dull,
        0x140e434bc3ddf8cbull, 0x259a0dbf0bd2799bull, 0x9d33266f86bb6caaull,
        0xd39a488b0d34c791ull, 0xb23e882fa7cfb5feull, 0x208c9a6a43757bceull,
        0x0d2cbdc2bc21fd18ull, 0x4423c8000c7d9fa4ull, 0xd7fb085ef02bb8a1ull,
        0xb835fa45db45c424ull, 0x7e041a0857bce27aull, 0x49e4954d32db4f63ull,
        0x7f58cd4957c3babdull, 0xff2372d714d77e0dull, 0xf7067c4481fc9203ull,
        0x3cceffe6df76cc53ull, 0xf67b68c85e2661b9ull, 0xbeff84cdcb56bf6dull,
        0xd5dddf85d67c01c1ull, 0x77d948315d75e528ull, 0x01ac13d0cd918259ull,
        0x264acd1aa53672b6ull, 0x0a8542be0bc76ea8ull, 0x3b9903cc4bcd26afull,
        0x876167dde803c30aull, 0x5aba1792027a39acull, 0x6fd7d43c5e3459d0ull,
        0x6ea90edd65c8a128ull, 0x5bdba371bee3e983ull, 0x9477c812eb8bd0d4ull,
        0x4f52cfa97d298554ull, 0x56f04084ac497994ull, 0x4244013c8854ddfcull,
        0xc698ff773fc8025dull, 0x124fa78b7833102aull, 0x830df38888636c94ull,
        0xc9923a81114b2377ull, 0xe4b8c22d54fb7738ull, 0x98d53c385b041515ull,
        0x1ed82fa009f0c977ull, 0x9493a45c3ba5d31bull, 0xdce033e0ad0ba42bull,
        0xc291d76745929154ull, 0xce763f55e0ec397bull, 0x7ac00addb584ad6bull,
        0xae281bb6e302b885ull, 0x34f13a523d6b4b6full, 0x7d0ba90c67d8d04bull,
        0x7fe57c55c987f98dull, 0x8425cefb0143fa16ull, 0x1884c3304d93b95full,
        0xd9fea6f74a76e830ull, 0x521f1cfcb81e0041ull, 0x7b453ab892a37363ull,
        0xc49306d7640b68f1ull, 0x35ab76e4fc13deb5ull, 0x203cd8b1e7816032ull,
        0xb661a0facae66a66ull, 0x16ed7a193210de76ull, 0x1cc280fbb466bb64ull,
        0xca41d502479d7578ull, 0xd1f46ac996c16068ull, 0xd250cbbb89c469b3ull,
        0x6cd465d4b2caf113ull, 0xbe97c34a392b6612ull, 0x05d3f7ce2ed6faffull,
        0x4b2ac529f3fbe75cull, 0xa6590d6354b02707ull, 0x6d5c2dbbed7cf801ull,
        0xe4aacf67eb9f0c2dull, 0x3b555c659b20168bull, 0xe8f96c4e03c52617ull,
        0xe6e7d4dfb65894f4ull}},
};

inline const std::vector<golden::Render> kPoolGolden{
    {44100, 45804, {
        0x5e21a41d2af836a7ull, 0x1a0c05356ae6a6a2ull, 0x80bbdc437a6aa7e2ull,
        0xb527e18847f6652cull, 0x81ec9a80365b324dull, 0x044f11f9af2a7d43ull,
        0xb38a2262ab034c42ull, 0xcf49e2748eeeaee7ull, 0x17b1d248f5eab8f9ull,
        0xc68459d2630ab647ull, 0x59d96ee2bf8729edull, 0xfffba9d654a2a131ull}},
    {48000, 49855, {
        0x6f14ff983b744948ull, 0xf4c9c1106c385b31ull, 0x5863ff1c7933a7acull,
        0x8bbaf041839ccea2ull, 0xcd5b26716cac115cull, 0x4c2b7d275384d3ebull,
        0x942c8131f9fce284ull, 0x3e3542176b955b9bull, 0x0fa574896bc70f9aull,
        0xafc1047b74b90219ull, 0x6fa5f7355997960eull, 0x4597e8aa32416659ull,
        0xab4b5e3f511d713aull}},
    {22050, 22903, {
        0x24a0294890ae005dull, 0x75bd2371a1c95347ull, 0xa1e91c7191c5d99aull,
        0xe25a917779f7573bull, 0x6c803ca11c4bdd2aull, 0xe77a276f134e82b2ull}},
};

inline const std::vector<golden::Render> kPreviewGolden{
    {44100, 15058, {
        0x9475f0952da883b5ull, 0x7204294d4d139831ull, 0xfaed60780fc7083bull,
        0xd3a432ac03452ad3ull}},
    {48000, 16390, {
        0x7597ec3240543af8ull, 0x550bb0ded79d1774ull, 0x187ad7db25192b84ull,
        0xf73b149c78a2ad3full, 0x907119e1db7fd826ull}},
    {22050, 7530, {
        0xf274989813bb8352ull, 0xe94fb3673346a502ull}},
};

} // namespace webgolden
