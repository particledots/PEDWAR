# PEDWAR

An original AU / VST3 quad resonant filter for Logic Pro, in two forms built from the same engine and the same list of
controls: **PEDWAR**, an audio effect played by MIDI, and **PEDWAR Synth**, an instrument. Inspired by the Instruō
ceithir (a quad state-variable filter / resonator). It is not a copy, contains no Instruō code or artwork, and is not
affiliated with Instruō. The ceithir is an analog module with no published firmware; its manual was used only to
understand what the controls do. "Pedwar" is Welsh for "four".

Status: **milestone 3 of 3: version 0.3.0 tested in Logic on both Macs on 2026-11-20; 0.3.1 (preset levels re-matched) built on 2026-11-20, waiting for the Logic test on the M4.** Milestones 1
and 2 (0.2.0) were tested in Logic on the M4 Mac mini (Logic 11.2.2) and the Intel Mac (Logic 10.7.8) on 2026-11-20.

## What is new in milestone 3

- **Stereo sum / über** (master strip). **sum**: the normal stereo mix of all four. **über**: the left output is α's
  and β's right sides (their width all-pass blend) and the right output is γ's and δ's left sides (theirs); the plain
  sides are left out. Both are raised by 4 dB so über is about as loud as sum (it would drop 3 to 6 dB, with half the
  sources on each side). The switch cross-fades over 20 ms. With Width turned up and the LFOs on **width**, über is the
  widest, most moving stereo PEDWAR makes.
- **Pitch snap** (master strip): **Off / Semis / Steps**, **Scale** (Off + the same 28 scales as the other particledots
  plugins) and **Key** (C to B), the same quantizer as COMPLICATOR. They act on each filter's own tuning, Coarse and
  Fine together; key tracking, Offset, Link and the LFOs are added afterwards and are never snapped. Key tracking
  already works relative to middle C, so the tuning counts as an interval from C4 (Coarse at C4 = the note you play):
  **Semis** lands on whole semitones; **Steps** on octaves and fifths (0, ±7, ±12, ±19, ±24 ... semitones from C4);
  with a **scale** on, only where the filter's note is in that scale and key (the filter's note is the note you play
  when its Key track is on, otherwise C4; that note and its octaves are always allowed). With a scale on and Snap Off
  it still moves in whole notes. While snapping, the **Coarse readout shows the note really played** (Coarse + Fine,
  snapped). Changing any of them glides the pitch over 3 ms, no click. All Off by default; with Snap and Scale Off the
  tuning is exactly as before (checked on 1000 random settings).
- **Quality menu** (top bar): **2x / 4x / 8x** oversampling. 4x stays the default; **8x is marked "heavy"** in the list
  and the menu's text turns amber while it is chosen. Changing it while playing fades the sound out over 4 ms, switches,
  and fades back in (about 10 ms in all, no click); a self-oscillating filter keeps its pitch and level. The latency is
  the same at every Quality, so Logic never has to re-align the track. Aliasing of a hard-driven In limit at 7.3 kHz:
  2x about -50 dB, 4x about -90 dB, 8x about -97 dB.
