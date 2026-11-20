# PEDWAR

An original AU / VST3 quad resonant filter for Logic Pro, in two forms built from the same engine and the same list of
controls: **PEDWAR**, an audio effect played by MIDI, and **PEDWAR Synth**, an instrument. Inspired by the Instruō
ceithir (a quad state-variable filter / resonator). It is not a copy, contains no Instruō code or artwork, and is not
affiliated with Instruō. The ceithir is an analog module with no published firmware; its manual was used only to
understand what the controls do. "Pedwar" is Welsh for "four".

Status: **milestone 1 of 3 (version 0.1.1) finished: tested in Logic on the M4 Mac mini (Logic 11.2.2) and the Intel Mac (Logic 10.7.8) on 2026-11-20.**

## What works (milestone 1)

Signals inside are plus/minus 1 at full scale.

- **Two plugins.** PEDWAR is a MIDI-controlled effect (AU type `aumf`); PEDWAR Synth is an instrument (AU type
  `aumu`), for self-oscillating patches played from the keyboard, with an optional side-chain audio input. Presets
  and saved settings are the same in both.
- **Two ways to use PEDWAR in Logic.** Logic never sends MIDI to a plug-in in an Audio FX slot (there is no MIDI
  side-chain menu for third-party effects), so:
  - **Played by MIDI** (strike, key track): make a **Software Instrument** track, click its instrument slot and choose
    **AU MIDI-controlled Effects > particledots > PEDWAR**. In PEDWAR's **Side Chain** menu (top right of its window)
    choose the audio track to filter, and mute that audio track so only PEDWAR's version is heard (a muted track
    still feeds PEDWAR). The notes in the instrument track's regions, or played on the keyboard with that
    track selected, play PEDWAR. All four filters hear the chosen track.
  - **Plain filter, no MIDI**: put it in an **Audio FX** slot (Audio Units > particledots > PEDWAR). α and β filter
    the track; the Side Chain menu can feed γ and δ another track.
- **Four filters**, α alpha, β beta, γ gamma, δ delta, each a state-variable filter with LP, BP, HP and notch.
  **Mode**:
  - **Filter**: a normal filter. **Q** runs from 0.5 to 50 (0.707 is the classic -3 dB at the cutoff; LP and BP
    peak at Q times the input at the cutoff, so high Q is loud: that is what Out limit is for).
  - **Res**: the same, but from Q of about 17 upwards the filter starts to sing a sine by itself, and louder the
    further Q goes (about -6 dB at Q 50). It takes a moment to build up from silence, like an analog filter.
  - **Osc**: a steady sine at the cutoff frequency, the same level (-6 dB on LP) across the whole range, in tune
    (the self-test measures within 2 cents and harmonics more than 50 dB down). Input audio and strikes still push
    it about. Q sets how firmly the level is held: low Q holds it tight, high Q lets strikes swell it more.
  - The sine is cleanest on the LP tap, as on the module. The notch (and "stereo morph", see below) takes the sine
    out, by its nature: use LP, BP or HP to hear a self-oscillating filter.
- **Gain** (-inf to +12 dB, 0 dB at about 60% of the knob) and **In limit** (off / tanh): a soft tanh shaper on the
  filter's input. Small signals pass at the same level; around -6 dB of Gain it gently tames loud peaks; at full Gain
  it shapes hard.
- **Coarse** (16 Hz to 20 kHz; the readout shows the nearest note and the frequency), **Fine** (±100 cents).
  The cutoff is kept below 20 kHz and below 45% of the project's sample rate.
- **Key track** (off / on, on by default): the MIDI note moves the cutoff, 1 semitone per note, relative to middle C
  (note 60). Last note wins; releasing it goes back to a note still held; with no note held the pitch stays where it
  was. To play Osc mode in tune, set Coarse to C4 (261.6 Hz).
- **Output tap**: stereo morph (the normal path), LP, BP, HP or notch, to both sides. **Until milestone 2, "stereo
  morph" is a plain notch.**
- **Out limit** (off / on): an aggressive soft limiter on the filter's output (linear up to about -9 dB, flat at
  about -3 dB).
- **Strike**: every MIDI note-on pings every filter whose strike is on; velocity sets how hard. The filter rings at its
  own frequency (in Filter mode with a low Q it is just a click; raise Q to hear a ring). The **strike light** flashes on
  each strike; **click it** to switch that filter's strike on or off (an empty ring = off).
- **Levels** (one fader per filter, -inf to +6 dB, -12 dB by default so the four together are about the input's level),
  the master stereo mix, and **Master limit** (off / on): very linear at low levels, it rounds off towards 0 dB when
  pushed.
- **Inputs.** In PEDWAR, α and β take the track audio. γ and δ take Logic's **side chain** while it carries audio,
  and the track audio otherwise (the side chain counts as present until it has been silent for 2 seconds; the change
  is cross-faded over 20 ms). If the track input has been silent for 2 seconds while the side chain plays, α and β
  take the side chain too. In the instrument slot of a Software Instrument track, Logic delivers the audio you pick in
  the Side Chain menu on PEDWAR's normal (track) input, as far as the labels in the Logic tests show, so all four
  filters hear it as "track in". Under each filter's name, "track in" or "side chain" says what it is hearing; when
  it is the side chain, the label turns the filter's colour with a dot in front. In PEDWAR Synth there is no track audio: the side chain (if one is
  chosen) feeds all four.
