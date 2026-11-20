# Changelog

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
