# Transform Changelog

## 1.43.16 — FREEZE panel spacing — 26 November 2026

- Equalise both FREEZE selector panels at 116 px; retain the 6 px gaps and recenter the existing two-row knob grid within the remaining 574 px panel. No DSP or parameter changes.

## 1.43.15 — FREEZE selector layout — 26 November 2026

- Group ALGORITHM with DIRECTION and MODE with VOICES; align headings, radio rows and LED/text columns.
- Show VOICES only in Micro-loop MIDI mode, retaining the selected voice mode when hidden. DSP and parameter definitions unchanged.

## 1.43.14 — Micro-loop voice modes and interface refinement — 26 November 2026

- Added last-note-priority Mono and Legato Micro-loop MIDI voice modes and sequencer/MIDI-learnable GLIDE (0–500 ms). Poly remains the default.
- Moved FREQ SYNC beneath DELAY and tidied SIDEBAND spacing.
- Reformatted README into shorter paragraphs and module-specific sections.
- Retained existing PITCH algorithms and control availability.

## 1.43.13 — Enable module randomisation — 26 November 2026

- Enable the reserved main Randomise button and generate new values for the selected module’s visible, enabled rotary parameters using their actual parameter ranges and host gestures. Preserve power/modes, inactive harmony voices, MIX/OUT, gain staging, tuning reference, noise gate, MIDI mappings, saved presets and sequencer settings; limit random feedback, voice levels, EQ boosts and blend.
- All DSP and unrelated GUI remain unchanged.

## 1.43.12 — Alternator and relative frequency shift — 26 November 2026

- Add an optional PITCH Alternator engine using alternating positive/negative manual pitch ratios and pitch-aligned overlapping grains.
- Add FREQ RELATIVE selector and independent SHIFT percentage (±200% of detected fundamental); retains fixed-Hz SHIFT separately. MIDI Learn and sequencer target registration included.
- Match the off-state Randomise, MIDI Learn and SEQ foreground colour to the knob-label grey. Randomise remains reserved and disabled.
- FREEZE and other engines unchanged.

## 1.43.11 — Micro-loop sound refinement — 26 November 2026

- Replace linear seam correction with a smooth overlapping periodic waveform, cubic interpolation and frequency-aware periodic smoothing. Improve capture selection around POSITION using energy and endpoint matching; wait for useful transient audio.
- Smooth pitch/formant/SMOOTH changes, lengthen Live capture crossfades and shape note envelopes. Remove DC before each voice envelope. Cache stable cycles incrementally to reduce held-note processing costs; no new parameters or reported latency.
- Granular and Shaker remain sample-exact in standalone comparisons. All four sample rates, mono/stereo integration, extreme control changes and targeted sanitizer checks passed. Windows VST3 build, listening quality and PHI CPU remain unverified.

## 1.43.10 — MIDI Learn and bypass separation — 26 November 2026

- Keep the existing internal power parameter ID and state, but do not advertise it as the native VST3 bypass flag. JUCE supplies a separate native host bypass; both existing processing callbacks capture MIDI CCs before processing or early return. Native bypass still depends on the host delivering callbacks and MIDI.
- Retain existing DSP, MIDI mapping storage and CC capture implementation. Host parameter display name is Internal Bypass.
- Update reusable AGENTS.md and README. Windows compilation and actual PHI bypass/CC routing require verification.

## 1.43.9 — Creative FREEZE algorithms — 26 November 2026

- Add Micro-loop pitched waveform capture and audio-driven Shaker percussion, selectable alongside the original Granular algorithm.
- Add FORMANT, ATTACK and RELEASE as continuous targets; new algorithm choice uses the standard radio controls and MIDI Learn registration.
- Arrange FREEZE in three panels with compact two-row knobs; dim inapplicable controls.
- Use the existing capture storage and preallocated per-voice waveform tables. No additional reported latency. Algorithm changes reset the capture.

## 1.43.8 — Muted MIDI socket icon — 26 November 2026

- Use the same grey as the knob labels for the MIDI socket in both states. Leave the vector geometry, button styling, MIDI Learn implementation and DSP unchanged.
- Update the reusable AGENTS.md MIDI Learn instructions.

## 1.43.7 — Crisp MIDI Learn icon — 26 November 2026

- Replace the embedded raster MIDI socket with a directly rendered vector outline and five larger pin dots. Preserve the 26 px button, white icon, position and existing on/off styling. No image cache or persistent artwork object.
- No changes to MIDI mappings, DSP or controls. Windows visual verification outstanding.

## 1.43.6 — MIDI Learn and mouse gesture refinement — 26 November 2026

- Left-button-only saved-preset double-click reset; middle-click Init reset retained.
- Add host-delivered MIDI CC learning, exact supplied MIDI socket PNG, highlighted eligible controls and bottom-centred CC badges. Global settings file, independent of presets; message-thread parameter updates, no file operations in the audio callback.
- Apply the gold active style to Preset Browser, SEQ and Start/End Link, leaving their off appearance unchanged.
- C++17 mock tests passed for the extracted processor mapping methods: capture, learning, stepped values, reassignment, reserved CCs, persistence, and clearing. Source comparison confirmed unchanged DSP and parameter layout. Windows JUCE build, real controller routing, visual appearance and host shutdown require verification.

## 1.43.5 - Vocoder overload and idle-state correction - 26 November 2026

- Add input headroom conditioning above unity and stereo-linked wet gain protection with an 80 ms recovery, retaining the SIMD bank. Wet peaks are limited to 0.90 before the note gate and master output. Normal levels below the protection threshold are not reduced.
- Clear filter and carrier state once when the wet path sleeps; discard released MIDI voices during idle and after the note gate closes. Preserve held MIDI notes and dry BLEND behaviour.
- Standalone tests reproduced over-full-scale output in 1.43.4. Corrected engine passed four-rate, mono/stereo, all-carrier, extreme control, rapid note and silence tests; targeted 48 kHz ASan/UBSan passed. In-host burst and release hiss still require verification.

## 1.43.4 — Restored vocoder controls — 26 November 2026

- Restored SHIFT, CONTOUR, COMPAND, MOD IN, CAR IN, HARMONIC and SUB from the earlier expanded vocoder, retaining the SIMD filter bank and shared spectral analysis of 1.43.3. New controls default to neutral.
- Restored the two-row, sixteen-knob layout while retaining the existing CARRIER/BANDS panel and all other module layouts. DETUNE and SUB are inactive for non-pitched carriers.
- Appended seven sequencer targets without changing existing target indices. Parameter reset gestures use existing handling.
- Standalone neutral-output comparisons, parameter response and MIDI note-release checks passed at 44.1, 48, 96 and 192 kHz. Actual host performance and Windows build require verification.