- **Preset menu** (top bar): **Init** plus 14 factory presets of my own (the ceithir manual has no example patches).
  Init and every preset leave **Pitch Snap, Scale, Key and Quality** alone. Every preset is level matched to Init
  (K-weighted loudness, the measure behind LUFS). The "input" presets are measured with three signals going in, pink
  noise, a drum loop and a bass line, and are within 2 dB of Init on each (since 0.3.1; 0.3.0 matched pink noise only).
  The exception is the bass line through the two formant presets, Vowel Ah and Talking Formants, which are 4 to 5 dB
  quieter than Init on it because they filter out the bass's low notes. Band-pass and über presets make drum hits
  spikier than Init does, so the resonant ones (Vowel Ah, Talking Formants, Uber Space, Auto Wah, Sympathetic Strings,
  Singing Res and the three struck presets) have **Master limit** on: it does nothing at normal levels and rounds off
  only the biggest hits towards 0 dB. On the test drum loop no preset peaks more than 3 dB above Init and none goes
  over 0 dB. The other presets are measured with no input and middle C held or struck four times a second, within 1
  dB of Init. A project remembers
  which preset was chosen (if you then move a knob the menu still shows it); a project saved before 0.3.0 shows
  "Custom", with its settings exactly as saved. The same presets are in Logic's own preset menu (Factory).

  | Preset | What it is | Play it with |
  |---|---|---|
  | Notch Phaser | four notches drifting slowly (FM bus) | any audio |
  | Vowel Ah | four band-passes on the formants of "ah" | noise, drums, synths |
  | Talking Formants | formant band-passes whose Q and Morph wander (anim) | any audio |
  | Uber Space | leaning notches at full width, swept, über stereo | any audio |
  | Tanh Low Pass | two driven low-passes an octave apart | bass, drums |
  | Auto Wah | two band-passes swept by the LFOs at about 2.2 Hz | guitar, keys, drums |
  | Sympathetic Strings | high-Q band-passes on the note and its harmonics; notes pluck them | any audio plus notes |
  | Sine Drone | four steady sines on the note's harmonics | a held note |
  | Struck Bell | ringing band-passes on bell partials, velocity sets the hit | notes C4 and up |
  | Mallets | shorter rings on marimba-like partials | notes around C4 to C6 |
  | Tom Cluster | four low tuned drums | low to middle notes |
  | Singing Res | Res mode past its singing point, slowly animated | a held note |
  | Split Drone | notes from middle C up play α and β, notes below play γ and δ | one note in each half |
  | Bleep Swarm | four sines frequency-modulating fast, über stereo | a held note |

  The "voice" presets (Sine Drone to Bleep Swarm) sound on their own, so they also suit PEDWAR Synth; in PEDWAR the
  side-chain audio colours them too. Sine Drone, Singing Res, Split Drone and Bleep Swarm keep sounding (Osc and Res
  modes have no envelope); the struck ones ring and die away.
- **Polish**: every milestone 3 control is live and the M3 tags are gone. The Coarse, Q and Offset readouts no longer
  show a long row of digits (for example "C#4 277.183 Hz" is now "C#4 277 Hz", a Q of 23.456 is "23"). The latency
  Logic is told is now exact (it was about ¾ of a sample long since 0.1.0, harmless).
- **Old projects**: no control id changed and no saved-setting entry was added (the 75-id list is unchanged; the
  "preset" entry has been there since 0.1.0). Projects saved with 0.1.x or 0.2.0 open with the same settings and the
  same sound (Stereo, Snap, Scale and Key were stored at their defaults, sum and Off), at 4x, with the Preset menu
  showing "Custom".

## What works (all milestones)

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
  - The sine is cleanest on the LP tap, as on the module. The notch (and "stereo morph" at ×1, see below) takes the
    sine out, by its nature: use LP, BP or HP, or turn Morph away from ×1, to hear a self-oscillating filter.
- **Gain** (-inf to +12 dB, 0 dB at about 60% of the knob) and **In limit** (off / tanh): a soft tanh shaper on the
  filter's input. Small signals pass at the same level; around -6 dB of Gain it gently tames loud peaks; at full Gain
  it shapes hard.
- **Coarse** (16 Hz to 20 kHz; the readout shows the nearest note and the frequency), **Fine** (±100 cents).
  The cutoff is kept below 20 kHz and below 45% of the project's sample rate.
- **Key track** (off / on, on by default): the MIDI note moves the cutoff, 1 semitone per note, relative to middle C
  (note 60). Last note wins; releasing it goes back to a note still held; with no note held the pitch stays where it
  was. To play Osc mode in tune, set Coarse to C4 (261.6 Hz).
- **Output tap**: stereo morph (the normal path, with Morph and Width below), or LP, BP, HP or notch, the same on
  both sides.
- **Morph** (stereo morph only). Left to right the readout goes **LP**, ×4, ×2, **×1**, ×½, ×¼, **HP** (or **BP**).
  At ×1 it is the plain symmetrical notch. Between the marks the notch moves to that multiple of the cutoff and the
  response leans low-pass (×2, ×4, the highs drop about 24 dB at ×4) or high-pass (×½, ×¼, the lows drop the same),
  with the pass band kept at unity. The last stretch at each end fades the notch out: fully left is a pure low-pass,
  fully right a pure high-pass. **Morph ends** BP makes the right end a band-pass instead: it is 90 degrees from the
  left end, so a self-oscillating filter shifts between sine and cosine across the knob, at the same level
  (LP·HP: 180 degrees).
- **Width** (stereo morph only, 0-100%, 35% by default). One side is an equal-power blend of the morph signal and a
  copy through two all-pass stages whose corner sits at the filter's cutoff: the right side on α and β, the left on
  γ and δ. Full width = that side is the all-pass copy alone (same level, 180 degrees turned at the cutoff). The
  filter's own LFO sweeps the all-pass corner ±2 octaves while that LFO's bus is on **width**; otherwise it stands
  still.
