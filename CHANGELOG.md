# Changelog

## 0.3.1 — milestone 3 preset level fix (built 2026-11-20, run 44845827670; tested in Logic on the M4, works)

- **Logic test (M4, 2026-11-20):** with a drum loop going in, Init peaks at about 0 dB on Logic's meter; Uber Space,
  Tanh Low Pass, Notch Phaser, Vowel Ah, Talking Formants, Auto Wah and Sympathetic Strings stay at about Init's level,
  none goes into the red or sounds noticeably louder. With a bass line they stay level, Vowel Ah and Talking Formants
  a little quieter. Singing Res on a held note sounds the same as before. **Milestone 3 finished.**

- In the 0.3.0 Logic test, with a drum loop going in, Uber Space came out much louder than Init and Tanh Low Pass
  moderately louder, and Logic's meter went into the red; the presets had been level matched on pink noise only.
- The "input" presets are now level matched on three signals: pink noise, a drum loop (120 BPM kick, snare and hats,
  with a typical mixed loop's spectrum) and a bass line (a filtered saw, C1 to C3), all in `engine/tests/Loudness.h`.
  Each is within 2 dB of Init on all three; only Vowel Ah and Talking Formants are 4 to 5 dB quieter on the bass line
  (they filter out its low notes). New trims: Notch Phaser -0.9 dB, Vowel Ah -1.8, Talking Formants +0.1, Uber Space
  -2.0, Tanh Low Pass -1.1, Auto Wah -0.7, Sympathetic Strings +0.3.
- Band-pass and über presets make drum hits spikier than Init does, so Vowel Ah, Talking Formants, Uber Space,
  Auto Wah, Sympathetic Strings and Singing Res now have Master limit on (the struck presets already had it). It does
  nothing at normal levels and only rounds off the biggest hits. On the drum loop no preset peaks more than 3 dB above
  Init (Init 0.70, the highest 0.98) and none reaches 0 dB on any of the three signals.
- The voice and struck presets are unchanged (they are played by notes and were already matched); Singing Res sounds
  the same with notes alone (its level stays below where Master limit starts).
- Nothing else changed: no control ids, no saved-setting entries, Init, and Pitch Snap, Scale, Key and Quality are
  never touched by a preset. Projects keep their own settings; only choosing a preset again gives the new levels.
- Checks: engine self-test 516 (new: every input preset's loudness and peak on pink noise, the drum loop and the bass
  line against Init; every preset with the drum loop going in plus notes stays at Master limit's ceiling); plugin-layer
  check 247 (PEDWAR) and 236 (PEDWAR Synth); auval and pluginval (strictness 5) passed for both plugins.

## 0.3.0 — milestone 3 (built 2026-11-20, run 44832903530; tested in Logic on both Macs, works except the preset levels on drums)

- **Logic test (2026-11-20).** M4: a project from an older build opens as "Custom" and sounds the same; nothing is
  dimmed; every preset sounds different; the bell and the drone work; the Snap and Scale readouts step correctly;
  über / sum and Quality switch without clicks; "8x heavy" in amber. Intel Mac: CPU meter about 7% at 4x and 15% at
  8x; no clicks switching Quality; Uber Space and Bleep Swarm sound; the preset name survives save and reopen.
  Problem: with a drum loop going in, Uber Space got much louder and Tanh Low Pass moderately louder, and Logic's meter
  went into the red (Notch Phaser, Vowel Ah and Auto Wah only very slightly different); with a bass line every preset
  was even. Fixed in 0.3.1.

- **Stereo sum / über**: über sends α's and β's right sides (the width all-pass blend) to the left output and γ's and
  δ's left sides to the right, +4 dB so it is about as loud as sum; cross-faded over 20 ms.
- **Pitch snap** Off / Semis / Steps, **Scale** (Off + 28) and **Key**: COMPLICATOR's quantizer, on each filter's
  Coarse + Fine, as an interval from C4 (key tracking's reference); a scale works around the note played. Key
  tracking, Offset, Link and the LFOs are added after it. The Coarse readout shows the snapped note.
- **Quality** 2x / 4x / 8x ("8x heavy", amber while chosen): fade out 4 ms, switch, fade in; the same latency at every
  Quality (a small delay at the project rate evens them out).
- **Presets**: Init plus 14 factory presets, level matched to Init within 0.3 dB (K-weighted), none peaking above
  0.95; none touches Pitch Snap, Scale, Key or Quality. The menu remembers the choice; projects from before 0.3.0
  show "Custom".
- **Polish**: milestone 3 controls live, M3 tags removed; Coarse, Q and Offset readouts rounded (no more
  "277.183 Hz"); the latency reported to Logic is exact (it was about ¾ of a sample too long).
- No control ids changed and no saved-setting entries added. Projects saved with 0.1.x / 0.2.0 open with the same
  settings and the same sound, with the Preset menu showing "Custom".
- Checks: engine self-test 460 (new: latency equal and measured at every Quality and sample rate, responses, Osc level
  and pitch and Res at 2x and 8x, aliasing falling with Quality, every Quality switch without a click and with pitch
  and level kept, quick back-and-forth switching; über routing, level and phase, loudness against sum, no click;
  Snap Off unchanged on 1000 random settings, Semis, Steps, scales, keys, key tracking, Link, Offset, glide; every
  preset's level, peak, ranges, and playing at 2x / 8x and 44.1 / 96 kHz; random automation now also of Stereo, Snap,
  Scale, Key and Quality); plugin-layer check 229 (PEDWAR) and 218 (PEDWAR Synth) (new: Init = the engine's defaults,
  every preset sets exactly its settings and leaves the player's four alone, the choice saved and restored, a 0.2.0
  project shows Custom with its settings unchanged, every preset plays through the plugin, all 73 panel controls
  live, the Preset menu, "8x heavy", the snapped Coarse readout, the rounded readouts).

## 0.2.0 — milestone 2 (built 2026-11-20, run 44757759235; tested in Logic on both Macs, works)

- **Morph** and **Morph ends**: the stereo morph signal is the filter's notch moved to ×4 … ×¼ of the cutoff (left
  leans low-pass, right high-pass, unity pass band), pure LP fully left, HP fully right, or BP with Morph ends on BP
  (90 degrees from the left end: sine <-> cosine on a self-oscillating filter). Readout LP, ×4, ×2, ×1, ×½, ×¼, HP / BP.
- **Width**: equal-power blend with a two-stage all-pass copy (corner at the cutoff) on one side, right on α and β,
  left on γ and δ.
- **Four triangle LFOs** with **Rate** (α at Rate; β, γ, δ at ×1.127, ×0.871, ×1.293), and the full **bus grid**:
  width (sweeps the filter's own all-pass ±2 octaves), FM (moves the previous filter's cutoff, times its **FM amt**,
  ±3 octaves at full), anim (FM, Q and Morph each from one of the other LFOs, plus a slight speed nudge). Every bus
  change cross-faded over 20 ms.
- **Link** (α leads β, δ leads γ): the follower adds the leader's Coarse, Fine, FM amt, Q, Width and Morph, measured
  from the leader's defaults, plus the leader's LFO movement.
- **Offset** (S-curve, ±48 semitones) and **Pitch γδ** all / split (notes from middle C up move α and β, below it γ
  and δ).
- **Modulation rings** on Coarse, Q and Morph: the LFO swing as a dotted ring and the current position as a dot.
- Panel: milestone 2 controls live, M2 tags removed. The Morph ends switch moved 3 units down and the Morph and Width
  knobs 3 up, so the Morph readout is no longer hidden behind the switch.
- No control ids changed and no saved settings added (the 75-id list is unchanged); projects saved with 0.1.x open
  with the same settings. Their sound changes only where milestone 2 now does something with a reserved default:
  the default Width of 35% on the width bus now adds slowly moving stereo to each filter's stereo morph output.
- Checks: engine self-test 203 (new: Morph responses and marks, sine / cosine ends, width all-pass level and phase,
  equal-power blend, LFO speeds, FM depth, polarity and routing, anim, Link, Offset, split, clicks on every new
  control, random automation of every control); plugin-layer check 137 (PEDWAR) and 126 (PEDWAR Synth); auval and
  pluginval (strictness 5) passed for both plugins. CPU worst case about 5.1% of one core on the build machine.
- Logic test results (M4 Mac mini, Logic 11.2.2, and Intel Mac, Logic 10.7.8; same on both): only the milestone 3
  controls are dimmed; Morph sweeps smoothly from LP through the notch to HP with no clicks; Width widens and moves the
  sound; beta's LFO on FM sweeps alpha's cutoff with a moving ring on Coarse; anim adds rings on Q and Morph with no
  clicks or crackles; Offset moves the cutoff with a semitone readout. Logic's CPU meter on the Intel Mac about 7%,
  never above 10%. Milestone 2 finished.

## 0.1.1 — milestone 1 fix (built 2026-11-20; tested in Logic on both Macs, works)

- Logic only sends MIDI to a MIDI-controlled effect loaded in the **instrument slot** of a Software Instrument track
  (AU MIDI-controlled Effects), with the audio to filter chosen in its Side Chain menu; nothing in an Audio FX slot
  gets MIDI. The README and INSTALL.txt now describe that set-up instead of a "side-chain MIDI menu", which does not
  exist for third-party effects.
- α and β now also take the side chain while the track input has been silent for 2 seconds (γ and δ already did),
  as a safety net. In the Logic tests, the audio picked in the Side Chain menu of the instrument slot reached all four
  filters (on the track input, judging by the labels, which stayed on "track in").
- The input label under each filter's name now clearly shows what the filter hears: "side chain" in the filter's
  colour with a dot while it takes the side chain (it was only a slightly lighter grey).
- Logic test results (M4 Mac mini, Logic 11.2.2, and Intel Mac, Logic 10.7.8; same on both): PEDWAR loads from AU
  MIDI-controlled Effects in a Software Instrument track's instrument slot; the track chosen in the Side Chain menu is
  heard through PEDWAR; every MIDI note strikes the filter and flashes the strike light; clicking the light (empty
  ring) stops the strike. Milestone 1 finished.
- Engine self-test and plugin-layer check: new checks for the instrument-slot routing and the labels' state.
  No control or saved setting changed.

## 0.1.0 — milestone 1 (built 2026-11-19; superseded by 0.1.1)

- First build: repo layout, build workflow (engine self-test, plugin-layer check, universal AU + VST3 for both plugins,
  ad-hoc signing, auval, pluginval, package with installer and INSTALL.txt, private draft release), same installer as
  the other particledots plugins.
- Two plugins from one engine and one control list: PEDWAR (MIDI-controlled effect, AU `aumf`) and PEDWAR Synth
  (instrument, AU `aumu`, optional side-chain input).
- The real panel, matching the approved draft. Controls for milestones 2 and 3 are in place, dimmed and tagged.
  One small change from the draft: the LFO bus grid's dots sit 8 units further left so the row names (width, FM,
  anim) no longer overlap the δ column.
- Full control list fixed (75 controls), including everything for milestones 2 and 3 (Pitch Snap, Scale, Key,
  Quality and the rest), plus a "preset" entry in the saved settings.
- Four filters with Filter / Res / Osc modes, Gain, In limit (tanh), Coarse, Fine, Q, Out limit, Key track, Output tap
  (stereo morph = notch until milestone 2), MIDI strike with per-filter on/off and flashing lights, Levels, master mix,
  Master limit, track input and side-chain routing (γ and δ take the side chain while it carries audio; in PEDWAR
  Synth it feeds all four).
- 4x oversampling with steep filters in and out, band-limited shapers and limiters, zero-delay-feedback filter core,
  smoothed controls, cross-faded switches.
- Engine self-test: 120 checks (responses, cutoff accuracy, Osc level / pitch / purity across the range, Res
  self-oscillation, key tracking, strikes, routing, limiters, clicks on every switch, random automation at three
  sample rates, fast sweeps at Q 50, CPU). Plugin-layer check: 139 checks for PEDWAR, 134 for PEDWAR Synth.
  pluginval (strictness 5, with the panel tests) passed on both VST3s on Linux before the first push.