## 1.43.3 — Shared spectral analysis and vectorised vocoder — 26 November 2026

- Share forward FFT and magnitude analysis between the lead, corrected reference and harmony voices only when the prepared windowed input is byte-identical. Each voice retains independent pitch/formant synthesis, phase history and overlap-add. Feedback uses independent analysis. Cache storage is per instance and preallocated, with no audio-thread allocation or shared static state.
- Vectorise the vocoder's modulator and carrier filters, envelope followers and carrier equalisation in groups of four bands using baseline Windows x64 SSE. Preserve band summation order, all six band counts, note gating, parameters and latency.
- Standalone sample-exact comparisons with 1.43.2: 52 original module/rate scenarios, 36 additional scenarios including single notes, four-note input, silence and changing band counts/carriers/harmony activation, and 24 recording scenarios at 44.1, 48, 96 and 192 kHz. Eight targeted mono/stereo ASan/UBSan scenarios passed; a longer sanitizer run exceeded the time limit and was not completed.
- Median of three alternating standalone 48 kHz runs with all effects active, four audible harmony voices and 48 vocoder bands: 32.1% lower elapsed time on a single-note workload, 32.7% on a four-note workload; 99th-percentile 128-sample block times 24.4% and 22.7% lower respectively. Isolated vocoder workload 67.3% lower. These are synthetic Linux C++ benchmarks, not PHI CPU measurements. Actual Windows build, host CPU and listening verification outstanding.

## 1.43.2 — Real FFT and GUI efficiency — 26 November 2026

- Replace the full complex FFT in the common spectral engine with a packed real FFT using N/2 complex butterflies in both directions. Preserve N-point spectral resolution, hop, window, phase locking, formant shaping and latency; no new audio-thread allocations. Numerical round-off can change peak/tracking decisions.
- Repaint the sequencer graph and target list only when their displayed parameter values or active step change (existing interaction, resizing and colour repaint paths retained). Read target assignments once per highlight pass and avoid polling animation for non-visible main controls. No layout, parameter or sequencer DSP changes.
- Standalone stereo comparison at 44.1, 48, 96 and 192 kHz: all 52 half-second synthetic scenarios finite; 52 shorter ASan/UBSan scenarios passed. Two seconds of the supplied recording at all four rates, six configurations per rate, yielded RMS differences between -123 and -84.9 dB relative to baseline output. One synthetic all-active 96 kHz scenario differed by -39.9 dB relative to output RMS because the serial nonlinear/pitch-tracking stages magnify small spectral differences; not a sample-exact replacement. Three-run median 48 kHz standalone benchmark (four active harmony voices) showed 26.5% PITCH, 26.1% HARMONY and 22.9% all-active Chords elapsed-time reduction. GUI CPU and Windows VST3/PHI operation not measured.

## 1.43.1 — DSP efficiency and darker theme — 26 November 2026

