# Voice2VocalSynth

Voice2VocalSynth is a private Windows-focused C++ project for live
voice-to-UTAU vocal synthesis experiments.

## Current core module

The repository currently contains the first phoneme mapping slice:

- ARPABET phoneme normalization with stress digit stripping.
- English phoneme to Japanese CV alias mapping.
- Configurable consonant substitutions such as `TH`.
- Partial CVC fallback for trailing consonants, e.g. `K AE T` maps to
  `ka` plus a shortened final `to` alias.
- Renderer hints for partial finals so later audio code can preserve the
  consonant while attenuating the helper vowel tail.
- Equal-temperament pitch target calculation with raw follow, semitone snap,
  key snap, fixed/default pitch, octave shifting, snap strength, and
  low-confidence fallback handling.
- Basic `oto.ini` parsing for UTAU voicebank aliases and timing fields.
- Voicebank alias resolution that chooses the first available mapper candidate
  and reports missing candidates for UI/debug output.
- Voicebank alias style is auto-detected from the loaded alias inventory.
- Latency budget presets and end-to-end monitoring latency breakdowns for
  analysis, stabilization, render scheduling, and device latency.
- App preset settings for audio routing, voicebank selection, pitch behavior,
  latency mode, and recording/debug options.
- Editable JSON preset import/export for the settings model. The UI can use the
  same model while users can still edit preset files by hand.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## License

This repository is private-use software. No permission is granted to copy,
modify, distribute, sublicense, or use this code except with prior written
permission from the copyright holder.

See [LICENSE](LICENSE) for the full terms.