- **The panel** is the approved draft: every control of milestones 2 and 3 is in place, dimmed and tagged M2 or M3.
  The window opens at about 780 x 700 and keeps the draft's proportions when you drag the corner.
- **Clean under automation.** Everything runs at 4x the project's sample rate with steep filters on the way in and out,
  and the shapers and limiters are band-limited (antiderivative anti-aliasing). The core is a "zero-delay feedback"
  state-variable filter that stays stable however fast the cutoff and Q move. Every knob is smoothed and every switch is
  cross-faded over 5 ms. The self-test throws random automation of every control at it every 32 samples at 44.1, 48 and
  96 kHz, and sweeps the cutoff 16 Hz <-> 20 kHz every block at Q 50 in all three modes: always finite and bounded.
- **Latency**: about 39 samples at 48 kHz (the oversampling filters). Logic compensates for it automatically.
- **CPU** (Linux build machine, % of one core at 48 kHz): about 1.4% as it opens; about 4.7% with every limiter on,
  all four filters busy and the side chain playing. Not yet measured on the Intel Mac (milestone 3).
- **Reserved from the start, so saved projects never change:** every control of milestones 2 and 3 (75 controls in all),
  including Pitch Snap, Scale (Off + the same 28 scales as the other particledots plugins), Key and Quality, and a
  "preset" entry in the saved settings for the Preset menu (milestone 3; it has Init only for now).

## Coming

- **Milestone 2**: Morph and Morph ends, Width with the all-pass stereo enhancement, FM amt, the four LFOs with Rate and
  the full bus grid (Width / FM / Anim), Link, Offset, Pitch γδ split, modulation rings on the knobs.
- **Milestone 3**: Über stereo, Pitch Snap with the 28 scales and Key, the Quality menu (2x / 4x / 8x), presets, a CPU
  check on the Intel Mac, final polish.

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code: the state-variable core, the shapers and limiters, up- and down-sampling. Reusable for later plugins. |
| `plugin/` | The plugin wrappers (both plugins, from the same files): what Logic sees, the list of controls (`Parameters`), and the panel (`PluginEditor`, `Controls`, `Style`). |
| `check/` | `PedwarCheck` and `PedwarSynthCheck`: console programs that test the plugin layer (controls, saved settings, audio, MIDI, side chain, panel) without a host. |
| `tools/install/` | The same installer as the other particledots plugins (unchanged). |
| `.github/workflows/` | The automatic build. |

The wrapper hands the engine the control values, the notes and the audio; the engine hands audio back.
Nothing in `engine/` knows about knobs or Logic.

## Getting the plugin

1. Open the **Actions** tab of this repo and click the latest **Build PEDWAR** run.
2. At the bottom, under **Artifacts**, download **PEDWAR-macOS** and unzip it (twice: the download is a zip
   containing `PEDWAR-macOS.zip`).
3. Inside the `PEDWAR-macOS` folder are `PEDWAR.component`, `PEDWAR Synth.component`, `PEDWAR.vst3`,
   `PEDWAR Synth.vst3`, `INSTALL.txt`, the auval and pluginval logs and `Install-Unofficial-Plugin.command`.
4. In Terminal, once, clear the download block: `xattr -dr com.apple.quarantine ~/Downloads/PEDWAR-macOS/*`
   (adjust the path if you unzipped elsewhere).
5. **Easy install (Logic, Audio Unit):** double-click `Install-Unofficial-Plugin.command`. It installs both plugins
   for all users, asks for your Mac password once and restarts the AU registrar.
   Or by hand: copy both `.component` files to `~/Library/Audio/Plug-Ins/Components/` and both `.vst3` files to
   `~/Library/Audio/Plug-Ins/VST3/`.
6. Restart Logic. If it was already scanned, Logic Pro > Settings > Plug-In Manager > Reset & Rescan Selection.

PEDWAR appears in the instrument slot of a Software Instrument track under **AU MIDI-controlled Effects >
particledots > PEDWAR** (to play it by MIDI) and in an Audio FX slot under **Audio Units > particledots > PEDWAR**
(as a plain filter); PEDWAR Synth under **AU Instruments > particledots > PEDWAR Synth**. The same package also sits in a private draft
release of this repo (only people with access see it).

## Notes

- Built as a universal binary (Apple Silicon and Intel), macOS 11 or newer. Tested targets: M4 Mac mini (macOS Tahoe,
  Logic Pro 11.2.2) and an Intel Mac (macOS 12.7.8, Logic Pro 10.7.8).
- Signed ad-hoc only (no Apple Developer account), hence the quarantine step above.
- Fixed identifiers (never to be changed): names PEDWAR and PEDWAR Synth, bundle ids `com.particledots.pedwar` and
  `com.particledots.pedwarsynth`, manufacturer code `Pdot`, plugin codes `Pdwr` (AU type `aumf`) and `PdwS`
  (AU type `aumu`). Internal names `Pedwar` and `PedwarSynth`. The 75 control ids are listed in
  `check/golden/layout-0.1.0.txt`, which the build checks on every push.
- Double-click a knob or fader to return it to its default; hold Shift while dragging for finer moves. Drag the
  bottom-right corner to resize the window.