- Darken the common theme colour to RGB 96, 96, 192 (#6060C0), retaining theme blend, bar opacity and sequencer colours.
- Phase-locked spectral processing (TUNE, Chords PITCH and HARMONY): determine instantaneous frequencies only for spectral peaks, reuse magnitude calculations during transient analysis, retain previous complex bins, and rotate each peak region with one common complex rotation instead of converting each bin to polar coordinates. Same FFT size, overlap, formant envelope and phase-lock algorithm; small floating-point differences are expected.
- Vectorise independent YIN squared differences using baseline x64 SSE instructions, retaining ordered scalar accumulation. Simplify serial engine dispatch without skipping fade-outs; eliminate redundant circular-buffer divisions in HARMONY and FREEZE, share stereo grain interpolation indices, and precompute constant MORPH harmonic weights.
- Standalone four-rate stereo module-chain regression (44.1, 48, 96 and 192 kHz): all finite; spectral output differences approximately -138 dB or lower relative to signal RMS for isolated TUNE/PITCH/HARMONY in the tested material; stacked effects amplify numerical differences. Shorter ASan/UBSan suite completed all 52 scenarios. No parameter or latency changes. Three-run median 48 kHz Linux C++ benchmark showed approximately 9% lower elapsed time for one all-active Chords scenario, not a Windows/PHI CPU measurement. Actual VST3 compilation, DAW audio, automation and host shutdown remain to be verified.

## 1.43.0 — Version scheme, Init reset, theme and reusable instructions — 26 November 2026

- Adopt v1.43.0 as the version baseline; future builds increment the version.
- Middle-click main and sequencer rotary/slider controls and target depth knobs to reset the individual parameter to its actual Init default. Double-click restores the saved state; THEME resets to 50% with either gesture.
- Add GUI Zoom section heading. Change the theme surround, colour bar and menu borders to RGB 128, 128, 255; preserve sequencer accent colours and theme opacity curves.
- Rework AGENTS.md into a reusable plugin development template without plugin-specific layout or DSP instructions. Preserve mandatory host shutdown tests. No DSP or parameter schema changes. Windows build, mouse gesture, menu appearance and host shutdown verification outstanding.

## 0.1.43 — Background image lifetime and host shutdown — 26 November 2026

- Replace the function-local static JUCE background image with a single editor-owned image decoded during construction. The sequencer borrows the same image; its lifetime ends before the editor releases the image. Preserve artwork mapping, THEME opacity, missing-image fallback and build-time embedding. No ImageCache or process-lifetime graphics resource.
- Add permanent resource-lifetime and clean host-exit regression requirements to AGENTS.md. Static ownership and source-diff checks only; Windows build and PHI/SAVIHost process-exit verification outstanding.

## 0.1.42 — Background artwork and theme bar — 25 November 2026

- Optionally embed Resources/transform_background.jpg at build time; one 910 × 470 mapping clipped to the two surrounds, with independent rounded edges and the existing 8 px gap. Dark control panels remain opaque. Missing or invalid artwork falls back to charcoal.
- THEME: artwork at 0%, charcoal at 50%, original blue at 100%; colour bar opacity rises linearly to 100% at 50% and stays there. New default and double-click reset are 50%; saved GUI settings are retained. No audio or parameter changes. Windows build and in-host visual verification outstanding.

## 0.1.41 — Layout corrections — 25 November 2026

- Align DE-ESS MONITOR radio LEDs and labels with MODE. Resize TUNE SCALE to 144 × 20 px, preserving its aspect ratio and matching sequencer dropdown height; place its label to the left while retaining dropdown centring. No DSP or parameter changes. Windows build and GUI verification outstanding.

## 0.1.40 — Module layout and tooltip refinements — 25 November 2026

- TUNE: replace KEY dropdown with stepped note-name knob using the same parameter; centre SCALE under tuner, split controls into two panels. Centre radio groups in PITCH, FREQ, HARMONY, VOCODER, MORPH and DE-ESS; move FREQ SYNC into its selector panel. Separate FREEZE direction choices and centre knob row. Centre both gain-reduction bars, spell out Gain Reduction, and space labels equally. Preserve GATE and INT captions while expanding their help prefixes to NOISE GATE and INTERVAL.
- UI-only: processor, DSP, sequencer and parameter definitions unchanged. Windows compilation and DAW layout verification outstanding.

## 0.1.39 — CHANNEL refinement — 25 November 2026

- Add compressor threshold, ratio, attack and release, post-EQ soft saturation and a compressor gain-reduction meter. Five new saved/automatable sequencer targets 100–104. Two 406 px panels with a 6 px gap, six full-size knobs per panel; remove redundant CHANNEL heading. COMP retains its amount and automatic makeup behaviour; the ratio scales makeup. No added reported latency.
- Standalone C++17 and integrated module-chain tests at 44.1, 48, 96 and 192 kHz: all five parameter responses, neutral and 1:1 exact passthrough, saturation-only path, 64 extreme combinations per rate, bypass and GR routing. AddressSanitizer/UBSan passed. Static parameter and geometry checks passed. Windows compilation, host automation, GUI appearance, subjective listening and CPU usage remain unverified.

## 0.1.38 — DE-ESS refinement — 25 November 2026

- Add adjustable detection bandwidth (25–200%, default 100%) and adaptive body-relative threshold (0–100%, default 0%); both saved, automatable and available as sequencer targets 98–99. Centre-normalised variable-Q filter preserves the previous detector at neutral settings.
- Replace MODE and MONITOR dropdowns with amber radio LEDs in a dedicated left panel; seven full-size knobs in a centred row, remove the redundant module heading and add an internal gain-reduction meter with peak decay.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz: 760,200 exact baseline comparisons, bandwidth and adaptation response, attenuation limits, bypass, complementary monitor, extreme combinations and module-chain meter routing. AddressSanitizer/UBSan passed. Windows compilation, host automation, GUI appearance and subjective listening remain unverified.

## 0.1.37 — MORPH wet-only routing correction — 25 November 2026

- Separate dry/wet blend from pitch confidence. When pitch detection is unavailable, fade the resynthesis to silence instead of passing dry audio at full BLEND. At partial BLEND the dry proportion is independent of pitch detection; the existing 20 ms enable/bypass fade remains.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz passed for all three modes, 0/50/100% blend, unpitched input, voiced resynthesis, note loss, bypass and standalone module-chain routing. Windows build and in-host verification remain outstanding.

## 0.1.36 — FREQ stereo, PITCH Granular and MORPH refinement — 25 November 2026

- Restore Granular as the original waveform-aligned pitch algorithm; add distinct Textures with longer grains, four-way overlap and smoother windows. Six radio choices.
- Add Stereo sideband (left lower, right upper; mono lower), retain existing three sidebands. FREQ and MORPH use the same two-panel geometry and nine-knob grid as PITCH; green SYNC centred above DELAY.
- MORPH: Additive, Reed and Metallic modes; density, spectral smear, timbre modulation rate and depth. Four new sequencer targets 94–97.
- Standalone tests at 44.1, 48, 96 and 192 kHz: stereo sideband separation, distinct Granular/Textures and MORPH mode outputs, all four MORPH control responses. Existing five pitch modes and three frequency sidebands match v0.1.35 in 3,648,960 compared float samples. Windows build, DAW appearance, listening and CPU usage remain unverified.

## 0.1.35 — PITCH radio selector layout — 25 November 2026

- Replace ALGORITHM dropdown with five mutually exclusive amber radio LEDs in a dedicated left panel, matching VOCODER/HARMONY. Keep the current parameter and algorithm order, host automation and state attachment.
- Centre the nine full-size PITCH knobs in the right panel using VOCODER geometry; BLEND remains last. FREQ layout and all processor/DSP sources are unchanged.
- Static source and geometry checks passed; Windows compilation and host GUI verification remain outstanding.

## 0.1.34 — Five PITCH algorithms — 25 November 2026

- Retain the existing phase-locked spectral, pitch-synchronous vocal and waveform-aligned granular engines as Chords, Vocal and Textures respectively. Chords remains the default. Add Bass and Beats to the existing ALGORITHM selector without changing the shared PITCH/FREQ knob geometry.
- Bass uses an independent low-range configuration of the existing pitch detector (nominal 20–500 Hz, 50 ms comparison window), long period-adaptive grains and a wider bounded waveform-alignment search. Default TUNE/HARMONY/MORPH detector behaviour is unchanged.
- Beats uses short grains and stereo-linked transient detection at the latency-aligned input. Retain a brief, smoothed original attack overlay; TRANSIENT controls its intensity. FORMANT/PRESERVE remain Chords-only; TRANSIENT is enabled in Chords and Beats.
- No additional reported latency, audio-thread allocations or migration code. Selected modes only; feedback uses the same selected algorithm. Standalone Lead tests at 44.1, 48, 96 and 192 kHz verified Bass octave transposition at 30.87, 41.2 and 82.4 Hz, Beats/Textures output differences, TRANSIENT response and finite output at high feedback. All 1,596,420 compared float samples (stereo, three original algorithms, four rates) match v0.1.33 exactly. ASan/UBSan mode-switch/feedback smoke tests passed. Windows compilation, in-host listening and CPU measurements remain unverified.

## 0.1.33 — Simplified VOCODER — 25 November 2026

- Restore the v0.1.31 vocoder DSP and original nine effect knobs. Remove SHIFT, CONTOUR, COMPAND, MOD IN, CAR IN, HARMONIC and SUB from the current parameter layout and sequencer targets. No legacy migration code.
- Retain v0.1.32 CARRIER radio LEDs, stepped BANDS rotary and separate left panel, including BANDS sequencing. Centre nine full-size effect knobs in the right panel, with BLEND last.
- Static source, parameter, geometry and ZIP checks passed. Windows compilation and in-host verification remain outstanding.

## 0.1.32 — VOCODER carrier and filter-bank controls — 25 November 2026

- Replaced CARRIER dropdown with amber radio LEDs and BANDS with a stepped rotary in a separate panel. Two compact rows contain sixteen rotary controls, with BLEND last.
- Added SHIFT, CONTOUR, COMPAND, MOD IN, CAR IN, HARMONIC and SUB. Added all eight new rotary targets (including BANDS) to the sequencer.
- Existing carrier note gating remains in place.

## 0.1.31 — VOCODER speech clarity and note gating — 25 November 2026

- Gate complete Saw/Pulse wet output, including NOISE and UNVOICED, with a latency-aligned MIDI note gate (5 ms attack, 35 ms release). Maintain output while any note is held; clear carrier filter state after gate closure. Noise carrier remains independent of MIDI; silent external carriers also suppress noise and consonant leakage.
- Add bounded adaptive carrier-band equalisation to Saw/Pulse and strengthen consonant preservation. New defaults: ATTACK 5 ms, RELEASE 45 ms, UNVOICED 65%; NOISE remains 0%. No new parameters or additional reported latency.
- Integrated standalone tests at 44.1, 48, 96 and 192 kHz passed all six band counts, parameter impact, neutral/bypass paths, finite extremes and exact no-note silence with NOISE and UNVOICED both at maximum. Targeted note-gate ASan/UBSan passed; speech renders were finite. Windows build, perceptual speech quality and CPU benchmarking remain unverified.

## 0.1.30 — VOCODER refinement — 25 November 2026

- Expand VOCODER to six band counts (8–48) and add BANDWIDTH (50–200%), DETUNE (0–100 ct, internal carriers) and UNVOICED (0–100%, high-frequency consonant preservation). All three parameters are saved, host-automatable and sequencer targets 90–92. Filter centre trigonometry is cached; bandwidth changes preserve filter state and have approximate overlap gain compensation.
- Use the shared centred nine-knob grid with BLEND last; move CARRIER and BANDS to upper left, remove the redundant VOCODER heading and disable DETUNE for Noise/Sidechain. Reuse existing tooltip and dropdown behaviour.
- Integrated standalone tests at 44.1, 48, 96 and 192 kHz passed for every band count, new controls, carrier modes, mono/stereo, MIDI release, exact neutral and missing-sidechain paths, and changing extreme parameters. ASan/UBSan passed. Speech test rendered Saw, Pulse and Noise without nonfinite values. Windows build, in-host audio/MIDI routing, listening and CPU measurements remain unverified.

## 0.1.29 — FREEZE radio selectors and HARMONY label — 25 November 2026

- Replace FREEZE MODE and DIRECTION dropdowns with independent mutually exclusive amber radio groups using HARMONY styling. Both groups share a separate left selector panel with a 6 px gap to the eight-knob panel. Remove the redundant FREEZE heading; preserve knob size and placement.
- Shorten all four HARMONY INTERVAL control captions to INT, including their information-strip help prefixes. Host parameter IDs, ranges, sequencer target names and all DSP remain unchanged.
- Static geometry, choice binding and source-diff checks passed; Windows build and in-host GUI verification remain outstanding.

## 0.1.28 — FREEZE usability and HARMONY readability — 25 November 2026

- Enlarge all HARMONY voice labels (9.5 to 11 px) and readouts (10 to 11.5 px); increase label and text-box heights without changing the rotary drawing area, voice/mode layout or LEDs.
- Replace FREEZE Live recording-only operation with a continuous rolling granular effect. Hold and MIDI wait for usable audio and trim initial/trailing near-silence instead of capturing startup silence. Reserve source-position room for phase alignment on short captures, keep rolling read positions valid on buffer wrap, and allow high DRIFT to decorrelate grains.
- Retain the MIDI capture between note releases; at full BLEND only held/releasing voices sound. Use a square-root velocity response and voice-count gain normalisation to improve level. Return to Live to acquire a new capture. No additional reported latency or migration code.
- Standalone tests passed at 44.1, 48, 96 and 192 kHz for Live octave shifting, Hold after silence, MIDI capture after silence and delayed input, note release/retrigger, chords and extreme settings including rolling-buffer wrap. ASan/UBSan passed. Integrated-chain tests on the supplied recording verified output changes for each grain control and direction in all three modes, and exact zero-BLEND Live passthrough. Windows build, GUI, physical MIDI routing, subjective listening and CPU benchmarking remain unverified.

## 0.1.27 — FREEZE refinement — 25 November 2026

- Change the four HARMONY voice power LEDs to the green HOST SYNC colour; retain amber mode selection. Reduce FIXED/SCALE/MIDI radio label font from 10.5 to 9 px, retaining the MODE heading.
- Add FREEZE density (2–8 overlapping grains), smoothing, source-position drift and adjustable 100–8000 ms capture. All eight rotary controls remain visible at full size, with BLEND last.
- Replace monophonic MIDI playback with eight velocity-sensitive voices sharing a preallocated recording buffer; add four sequencer targets. No additional reported latency or preset migration.

## 0.1.26 — HARMONY panels and FREQ layout — 25 November 2026

- Removed FREQ heading and centred SYNC above DELAY.
- Replaced HARMONY mode dropdown with mutually exclusive amber radio controls.
- Added four numbered voice power LEDs, saved and automatable, muting the corresponding voice through the existing fade path without changing its LEVEL parameter.
- Separated HARMONY into five panels with 6 px gaps and increased vertical space between knob rows.

## 0.1.25 — HARMONY refinement and FREQ sync — 25 November 2026

- Replace FREQ SYNC dropdown with a green LED using the sequencer HOST SYNC colour. DELAY displays either milliseconds or eight tempo divisions and retains separate settings; both are sequencer targets. Use identical centred PITCH/FREQ nine-knob grids.
- Add HARMONY Fixed, Scale and MIDI modes; independent per-voice FORMANT, HUMAN and DELAY controls. All 24 knobs visible in two compact rows per voice. Add 12 voice targets and one delay-division target. Shared input pitch detection runs only when required, including when TUNE is off. No extra reported latency; per-voice delay is intentional. Current state schema updated without migration.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz passed for scale/MIDI targets, eight sync divisions, harmony pitch, pan, delay, formant, humanisation and muting. ASan/UBSan passed for these tests. Windows compilation, DAW GUI, MIDI/tempo routing, listening and measured CPU remain unverified.

## 0.1.24 — FREQ refinement and selector layout — 25 November 2026

- Place PITCH and FREQ operating selectors at the upper left, stack FREEZE, VOCODER and DE-ESS dropdowns at the left, and retain the existing left-side TUNE selectors. Keep full-size knobs and BLEND last among rotary controls. Preserve header, sequencer, tooltip styling and wheel handling. Update AGENTS.md.
- Add host-automatable FREQ SYNC (Free, straight/dotted/triplet note divisions), MOD RATE, MOD DEPTH, PHASE and FB LP; append four continuous sequencer targets. Tempo sync is independent of sequencer host sync. Preallocate six seconds of feedback history per channel; invalidate on disable instead of clearing the full buffer on the audio thread. Feedback remains capped at 95%. Report a feedback-dependent tail estimate of at least 15 seconds. No additional fixed latency or migration code.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz cover sideband frequencies, modulation, stereo phase, filtering, feedback delay, 95% feedback transitions, mono and neutral chain. ASan/UBSan passed at all four sample rates. Static parameter/selector/geometry checks and ZIP integrity passed. Windows JUCE build, DAW appearance, host tempo integration and CPU measurements remain unverified.

## 0.1.23 — BLEND knob layout correction — 25 November 2026

- Restore the v0.1.21 FREQ, FREEZE, MORPH and VOCODER control positions. BLEND remains the last rotary knob, not necessarily the last control in the panel. PITCH retains its v0.1.22 layout and algorithms. Clarify AGENTS.md. No DSP or parameter changes.
- Static layout and source comparison passed; Windows build and DAW appearance remain unverified.

## 0.1.22 — PITCH algorithms and blend layout — 25 November 2026

- Add host-automatable ALGORITHM selection: Spectral (phase-locked), Vocal (detected-period-aligned time-domain grains with unvoiced passthrough and granular fallback), and Granular (waveform-aligned overlapping resampled grains). Selected modes also drive the manual feedback shifter. Fixed reported latency remains unchanged; no processing-time allocations added. Precomputed low-pass resampling filters reduce upward-resampling aliasing. FORMANT, PRESERVE and TRANSIENT remain Spectral-only; these controls are disabled in Vocal and Granular. GLIDE, DETUNE, BLEND and FEEDBACK remain available in every mode. TUNE-only and HARMONY use spectral processing.
- Move BLEND to the far right in PITCH, FREQ, FREEZE, MORPH and VOCODER. Retain full-size knobs and use the shared dropdown styling, wheel handling and tooltip resolution. Update AGENTS.md. No compatibility/migration code added.
- Standalone C++17 integrated tests passed at 44.1, 48, 96 and 192 kHz, including 36 steady-tone shift measurements, neutral fixed-delay passthrough, TUNE correction at zero/full PITCH blend, extreme combined shifts and 95% feedback with mode changes. ASan/UBSan completed successfully before a signature-only cleanup; final ordinary integrated tests passed. Processed the supplied 451,608-frame stereo recording at +7 st in all three modes; output differences measured. Windows JUCE build, DAW layout, listening and CPU usage remain unverified. Vocal naturalness, formants and mode-transition artifacts need listening evaluation.

## 0.1.21 — PITCH feedback range — 25 November 2026

- Raise PITCH FEEDBACK to 95% in both host parameter range and DSP clamp; retain the bounded feedback loop. No changes to BLEND or global MIX routing. Clarify that both must be 100% for shifted-only audio with PITCH alone.
- Omit the zoom reference GIF from this and future packages.
- Standalone C++17 integrated-chain tests passed at 44.1, 48, 96 and 192 kHz: 95% feedback with -24, 0, +12 and +24 st on a tone followed by silence; shifted-only frequency checks at +12 st with full PITCH BLEND. Windows build and DAW listening remain unverified.

## 0.1.20 — PITCH blend and feedback — 25 November 2026

- Add PITCH BLEND (0–100%, default 100%) and FEEDBACK (0–90%, default 0%); both are host-automatable and continuous sequencer targets. Nine full-size knobs fit on the existing panel.
- Use a latency-aligned correction-only reference when blending the transformed lead, and a separately gated, bounded spectral feedback loop that reapplies the manual interval only. No additional reported latency. Partial blend with active TUNE can require a second FFT; feedback can require a third. Unused FFTs sleep after fades. HARMONY remains independently controlled and follows configured lead intervals.
- Remove redundant neutral-expression TUNE correction recalculation; use the current expression path for all settings. No development-version migration code added.
- Standalone full-chain tests and ASan/UBSan passed at 44.1, 48, 96 and 192 kHz, including 0/50/100% blend, TUNE retention, cascaded frequencies, mono, disabled PITCH and parameter transitions at extreme settings. Processed the supplied 451,608-frame stereo recording at +12 st: 50% BLEND and 65% FEEDBACK each measurably change the output. Actual Windows JUCE build, DAW listening, GUI and CPU remain unverified. Dedicated vocal/granular algorithms remain separate development work.

## 0.1.19 — PITCH cleanup and glide — 25 November 2026

- Remove Classic bin-remapping, the single-purpose algorithm selector and earlier development preset migration code. TUNE, PITCH and HARMONY use phase-locked spectral processing. Current state schema 3.
- GLIDE now smooths manual pitch changes in musical pitch units, including the return to zero. At the selected duration the transition is approximately 99% complete. TRANSIENT remains spectral-flux-triggered phase reseeding; it is not a separate audible effect at neutral pitch.
- AGENTS.md specifies no legacy migration or redundant DSP solely for unreleased development compatibility.
- Tested the supplied 48 kHz stereo recording (451,608 frames) with phase-locked pitch at +12 st, TRANSIENT endpoints, neutral passthrough and manual GLIDE transitions. Standalone full-chain finite-output tests at 44.1, 48, 96 and 192 kHz and targeted ASan/UBSan tests passed. Windows/DAW build and perceptual improvement remain unverified.

## 0.1.18 — PITCH refinement, part 1 — 25 November 2026

- Add PRESERVE (0–100%), GLIDE (0–500 ms), DETUNE (0–100 ct), TRANSIENT (0–100%) and ALGORITHM (Classic / Phase-locked). All default to the previous processing. Seven full-size knobs and one dropdown fit on the existing PITCH panel. Four new continuous targets are appended at 63–66; ALGORITHM is a host-automatable choice. Saved parameter values and earlier target IDs remain unchanged. Normalised host automation of target selectors may need adjustment.
- Apply approximate envelope compensation against combined lead transposition, optional manual glide, symmetric stereo detune and onset-dependent spectral phase reseeding. Optional peak-region translation reuses the existing harmony renderer in the lead engine without modifying the harmony settings or DSP path. No additional FFT stage, reported latency or realtime allocation. New controls are scoped to PITCH; Classic remains the default. Large Classic shifts can attenuate substantially. Phase-locked is an alternative, not a dedicated vocal or granular algorithm; mode changes may cause a phase-transition artifact. Independent PITCH BLEND, FEEDBACK and dedicated vocal/granular algorithms remain for later work.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz: 4,561,200 exact previous-engine sample comparisons, 9,122,400 finite-output checks across frequency and transition tests, 16 control-impact checks (1,520,400 finite samples, also passed ASan/UBSan), and 1,013,600 integrated-chain comparisons with PITCH disabled and TUNE active. Tested phase-locked stereo shifts retain substantially more consistent levels on the steady tones; real vocals, complex material, actual CPU use, Windows JUCE compilation and DAW GUI remain unverified.

## 0.1.17 — TUNE refinement — 25 November 2026

- Add TOLERANCE (0–100 ct), VIBRATO (0–100%) and REFERENCE (400–480 Hz, default 440 Hz), with neutral defaults and appended sequencer choices 60–62. Add Custom and MIDI at the end of the existing SCALE list. Twelve independently saved and automatable absolute pitch-class buttons; an empty custom scale disables correction. MIDI targets the nearest held note within two octaves, with channel-specific note tracking and note-off/all-notes-off handling. No held eligible note means no correction. MIDI is shared with existing MIDI modules.
- VIBRATO blends instantaneous pitch error with an approximately 180 ms smoothed error, reset on target changes. TOLERANCE applies a continuous deadband. The tuner displays the actual selected DSP target and active reference. All five rotary controls retain their original dimensions; the tuner display is smaller to make room for them and the custom note row. Shared dropdowns, units and help handling retained. No in-panel help captions.
- Extend only the shared spectral correction limit to ±24 st for MIDI; existing nine scales retain their previous quantisation arithmetic at neutral settings. Existing PITCH, HARMONY and remaining module algorithms are otherwise unchanged. No additional reported latency. At extreme upward shifts, the existing spectral engine may substantially attenuate the signal. Vibrato preservation is approximate, not a dedicated vocal model.
- Standalone C++17 tests: 324,000 exact original quantisation comparisons; 20,480 custom-mask checks and six reference checks; MIDI and expression tests; 3,420,900 exact previous-engine audio comparisons and 7,982,100 finite samples across four sample rates. Targeted ASan/UBSan integrated chain smoke test passed 768,000 finite samples; the longer sanitizer run did not complete within the tool time limit. Windows JUCE build, DAW MIDI routing, GUI rendering, listening and actual CPU usage remain unverified. Expanded SCALE and sequencer target lists can affect existing normalized host automation; saved parameter values and prior target IDs remain unchanged.

## 0.1.16 — Value units and stronger compression — 25 November 2026

- Use the existing parameter metadata for every main-panel dial readout after creating its slider attachment. Display st, ct, Hz, ms, dB and %, with interval-appropriate precision and numeric entry support. Widen only FREQ SHIFT/OFFSET slider bounds and value boxes to fit signed fractional Hz values; knob centres remain unchanged. Sequencer formatting is retained.
- Strengthen CHANNEL COMP: fixed -42 dB threshold, 8:1 full-strength ratio, 3 ms gain attack, 100 ms release, and up to 4 dB automatic makeup. Existing COMP values above zero intentionally sound different; zero COMP, other CHANNEL controls, parameter IDs and ranges are preserved.
- Standalone C++17 ASan/UBSan tests at 44.1, 48, 96 and 192 kHz passed 4,561,200 exact zero-COMP/bypass comparisons and 26,698,224 finite-output checks. Full compression reduced a -24 dBFS peak steady tone by approximately 11.2 dB versus no reduction in v0.1.15. Windows JUCE build, GUI suffix rendering, actual listening and CPU measurements remain unverified.

## 0.1.15 — Parameter ranges and editor persistence — 25 November 2026

- Retain the selected module per processor instance across editor recreation, with optional host-state restoration independent of sound presets.
- Expand PITCH and FORMANT to ±24 st, including the shared lead/harmony DSP limits. FINE displays cents. Expand FREEZE SIZE to 500 ms with 2.1-second prepared capacity, retaining the original snapshot length where sufficient.
- Expand FREQ FEEDBACK to 95% with the existing bounded recirculation; report a 15-second host tail. Expand CHANNEL LOW, MID, HIGH and PRESENCE to ±24 dB, with neutral detents.
- Fit the entire drag marker and caption inside the available label width and widen the MORPH HARMONICS and DE-ESS THRESHOLD labels.
- Existing parameter IDs, preset values and sequencer target order retained. Normalised host automation and sequencer modulation depths for expanded ranges may change.
- Standalone endpoint checks and ASan/UBSan passed at 44.1, 48, 96 and 192 kHz. Original-range chain hashes matched v0.1.14 across 3,040,800 stereo samples (including freeze capture). Windows compilation, PHI editor lifecycle and GUI rendering remain unverified.

## 0.1.14 — Channel strip and dropdown scrolling — 25 November 2026

- Added CHANNEL with seven neutral-by-default saved and automatable parameters: GATE, COMP, LOW CUT, LOW, MID, HIGH and PRESENCE. Appended sequencer choices 53–59. Stereo-linked dynamics, complementary tonal bands and a short enable/disable fade; no new latency or audio-thread allocations.
- Corrected C4244 in TransformVocoder.h by using float zero for both delay-buffer fills. No change to vocoder arithmetic.
- Shared stepped dropdown implementation for all twelve choices; discrete wheel events move exactly one item, guard against duplicate internal-label callback, ignore inertial scrolling and accumulate smooth gestures. Retained existing styles and tooltip ownership. Waveform commands retain wheel position after resetting their display. User sequence refresh retains a matching selection.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz: identical output hash over 1,520,400 previous-chain samples; 760,200 exact neutral samples; 28 control-impact tests; 7,418,552 finite samples; ASan/UBSan passed. Mock wheel handler tests passed. Windows JUCE build, real mouse-wheel behaviour, DAW listening and measured CPU remain unverified. Normalised sequencer target-selector automation may require adjustment.

## 0.1.13 — De-esser — 25 November 2026

- Added stereo-linked frequency-selective de-essing, split-band/broadband modes and removed-signal monitoring. Five continuous controls append sequencer targets 48–52; RANGE defaults to zero for neutral compatibility.
- Module tabs and their LEDs share identical function descriptions; removed persistent instructional/status text from module panels. Retained parameter labels, main help strip and tuner measurements. Updated AGENTS.md.
- Standalone C++17 tests passed 1,216,320 exact neutral comparisons against v0.1.12 with PITCH active, 4,713,240 finite sample checks, four sample rates (44.1, 48, 96 and 192 kHz), split-band and broadband attenuation, removed monitoring, threshold, stereo equality, settled bypass and AddressSanitizer/UndefinedBehaviorSanitizer.
- Windows JUCE compilation, DAW hover behaviour, listening and actual CPU usage remain unverified.

## 0.1.12 — Multiband Vocoder — 24 November 2026

- Added VOCODER with 12/24 bands, eight-voice MIDI saw/pulse synthesis, noise and optional mono/stereo external sidechain carrier, plus ATTACK, RELEASE, FORMANT, WIDTH, NOISE and BLEND. Six continuous controls append sequencer choices 42–47; both dropdowns reuse the shared tooltip and wheel-enabled styling. BLEND defaults to zero for existing schema-2 presets.
- Dedicated logarithmic filter bank with separate modulator/carrier filters and stereo envelopes, smoothed formant displacement, wet width, noise and blend. Both carrier paths align to the existing spectral delay, without additional reported latency or audio-thread allocation. Processing sleeps at zero blend or after disable fade. Optional sidechain uses a separate bus view and main-output-only writes; MIDI events are shared with FREEZE and the sequencer without consumption.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz passed 2,090,542 exact comparisons, 14,732,176 finite output checks, 60 feature checks and 380,100 mono equality checks. AddressSanitizer and UndefinedBehaviorSanitizer passed. Existing DSP engine headers are unchanged.
- Windows JUCE compilation, DAW sidechain/MIDI routing, live tooltip behaviour, listening and actual CPU measurements remain unverified. Sidechain needs host routing; carrier/band changes can reset state. No sustain pedal, pitch bend or MPE yet. Normalised sequencer target-selector automation may need adjustment after appended choices.

## 0.1.11 — Harmonic Morph — 24 November 2026

- Added MORPH pitch-following harmonic resynthesis after FREEZE and before VOCODER, with HARMONICS, TIMBRE, SUB, RESPONSE and BLEND. All five saved, host-automatable controls append sequencer choices 37–41. BLEND defaults to zero, including earlier schema-2 presets.
- Reuses the YIN detector implementation on the post-FREEZE signal, generating a phase-continuous fundamental, harmonics 2–6 and optional sub-octave. Separate stereo amplitude envelopes, smoothed harmonic weights, bounded wet fade and Nyquist guard. No audio-thread allocations, additional FFT or reported latency. Zero-BLEND/disabled processing sleeps after the 20 ms wet fade.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz passed 3,040,792 exact comparisons (previous chain, zero blend, settled disable, silence and tested noise), 32 frequency/harmonic balance checks including upstream PITCH transposition, 20 control-impact checks and 13,303,500 finite sample checks. AddressSanitizer and UndefinedBehaviorSanitizer passed.
- Existing engine headers, corrected tooltip resolver and GUI layouts are unchanged. Monophonic tracking is approximate and adds response time, not latency. Stereo resynthesis shares oscillator phase; chords and complex audio are not reliable tracking inputs. Windows build, DAW integration, listening quality and actual CPU measurements remain unverified. Existing normalized target-selector automation may need adjustment.

## 0.1.10 — Granular Freeze — 24 November 2026

- Added FREEZE with Live (rolling history), Hold (sustained capture) and MIDI (held-note capture with last-note priority), PITCH ±24 st, SIZE 20–250 ms, POSITION 0–100%, BLEND 0–100% and Forward/Reverse DIRECTION. All six parameters are saved and automatable; the four rotary controls append sequencer choices 33–36. Earlier schema-2 presets retain neutral defaults. The audio snapshot is transient, not preset data.
- Added an independent, preallocated two-second stereo capture engine after HARMONY and before FREQ. Two overlapping grains use complementary smoothstep windows, interpolated reads and a local waveform alignment search. No extra FFT or reported latency; sample-position MIDI input is shared without consuming the existing sequencer events. Module release fades over 15 ms, then disabled processing stops. No audio-thread allocation. Fully muted playback suspends grain reads while retaining the snapshot.
- Reused the existing tooltip owner resolver, identical heading/control descriptions and wheel-enabled dropdown styling for both choices. Existing tooltip logic and all prior DSP engines remain unchanged.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz passed 20,905,500 exact sample comparisons (live/disabled paths and previous-chain comparisons), 88 expected-frequency presence checks, 11,757,800 finite sample checks, mono/stereo capture, forward/reverse, MIDI priority/release and position/blend tests. AddressSanitizer/UndefinedBehaviorSanitizer passed. These tests are not a Windows JUCE build or a DAW integration test.
- Fixed grain overlap may colour complex material, and upward interpolation is not bandlimited. The host tail estimate remains eight seconds even though Hold can sustain while the host processes audio. DAW MIDI routing, GUI hover behaviour, listening quality, Windows compilation and actual CPU usage remain unverified. Existing normalized host automation of sequencer target selectors may require adjustment after adding choices.

## 0.1.9 — Tooltip Ownership Fix — 24 November 2026

- Centralised hover help resolution for the main interface and sequencer. The owning ComboBox or Slider now takes precedence over internal child labels, preventing plugin-name or selected-item prefixes. Label and control share the same parameter name and description, including SIDEBAND, KEY, SCALE and DIRECTION. Unnamed standalone help no longer receives an invented title.
- Added an explicit cross-project tooltip ownership and verification rule to AGENTS.md. No DSP, parameter, preset, sequence, control layout or Resources changes. Windows build and in-DAW hover verification remain required.

## 0.1.8 — Frequency Shifting — 24 November 2026

- Added FREQ SHIFT (±2000 Hz), OFFSET (±1000 Hz stereo separation), FEEDBACK (0–80%), DELAY (1–100 ms), BLEND (0–100%) and SIDEBAND (Upper, Lower, Ring). All six are saved and host-automatable; the five continuous controls append sequencer choices 28–32. The dropdown matches the existing wheel-enabled choices. Earlier schema-2 presets remain neutral.
- Added an independent quadrature allpass single-sideband engine and ring modulation, with a bounded fractional-delay feedback loop, cached oscillator rotation, parameter smoothing and short bypass fades. No extra fixed latency, FFT, allocation during processing or dependency. Disabled/neutral FREQ stops processing after the fade. The host tail estimate is now eight seconds for feedback.
- Standalone C++17 tests at 44.1, 48, 96 and 192 kHz passed 3,329,568 exact comparisons (previous-chain and settled bypass), 192 frequency/feedback amplitude measurements, and 21,057,540 finite sample checks. ASan/UBSan passed.
- Existing TUNE/PITCH/HARMONY DSP, layout, master, and sequencer controls are unchanged. Existing normalized host automation of target selectors may require adjustment. Near-DC/Nyquist sideband performance, listening, Windows compilation, DAW integration and actual CPU usage remain unverified.

## 0.1.7 — Harmony Voices — 24 November 2026

- Added four independently adjustable interval voices with simultaneous INTERVAL, PAN and LEVEL controls, fixed relative intervals of ±24 semitones and neutral default levels.
- Added parallel spectral harmony processing with peak-region phase locking, aligned to the existing lead latency and following TUNE/PITCH offsets. Only audible or fading voices process audio; gain and stereo balance are smoothed. The established lead DSP is unchanged.
- Appended twelve host-automatable parameters and sequencer target choices 16–27; the existing 32 steps and 16 target slots remain intact. Older schema-2 states restore muted harmony defaults.
- Standalone C++17 tests passed 4,865,280 exact-value comparisons against v0.1.6 with harmony inactive, 40 expected-frequency presence checks with mono/stereo and combined lead offsets, unison/level/pan and fade-out checks, and 3,040,800 finite output comparisons at 44.1/48/96/192 kHz. ASan/UBSan passed.
- Windows VST3 build, actual DAW automation, GUI appearance, listening quality and CPU measurements remain unverified. Scale-aware/MIDI harmony, independent voice formants and humanisation are later stages.

## 0.1.6 — Formant Shifting — 24 November 2026

- Matched TUNE KEY and SCALE dropdown colours, centred text, compact font and mouse-wheel behaviour to the sequencer choices.
- Added PITCH FORMANT (±12 semitones, default 0), a saved and host-automatable parameter with sequencer target 15 appended. Existing target indices and state schema are retained; earlier presets default to zero. Normalized automation of target selectors may need adjustment.
- Independent relative spectral-envelope warping reuses the shared FFT, with preallocated power smoothing, bounded gain and frame-rate interpolation. No added latency, FFT or dependency; neutral FORMANT skips the envelope work and retains the previous engine.
- Standalone tests passed 6,081,600 exact comparisons against v0.1.5 at neutral FORMANT, 12 synthetic vowel envelope measurements, 24 steady-tone pitch tests, 7,602,000 finite output checks and ASan/UBSan.
- Formant processing is approximate; automatic formant preservation and low-latency vocal processing remain future work. Windows build, DAW listening and CPU measurement are unverified.

## 0.1.5 — Scale-based Pitch Correction — 24 November 2026

- Added TUNE KEY (12 roots), SCALE (9 scales), AMOUNT (0–100%, default 0%) and SPEED (0–200 ms, default 40 ms). The original input tuner and nearest permitted target note remain visible alongside these controls.
- TUNE and PITCH share one spectral engine and the existing fixed reported latency. Independent frame-rate correction smoothing is combined with the manual transposition ratio. Unvoiced input releases correction and disabling TUNE stops detection; FFT processing idles when neither stage requires it.
- Four new saved, host-automatable parameters; AMOUNT and SPEED append sequencer targets 13 and 14. Old preset defaults remain neutral, and all earlier target indices are retained. Normalized host automation of target selectors may require adjustment.
- Monophonic correction uses the existing input detector, with causal per-sample updates only when correction is active. Tuner-only analysis retains block processing. No new dependencies or audio-thread allocations.
- Standalone scale selection, previous-engine exact-output comparisons and detector-to-chain steady-tone tests passed. The shared spectral algorithm is an initial implementation; real vocals, transitions, formants, CPU and Windows/DAW integration require further validation. No additional fixed latency is introduced.

## 0.1.4 — Spectral Pitch Shifting — 24 November 2026

- Changed only the module power LEDs to aqua (#34d6c6), with 9 px diameter instead of 12 px; retained the 18 px hit areas and selected/unselected tab styling.
- Added PITCH (-12 to +12 semitones) and FINE (-100 to +100 cents), saved and host-automatable, with sequencer targets 11 and 12 appended.
- Added a self-contained streaming FFT phase vocoder with preallocated stereo/mono state, overlap-add and control transitions. The neutral and disabled paths preserve samples after the fixed, reported latency. FFT processing stops once disabled or neutral and its fade finishes.
- Added aligned dry, wet and host-bypass delay paths; frame latency is fixed at preparation (2048 samples at 44.1/48 kHz). Live monitoring therefore incurs latency even with PITCH disabled.
- Preserved schema-2 state and old target indices; missing pitch parameters default to zero. Existing normalized host automation of sequencer target selectors may need adjustment.
- TUNE detector, sequencer visuals, original header, MIX/OUT and Resources unchanged. Independent formants, correction and natural vocal/transient processing remain future work.
- Standalone frequency, exact delayed-neutral, finite transition and ASan/UBSan tests passed. Windows VST3 compilation, DAW integration, listening and real CPU use are unverified.

## 0.1.3 — Chromatic Tuner — 24 November 2026

- Restored grey fill, border and text on unselected tabs; retained the selected gold state and independently clickable amber power LEDs.
- Added a bounded, decimated YIN-style monophonic pitch detector with fractional-period interpolation, silence rejection and louder-channel stereo analysis.
- Added the TUNE note, frequency and cents display, fixed A4 = 440 Hz, with idle/bypass status. Analysis runs only while TUNE and master Power are enabled.
- Input analysis does not modify audio, add latency or create audio-thread allocations. No correction DSP or new saved parameters; preset schema, sequencer and master controls unchanged.
- Standalone detector tests passed; full Windows build and DAW testing remain required.

## 0.1.2 — Module Tab Styling — 24 November 2026

- Combined all nine tabs into one horizontal panel, retaining six-pixel panel gaps.
- Widened the buttons and embedded each independently clickable amber power LED within its tab. Selected tabs use a dark gold fill and gold outline; unselected tabs use muted gold.
- Renamed the local zoom constrainer variable to avoid MSVC warning C4458.
- No DSP, parameter IDs, preset schema, sequencer or other interface changes.

## 0.1.1 — Module Interface — 24 November 2026

- Added nine independent module tabs and amber enable LEDs, with saved/automatable power parameters and a contextual editor.
- Removed placeholder filter and distortion parameters, controls, DSP and build references.
- Added a provisional serial module chain with intentionally transparent module stages. Preserved master mix, gain, bypass, gate and sequencer processing; removed the obsolete DIST target only.
- Preserved header controls other than the obsolete DIST selector, retained the randomise button (disabled pending effect controls), existing MIX/OUT panel, sequencer appearance, zoom and data folder. Main panels use six-pixel gaps.
- Increased state schema to 2 because the temporary v0.1.0 parameter layout is no longer valid. Existing user sequence files remain compatible.
- Module audio algorithms, sidechain, actual Windows build and DAW validation are pending.

## 0.1.0 — Independent Project Foundation — 24 November 2026

- Forked the successfully built Dusk v0.1.2 reusable template without changing audio algorithms or interface geometry.
- Assigned product name Transform, manufacturer code `sl23`, plugin code `trf1`, bundle ID `com.sl23.transform`, and general Fx category.
- Renamed processor/editor classes, sequencer namespace and source files; updated build paths and target names.
- Started independent `TransformParameters` schema 1, `TransformSequence` XML, `.transformpreset` and `.transformseq` extensions, and drag prefix.
- Runtime data folder resolves to `Transform_Data`; build creates only the portable binary. Resources excluded.
- Pitch and voice modules are not implemented in this identity-only checkpoint. Windows compilation and host validation remain required.
