// PROTOTYPE — throwaway probe for .scratch/midishare-playback/issues/04-page-to-modular-contract.md
//
// Question: does the Standard MIDI File that abcjs writes for Download .mid carry what the site
// plays and shows today? abcjs 6.7.2 gives three views of one tune, all from the same flattened
// sequence (tune.setUpAudio):
//   - the synth's note windows (createNoteMap with prime's seconds-per-whole-note), which is what
//     the site hears today;
//   - the bytes getMidiFile(abc, {midiOutputType: 'binary'}) returns, which is Download .mid;
//   - the piano roll's notes (abcToNotes: rounded to 16th steps, same pitch and start deduped).
// For each clip it prints counts, ends and every note whose onset, length or velocity differs.
//
// Run with Node.js against a copy of abcjs 6.7.2's dist/abcjs-basic.js, read from the midishare
// repository's node_modules and copied anywhere outside it:
//   node PROTOTYPE_compare.js <path to abcjs-basic.js>
// abcjs logs "MIDI creation. Unknown el_type" lines for directives it ignores; filter them out.
const abcjs = require(process.argv[2]);

function parseSmf(bytes) {
  let p = 0;
  const u8 = () => bytes[p++];
  const u16 = () => (u8() << 8) | u8();
  const u32 = () => ((u8() << 24) >>> 0) + (u8() << 16) + (u8() << 8) + u8();
  const vlq = () => { let r = 0; for (;;) { const b = u8(); r = (r << 7) | (b & 0x7f); if (!(b & 0x80)) return r; } };
  const tag = () => String.fromCharCode(u8(), u8(), u8(), u8());
  if (tag() !== 'MThd') throw new Error('no MThd');
  const hlen = u32(); const format = u16(); const ntracks = u16(); const division = u16(); p += hlen - 6;
  const events = []; let eot = 0;
  for (let t = 0; t < ntracks; t++) {
    if (tag() !== 'MTrk') throw new Error('no MTrk');
    const len = u32(); const end = p + len; let tick = 0; let running = 0;
    while (p < end) {
      tick += vlq();
      const b = u8();
      if (b === 0xff) {
        const type = u8(); const l = vlq(); const data = Array.from(bytes.slice(p, p + l)); p += l;
        events.push({ t, tick, meta: type, data }); if (type === 0x2f) eot = Math.max(eot, tick); continue;
      }
      if (b === 0xf0 || b === 0xf7) { p += vlq(); continue; }
      let status; let d1;
      if (b & 0x80) { status = b; running = b; d1 = u8(); } else { status = running; d1 = b; }
      const kind = status & 0xf0; const d2 = (kind === 0xc0 || kind === 0xd0) ? 0 : u8();
      events.push({ t, tick, ch: status & 15, kind, d1, d2 });
    }
    p = end;
  }
  const tempos = events.filter(e => e.meta === 0x51).map(e => ({ tick: e.tick, us: (e.data[0] << 16) | (e.data[1] << 8) | e.data[2] }));
  const toSec = (tick) => {
    let s = 0, prev = 0, us = 500000;
    for (const x of tempos) { if (x.tick > tick) break; s += (x.tick - prev) * us / division / 1e6; prev = x.tick; us = x.us; }
    return s + (tick - prev) * us / division / 1e6;
  };
  const open = new Map(); const notes = []; const other = {}; const hanging = []; let offsWithoutOn = 0;
  const sorted = events.map((e, i) => ({ ...e, i })).sort((a, b) => a.tick - b.tick || a.i - b.i);
  for (const e of sorted) {
    const k = `${e.t}:${e.ch}:${e.d1}`;
    if (e.kind === 0x90 && e.d2 > 0) { if (!open.has(k)) open.set(k, []); open.get(k).push(e); }
    else if (e.kind === 0x80 || (e.kind === 0x90 && e.d2 === 0)) {
      const q = open.get(k);
      if (q && q.length) { const on = q.shift(); notes.push({ ch: e.ch, pitch: e.d1, vel: on.d2, on: toSec(on.tick), off: toSec(e.tick) }); }
      else offsWithoutOn++;
    } else if (e.kind !== undefined) { const key = `${e.kind.toString(16)}:${e.d1}`; other[key] = (other[key] || 0) + 1; }
  }
  for (const q of open.values()) for (const on of q) hanging.push({ ch: on.ch, pitch: on.d1, on: +toSec(on.tick).toFixed(3) });
  return { tempos, eotSec: toSec(eot), notes, other, hanging, offsWithoutOn };
}