- **LFOs and Rate.** Four triangle LFOs; **rate** (0.01-20 Hz) sets α's, and β, γ and δ run at ×1.127, ×0.871 and
  ×1.293 of it, so they drift against each other. They start a quarter cycle apart.
- **Bus grid** (modulation, one column per LFO, one row picked per column; all changes cross-faded over 20 ms):
  - **width**: the LFO sweeps its own filter's all-pass (above).
  - **FM**: the LFO moves the previous filter's cutoff (α LFO -> δ, β -> α, γ -> β, δ -> γ), times that filter's
    **FM amt** (bipolar; ±100% = ±3 octaves).
  - **anim**: the filter's FM comes from the next LFO (times its FM amt), its Q from the one after (±1.5 octaves of
    Q) and its Morph from the third (±0.25 of the knob, about ±1 octave of the notch); the next LFO also nudges this
    column's LFO speed (about ±9%). With FM amt at 0, anim moves only Q and Morph.
- **Modulation rings**: a dotted ring around **Coarse**, **Q** and **Morph** shows how far the LFOs can swing the
  setting, and a dot on it where they have it now. No ring = not modulated.
- **Link** (α leads β, δ leads γ; off by default). The follower adds the leader's Coarse, Fine, FM amt, Q, Width and
  Morph, measured from the leader's own defaults (α 110 Hz, δ 1.76 kHz, Fine 0, FM amt 0, Q 2, Width 35%, Morph ×1),
  plus the leader's LFO movement of cutoff, Q and Morph. So at the defaults Link changes nothing; turning α's Coarse
  up an octave moves both α and β up an octave. The follower's own knobs set its starting point.
- **Offset** (master): moves all four cutoffs, 0 at the centre, S-curve: fine near the centre, big steps at the ends
  (±6 semitones at half way, ±48 fully). Readout in semitones.
- **Pitch γδ** (master): **all** = key track (where on) moves all four filters with the last note. **split** = notes
  from middle C up move α and β, notes below it move γ and δ; each half keeps its own last note.
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
- **The panel** is the approved draft, every control live. The window opens at about 780 x 700
  and keeps the draft's proportions when you drag the corner. Two small changes from the draft: the bus grid's dots sit
  8 units left (so the row names do not overlap δ), and the Morph ends switch sits 3 units lower with the Morph and
  Width knobs 3 higher (so the Morph readout is not hidden).
- **Clean under automation.** Everything runs at 4x the project's sample rate (2x or 8x with the Quality menu) with
  steep filters on the way in and out,
  and the shapers and limiters are band-limited (antiderivative anti-aliasing). The core is a "zero-delay feedback"
  state-variable filter that stays stable however fast the cutoff and Q move. Every knob is smoothed and every switch is
  cross-faded over 5 ms (the bus grid and Link over 20 ms). The self-test throws random automation of every control at
  it every 32 samples at 44.1, 48 and 96 kHz, and sweeps the cutoff 16 Hz <-> 20 kHz every block at Q 50 in all three
  modes: always finite and bounded (since milestone 3 also randomly switching Stereo, Snap, Scale, Key and Quality).
  It also changes each milestone 2 and 3 control suddenly while a sine plays: no clicks.
- **Latency**: 39 samples at 48 kHz, the same at every Quality (the oversampling filters). Logic compensates for it
  automatically.
- **CPU** (% of one core at 48 kHz, worst case: every limiter on, LFOs on every bus, both Links, über, Snap and a scale,
  all four filters busy, side chain playing), measured by the engine self-test on a Linux machine (the Actions
  run's "ENGINE-CPU" line): 2x about 3.9%, 4x about 5.8%, 8x about 10.4%. In the milestone 2 Logic test on the Intel Mac (4x),
  Logic's CPU meter showed about 7%, never above 10%; the milestone 3 check on the Intel Mac is below once done.
- **Every control was fixed in the first build, so saved projects never change:** 75 controls, the same list in both
  plugins, and a "preset" entry in the saved settings for the Preset menu.

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code: the state-variable core, Morph and the width all-pass, the LFOs and bus grid, Link, the shapers and limiters, up- and down-sampling at 2x / 4x / 8x, sum / über, Pitch Snap (`Pitch.h`, `Scales.h`) and the factory presets (`Presets.h`). Reusable for later plugins. |
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