function synthView(tune) {
  const flat = tune.setUpAudio({}); // what CreateSynth.init uses when the site passes no options
  const meter = tune.getMeterFraction();
  const meterSize = meter.den ? meter.num / meter.den : 1;
  const secPerWhole = tune.millisecondsPerMeasure(flat.tempo) / 1000 / meterSize;
  const notes = []; const roll = new Map(); let rollRaw = 0;
  flat.tracks.forEach((track) => {
    for (const ev of track) {
      if (ev.cmd !== 'note') continue;
      rollRaw++;
      const r = { pitch: ev.pitch, start: Math.round(ev.start * 16), length: Math.max(1, Math.round(ev.duration * 16)) };
      const rk = `${r.pitch}@${r.start}`; if (!roll.has(rk) || r.length > roll.get(rk).length) roll.set(rk, r);
      if (!(ev.duration > 0)) continue;
      const gap = Math.min(ev.gap ? ev.gap : 0, ev.duration * 2 / 3);
      notes.push({ pitch: ev.pitch, vel: ev.volume, cents: ev.cents || 0,
        on: (Math.round(ev.start * 1e6) / 1e6) * secPerWhole,
        off: (Math.round((ev.start + ev.duration - gap) * 1e6) / 1e6) * secPerWhole });
    }
  });
  return { notes, rollNotes: roll.size, rollRaw, musicEndSec: flat.totalDuration * secPerWhole,
    bufferSec: flat.totalDuration * secPerWhole + 0.2 /* abcjs's default fadeLength */ };
}

const r3 = (x) => Math.round(x * 1000) / 1000;
function compare(name, abc) {
  const [tune] = abcjs.parseOnly(abc);
  const s = synthView(tune);
  const [bytes] = abcjs.synth.getMidiFile(abc, { midiOutputType: 'binary' });
  const again = abcjs.synth.getMidiFile(abc, { midiOutputType: 'binary' })[0];
  const deterministic = Buffer.compare(Buffer.from(bytes), Buffer.from(again)) === 0;
  let m;
  try { m = parseSmf(bytes); } catch (e) { console.log(`\n=== ${name}\nMIDI unreadable: ${e.message}`); return; }
  const left = [...m.notes]; const onlySynth = []; const differ = []; let maxOnsetMs = 0;
  for (const n of s.notes) {
    const i = left.findIndex(x => x.pitch === n.pitch && Math.abs(x.on - n.on) < 0.002);
    if (i < 0) { onlySynth.push({ pitch: n.pitch, on: r3(n.on), off: r3(n.off), vel: n.vel }); continue; }
    const x = left.splice(i, 1)[0];
    if (Math.abs(x.off - n.off) > 0.002 || x.vel !== Math.trunc(n.vel))
      differ.push({ pitch: n.pitch, on: r3(n.on), synthOff: r3(n.off), fileOff: r3(x.off), synthVel: n.vel, fileVel: x.vel });
  }
  const so = [...s.notes].sort((a, b) => a.on - b.on || a.pitch - b.pitch);
  const mo = [...m.notes].sort((a, b) => a.on - b.on || a.pitch - b.pitch);
  for (let i = 0; i < Math.min(so.length, mo.length); i++) maxOnsetMs = Math.max(maxOnsetMs, Math.abs(so[i].on - mo[i].on) * 1000);
  console.log(`\n=== ${name}`);
  console.log(JSON.stringify({ synthNotes: s.notes.length, fileNotes: m.notes.length, rollNotes: s.rollNotes, rollRaw: s.rollRaw,
    synthBufferSec: r3(s.bufferSec), musicEndSec: r3(s.musicEndSec), fileEndSec: r3(m.eotSec),
    maxOnsetDiffMs: +maxOnsetMs.toFixed(2), deterministic, synthVelocities: [...new Set(s.notes.map(n => n.vel))].sort((a, b) => a - b) }));
  if (onlySynth.length) console.log('heard today, not in the file:', JSON.stringify(onlySynth.slice(0, 6)), onlySynth.length > 6 ? `(+${onlySynth.length - 6})` : '');
  if (left.length) console.log('in the file, not heard today:', JSON.stringify(left.slice(0, 6).map(x => ({ ...x, on: r3(x.on), off: r3(x.off) }))));
  if (differ.length) console.log('length or velocity differ:', JSON.stringify(differ.slice(0, 6)), differ.length > 6 ? `(+${differ.length - 6})` : '');
  if (m.hanging.length) console.log('file note-ons never turned off:', JSON.stringify(m.hanging));
  if (m.offsWithoutOn) console.log('file note-offs with no sounding note:', m.offsWithoutOn);
  console.log('other channel messages in the file (status:data1 -> count):', JSON.stringify(m.other));
}

const head = (extra = '', q = 'Q:1/4=120', meter = 'M:4/4') => `X:1\n${meter}\nL:1/8\n${q}\n${extra}K:C\n`;
const cases = {
  'site example (chord symbols, Q=90)': `X:1\nT:Example\nM:4/4\nL:1/8\nQ:1/4=90\nK:Dm\n"Dm"D2 F2 A2 d2 | "Bb"B,2 D2 F2 B2 | "C"C2 E2 G2 c2 | "A"A,2 ^C2 E2 A2 |]`,
  'note preview, pitch 60 (previewAbc)': `X:1\nM:4/4\nL:1/8\nQ:1/4=120\nK:C\nC z6 z |]`,
  'plain melody, beat accents': head() + `C D E F G A B c | c B A G F E D C |]`,
  'grace notes': head() + `{g}A2 {ag}B2 {f}c4 | {gag}d8 |]`,
  'staccato at Q=120': head() + `.C2 .D2 .E2 .F2 | .G8 |]`,
  'staccato at Q=80': head('', 'Q:1/4=80') + `.C2 .D2 .E2 .F2 | .G8 |]`,
  'slur and tie': head() + `(CDEF) G2-G2 | G8 |]`,
  'accent, trill, mordent, roll': head() + `!accent!C2 !trill!D4 !mordent!E2 | !roll!F8 |]`,
  'dynamics': head() + `!p!C2 D2 E2 F2 | !f!G2 A2 B2 c2 |]`,
  'tempo change mid-tune': head('', 'Q:1/4=60') + `C2 D2 E2 F2 | [Q:1/4=120] G2 A2 B2 c2 |]`,
  'triplets': head() + `(3CDE (3FGA B2 c2 | c8 |]`,
  'septuplets, 40 bars': head() + Array(40).fill('(7CDEFGAB (7cBAGFED c4').join(' | ') + ' |]',
  'trailing rests': head() + `C2 z6 | z8 |]`,
  'pickup bar': head() + `G | c2 e2 g2 c'2 | c'8 |]`,
  '%%MIDI program 40': head('%%MIDI program 40\n') + `C2 D2 E2 F2 | G8 |]`,
  '6/8, Q:3/8=60': head('', 'Q:3/8=60', 'M:6/8') + `CDE FGA | c3 c3 |]`,
  'two voices': head() + `V:1\nc2 d2 e2 f2 | g8 |]\nV:2 clef=bass\nC,4 G,4 | C,8 |]`,
  'unison across two voices': head() + `V:1\nC4 C4 | C8 |]\nV:2\nC8 | C8 |]`,
  'repeat': head() + `|: C2 D2 E2 F2 :| G8 |]`,
  '%%MIDI drum': head('%%MIDI drum dd 38 38 100 100\n%%MIDI drumon\n') + `C2 D2 E2 F2 | G8 |]`,
  'quarter-tone sharp': head() + `^/C2 D2 E2 F2 | G8 |]`,
  '%%MIDI swing 66': head('%%MIDI swing 66\n') + `CDEF GABc | c8 |]`,
  'pitches 12 to 60': head() + `C,,,,2 C,,,2 C,,2 C,2 | C8 |]`,
};
for (const [name, abc] of Object.entries(cases)) compare(name, abc);
