---
schemaVersion: 1
project:
  name: Voice2VocalSynth
  direction: private-use Windows standalone live voice-to-UTAU-style vocal synthesis
  privacy: private_use_only
  platform: windows
  language: cpp17_or_cpp20
  framework:
    juce: true
    build: cmake
targetApp:
  distribution: windows_standalone_app
  input:
    liveMicrophone: true
  outputOptions:
    - monitor_speakers_headphones
    - existing_virtual_audio_device
  virtualMicNote:
    juceCanOutputToExistingVirtualDevice: true
    examples:
      - VB-CABLE
      - VoiceMeeter
      - Virtual Audio Cable
    customVirtualMicrophoneRequiresDriverWork: true
    guidance: Treat project-owned Windows virtual microphone as a later separate subsystem.
signalPipeline:
  - id: microphoneInput
    label: Microphone input
  - id: inputGainNoiseGateVad
    label: Input gain / noise gate / VAD
  - id: ringBuffer
    label: Ring buffer
  - id: parallelAnalysis
    label: Parallel analysis
    steps:
      - id: pitchDetector
        label: pitch detector
      - id: phonemeDetector
        label: phoneme detector
      - id: whistleDetector
        label: whistle detector
      - id: onsetTransitionDetector
        label: onset / transition detector
  - id: temporalStabilizer
    label: Temporal stabilizer
  - id: englishArpabetPhonemes
    label: English ARPABET phonemes
  - id: arpabetToJapaneseMapping
    label: ARPABET → Japanese CV/CVC mapping
  - id: utauLookup
    label: UTAU oto.ini / prefix-map / suffix-map lookup
  - id: voicebankRenderer
    label: Voicebank renderer
  - id: pitchCorrection
    label: Pitch correction / snapping / octave shift
  - id: audioOutput
    label: Audio output device or virtual audio cable
latencyDesign:
  acceptabilityNote: Up to ~200 ms may be acceptable depending on user goals.
  requirement:
    exposeMeasuredLatency: true
    note: The app should expose measured/estimated latency, not only buffer size.
  modes:
    - id: low_latency
      label: Low latency
      analysisContextMs:
        min: 40
        max: 80
      tradeoffs:
        - More unstable consonants
        - Useful for live responsiveness
    - id: balanced
      label: Balanced
      analysisContextMs:
        min: 80
        max: 140
      tradeoffs:
        - Better phoneme confidence
        - Probably default
    - id: high_accuracy
      label: High accuracy
      analysisContextMs:
        min: 140
        max: 200
      tradeoffs:
        - Better plosive/fricative timing
        - Fewer phoneme jumps
  latencyUi:
    dropdownOptions:
      - Low Latency
      - Balanced
      - High Accuracy
      - Experimental / Long Lookahead
      - Custom
    breakdown:
      - inputDeviceLatency
      - juceBufferLatency
      - analysisWindowLookahead
      - phonemeStabilizationDelay
      - pitchSmoothingDelay
      - renderQueueDelay
      - outputDeviceLatency
vadSynchronization:
  purpose: >-
    Voice activity detection (VAD) marks speech vs non-speech for gating, utterance
    boundaries, and coordination with streaming phoneme ONNX inference and the renderer.
    VAD edges must align with what the listener perceives at the output, not only with
    instantaneous features on the incoming waveform.
  analysisVersusPlaybackTimelines:
    summary: Treat capture/analysis time and DAC playback time as separable but linked clocks.
    analysisTimeline: >-
      VAD and phoneme frames are computed on the ring-buffer / capture clock, including
      analysis-window lookahead and temporal-stabilizer delay before labels are considered
      committed.
    playbackTimeline: >-
      Synthesized audio is heard only after end-to-end latency: device and JUCE buffer
      delay, analysis lookahead, stabilization, optional pitch smoothing, ONNX inference
      scheduling and queueing, render planning, and output path delay (see latencyDesign
      breakdown fields).
  boundaryRepresentation:
    summary: Emit timestamped speech boundaries instead of only unprompted edge events.
    recommendations:
      - Attach monotonic timestamps (seconds since stream start or a global sample index) to speech_onset and speech_end hypotheses.
      - Map analysis-time boundaries to renderer actions using measured or estimated end-to-end latency so phrase release matches perceived speech offset.
  latencyAlignment:
    baseline: Apply a fixed mapping from analysis time to render time using the exposed latency budget components where delays are stable.
    inferenceJitter: >-
      ONNX inference and async scheduling can add variable delay; maintain a bounded moving
      estimate of model-plus-queue lag and clamp updates so utterance boundaries do not
      audibly hunt.
  rendererInteraction:
    sustainAndRelease: >-
      For held material (including looped sustain regions in the voicebank renderer), use
      render-time-aligned utterance end to exit sustain and play the trailing tail once,
      rather than cutting at the raw VAD edge in analysis time.
  vadArchitectureNote: >-
    VAD may share front-end features with the phoneme model for simpler alignment, or run as
    a lighter parallel detector; the latter may disagree with phoneme segmentation and
    requires explicit fusion rules.
phonemeDetection:
  designPreference: onnx_based_streaming_phoneme_model
  phonemeSet: arpabet
  outputStructSpec:
    name: PhonemeFrame
    fields:
      - name: arpabet
        type: string
      - name: confidence
        type: float
      - name: estimatedOnsetSeconds
        type: double
      - name: estimatedEndSeconds
        type: double
      - name: isVoiced
        type: bool
      - name: isConsonant
        type: bool
      - name: isVowel
        type: bool
  constraints:
    avoidSpeechToText: true
    note: For nonsense syllables, classify sound units directly; do not infer words.
  temporalStabilizer:
    rawModelOutputMustNotTriggerSamplesDirectly: true
    responsibilities:
      - confidenceThresholds
      - minimumPhonemeDuration
      - onsetCorrection
      - hysteresis
      - plosiveBurstDetection
      - fricativeHoldDetection
      - vowelSustainDetection
      - fallbackSubstitutionLogic
answer_unvoicedConsonantsAndPitch:
  summary: Unvoiced consonants often lack stable F0; do not derive musical pitch from them.
  recommendedBehavior:
    - pitchDetectorRunsIndependently: true
    - unvoicedSegmentHoldsPreviousReliablePitch: true
    - rendererAvoidStrongPitchShiftOnNoisyConsonants: true
    - followingVowelResumesPitchTrackingOrSnapping: true
whistleDetection:
  shouldBeSeparateDetector: true
  characteristics:
    - strongNarrowbandSinusoidalPitch
    - littleOrNoVoicedHarmonicStructure
    - oftenHigherF0
    - noPhonemeContent
  handling:
    detectWhistleMode: true
    separatePitchPath: true
    defaultOutputAliasOrVowel: u
    userSelectableAlias: true
    optionallyBypassConsonantRendering: true
utauVoicebankSupport:
  v1:
    - japanese_cv_voicebanks_first
    - standard_oto_ini_parsing
    - alias_fallback_substitution
    - prefix_suffix_map_support_if_practical
  later:
    - optional_cvc_cvvcc_cvvc
  prefixSuffixWhy:
    summary: Multipitch/expression banks use prefix/suffix maps to select the correct sample variant per note range.
    exampleRanges:
      - range: C3-G3
        selection: low_voice_samples
      - range: Gs3-C5
        selection: normal_samples
      - range: Cs5_and_up
        selection: high_samples
    riskWithoutSupport: Bank may load but choose wrong sample range; tone worsens and pitch-shifting artifacts increase.
phonemeToJapaneseMapping:
  mustLiveInConfigFile: true
  note: Do not hardcode mapping in code; load from config.
  approximateMappingNote: English phonemes do not map perfectly to Japanese CV.
  fallbackStrategyExamples:
    - english: "K + AA"
      japanese: ka
    - english: "K + AE"
      japanese: ka
      alternatives:
        - kya
    - english: S
      japanese: s_then_nearest_vowel_cv
    - english: TH
      japanese: s_or_z_or_t_fallback_depending_on_voicing
    - english: R_or_L
      japanese: r_flap_fallback
  exampleConfig:
    phonemeSet: arpabet
    vowelFallbacks:
      AA: a
      AE: a
      AH: a
      IY: i
      UW: u
      EH: e
      OW: o
    consonantFallbacks:
      K: k
      G: g
      S: s
      SH: sh
      T: t
      D: d
      TH: s
      DH: z
      R: r
      L: r
pitchBehavior:
  pipeline:
    - rawF0
    - confidenceCheck
    - recentMeanFallbackWhenLowConfidence
    - optionalSmoothing
    - snapMode
    - manualKeyScaleFilter
    - octaveShift
    - rendererTargetPitch
  modes:
    - raw_pitch_follow
    - nearest_note_snap
    - key_scale_snap
    - fixed_default_pitch
    - whistle_pitch_mode
  controls:
    - pitchCorrectionAmount
    - hardVsSmoothCorrection
    - glideTime
    - pitchConfidenceThreshold
    - recentMeanWindow
    - octaveShiftAfterSnapping
    - manualKey
    - scaleSelectionMajorMinorPentatonic
  temperament:
    requirementNote: >-
      "Western temperament but not equal temperament" is ambiguous; support tuning tables to keep options open.
    implementationPreference: tuningTablesSupported
rendererPlan:
  v1Renderer:
    - loadWavSamplesFromUtauBank
    - parseOtoIni
    - preserveConsonantDuration
    - sustainOrStretchVowelRegion
    - crossfadesAtOverlapPoints
    - monophonicOnly
    - applyTargetPitch
    - simpleEnvelopesAndPitchCurvesInternally
  higherQualityRenderer:
    candidate: WORLD_vocoder
    worldDecomposition:
      - f0
      - spectralEnvelope
      - aperiodicityNoiseComponent
    benefit: More natural vowel pitch shifting vs naive resampling.
    licenseNote: Modified BSD / BSD 3-clause style (permissive).
  rubberBandNote:
    concern: Rubber Band typical OSS license is GPL v2-or-later (commercial licensing available).
    guidance: Prefer WORLD for private/proprietary cleanliness unless GPL implications are acceptable.
presets:
  categories:
    - id: audio_routing
      fields:
        - inputDevice
        - outputDevice
        - sampleRate
        - bufferSize
        - virtualCableTarget
    - id: voicebank
      fields:
        - voicebankPath
        - otoIniPath
        - prefixSuffixMap
        - mappingConfig
        - fallbackRules
    - id: detection
      fields:
        - latencyModeLowBalancedHigh
        - phonemeConfidenceThreshold
        - pitchConfidenceThreshold
        - whistleDetectionEnabled
        - noiseGateThreshold
    - id: pitch
      fields:
        - pitchMode
        - key
        - scale
        - temperamentOrTuningTable
        - octaveShift
        - glide
        - snapStrength
        - defaultPitch
    - id: debug
      fields:
        - recordInput
        - recordPhonemeTimeline
        - recordRenderedOutput
        - showPhonemeTimeline
calibrationSetupFlow:
  - selectMicrophone
  - measureInputNoiseFloor
  - setInputGain
  - detectComfortableSpeakingAndSingingRange
  - recordVowelSamples
  - recordPlosiveFricativeChecks
  - recordWhistleSampleIfEnabled
  - validatePitchDetector
  - validatePhonemeDetectorConfidence
  - scanLoadedVoicebankForMissingAliases
  - generateFallbackReport
decisions:
  projectName: Voice2VocalSynth
  momoneMomoChibi2009Bundling:
    officialTermsFound: true
    redistributionRequiresPermission: true
    decision:
      doNotBundleByDefault: true
      requireUserLocalCopy: true
      optionalSetupFlowLocateVoicebankFolder: true
    bundlingLaterRequires: explicitPermissionFromRightsHolder
  virtualMicrophone:
    finalTarget: project_owned_windows_virtual_microphone
    prototypeTarget: monitor_output_only
    phasingNote: Treat as later subsystem; likely requires driver/virtual endpoint work beyond normal JUCE app.
  lowPitchConfidenceBehavior:
    whenHighConfidence: useDetectedPitch
    whenLowConfidence:
      - unvoicedConsonantsHoldRecentReliablePitch
      - whisperedInputUseVoicebankDefaultRecordingPitch
      - configurable: true
  whistleBehavior:
    defaultWhistleOutputAlias: u
    userSelectableInSettings: true
    separateFromPhonemeDetection: true
  uiRequirements:
    showDetectedAndMappedPhonemes: true
    autosave:
      - lastVoicebank
      - selectedPreset
      - audioSettings
      - dataFolderPath
    rendererPriority: intelligibility_over_smooth_continuity
  pitchSnappingCorrection:
    v1Temperament: twelve_tone_equal_temperament
    controls:
      - rawFollow
      - semitoneSnap
      - keySnap
      - fixedDefaultPitch
      - octaveShiftAfterSnapping
      - hardSnapVsSmoothSlide
      - glideAmount
      - pitchConfidenceFallback
  voicebankSupportDecision:
    v1CoreType: cv
    v1CvcFallback: partial_cvc_optional_configurable
  virtualMicPackagingNote: revisit_installer_driver_packaging_during_implementation
dataStorage:
  mustNotStoreInRepo: true
  defaultLocations:
    recordings: "%LOCALAPPDATA%\\Voice2VocalSynth\\Recordings"
    trainingData: "%LOCALAPPDATA%\\Voice2VocalSynth\\TrainingData"
  userSelectablePrivateDataFolder: true
  recordingDebugFormat:
    formats:
      - wav
      - json
      - csv
    exampleLayout:
      folderPattern: "session_YYYY-MM-DD_HHMM"
      files:
        - input.wav
        - output.wav
        - timeline.json
        - pitch.csv
        - phonemes.csv
        - aliases.csv
  trainingRecording:
    optInAutoCaptureSupported: true
  repositoryTestFixtures:
    summary: Small permissive-licensed WAV, ONNX, or similar files may live in-repo for tests.
    provenanceRequired: true
    provenanceMinimumFields:
      - origin_or_author
      - license_identifier
      - source_url_or_checksum_reference
      - modifications_from_upstream_if_any
livePhonemeVerification:
  handoffDoc: docs/live-phoneme-verification-plan.md
  localTestingPlan:
    summary: >-
      Incremental manual verification of the Linux live phoneme path in a local
      development environment, including dependency bootstrap. Complements CTest
      unit coverage; not a CI job. Narrative commands and pass criteria also
      appear in docs/live-phoneme-verification-plan.md#local-environment-testing-plan.
    principles:
      - useTwoBuildDirectoriesCoreVersusJuce
      - keepDataOutsideRepoUnderLivePhonemeVerifyRoot
      - startWithOneUtteranceThenSubsetThenFullCorpus
      - recordEveryRunUnderTimestampedRunsDirectory
    buildDirectories:
      core:
        path: build
        cmakeArgs: "-DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF"
        contains:
          - Voice2VocalSynthCore
          - CLI tools
          - CTest suite
      juce:
        path: build-juce
        cmakeArgs: "-DVOICE2VOCALSYNTH_BUILD_JUCE_APP=ON"
        contains:
          - Voice2VocalSynthApp
        note: Required for live verification orchestration scripts.
    dataRoot:
      default: "~/.local/share/Voice2VocalSynth/LivePhonemeVerify"
      envVar: LIVE_PHONEME_VERIFY_ROOT
    environmentVariables:
      - name: LIVE_PHONEME_VERIFY_ROOT
        default: "~/.local/share/Voice2VocalSynth/LivePhonemeVerify"
        purpose: Verification data root
      - name: VOICE2VOCALSYNTH_BUILD_DIR
        default: "./build"
        purpose: Must be build-juce for live orchestration scripts
      - name: VOICE2VOCALSYNTH_APP_BIN
        default: auto-detect under build dir
        purpose: Override JUCE shell binary path
      - name: LIBRISPEECH_TEST_CLEAN_ROOT
        default: "${LIVE_PHONEME_VERIFY_ROOT}/datasets/LibriSpeech/test-clean"
        purpose: Dataset override
      - name: LINUX_AUDIO_SINK_NAME
        default: LivePhonemeVerify
        purpose: PipeWire/Pulse null sink name
      - name: MFA_ACOUSTIC_MODEL
        default: english_us_arpa
        purpose: MFA acoustic model id
      - name: MFA_DICTIONARY_MODEL
        default: english_us_arpa
        purpose: MFA dictionary id
      - name: LIVE_VERIFY_MAX_E2E_MS
        default: "1000"
        purpose: End-to-end latency gate override
      - name: LIVE_VERIFY_MIN_F1
        default: "0.20"
        purpose: Temporal quality gate override
    dependencyBootstrap:
      systemPackagesUbuntu:
        required:
          - cmake
          - g++
          - libstdc++-13-dev
          - pkg-config
          - libgtk-3-dev
          - libwebkit2gtk-4.1-dev
          - libcurl4-openssl-dev
          - libasound2-dev
          - libx11-dev
          - libxrandr-dev
          - libxinerama-dev
          - libxcursor-dev
          - libfreetype-dev
          - libgl1-mesa-dev
          - ffmpeg
          - curl
          - python3
          - pulseaudio-utils
          - ripgrep
        optionalAlternates:
          alsaLoopback: "sudo modprobe snd-aloop"
          jack: jackd2 or PipeWire pw-jack
      pipewireNullSink:
        oncePerBoot: true
        command: >-
          pactl load-module module-null-sink sink_name=LivePhonemeVerify
          sink_properties=device.description=LivePhonemeVerify
        verify:
          - pactl list sinks short | grep LivePhonemeVerify
          - pactl list sources short | grep LivePhonemeVerify.monitor
      montrealForcedAligner:
        recommended: conda create -n mfa -c conda-forge montreal-forced-aligner
        models:
          - mfa model download acoustic english_us_arpa
          - mfa model download dictionary english_us_arpa
        verifyScript: scripts/generate_librispeech_mfa_labels.sh --check-mfa
      buildCommands:
        core: |
          CXX=g++ cmake -S . -B build -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=OFF
          cmake --build build -j"$(nproc)"
          ctest --test-dir build --output-on-failure
        juce: |
          CXX=g++ cmake -S . -B build-juce -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=ON
          cmake --build build-juce --target Voice2VocalSynthApp -j"$(nproc)"
      smokeChecks:
        - tool: cmake
          minimum: "3.22"
          note: "3.25+ when PocketSphinx enabled"
        - tool: g++
          requirement: C++20 capable
        - tool: ffmpeg
        - tool: ffprobe
        - tool: curl
        - tool: python3
        - tool: rg
          note: Required by run_live_phoneme_verify_linux.sh but undeclared in README
        - tool: pactl
        - tool: parec
        - tool: mfa
          note: After conda activate
        - tool: Voice2VocalSynthApp
          path: build-juce/apps/juce-shell/Voice2VocalSynthApp_artefacts/Voice2VocalSynth
        - tool: ctest
          expectation: 47 tests registered
    coverageGaps:
      notCoveredByCTest:
        - realTimeFfmpegPlaybackAt1x
        - liveVirtualAudioHostProbe
        - mfaAlignmentExecution
        - juceLiveRuntimeWiring
          note: Phases 5–7 startup/log/scoring covered by Voice2VocalSynthLiveLogFixture and LiveLogFixtureHarnessTests; real JUCE audio callback path remains manual
        - streamingLiveRenderer
        - bashOrchestrationScripts
          note: Dry-run paths covered by ScriptDryRunTests and LiveVerifyOrchestrationDryRunTests; full live orchestration remains manual
      partiallyCovered:
        - libriSpeechDatasetValidation
          note: Negative validation and setup CLI paths covered by LibriSpeechDatasetTests and ScriptDryRunTests
        - mfaTextGridConversion
          note: Self-compare perfect F1 covered by MfaLabelPipelineTests; live MFA alignment remains manual
        - linuxVirtualAudioFixtureParsing
          note: Missing-sink and invalid-report parsing covered by LinuxVirtualAudioTests
        - postCaptureScoring
          note: CLI exit 0/3 paths covered by LivePhonemeVerificationTests; live capture remains manual
        - phonemeTemporalStabilizerInIsolation
          note: Hysteresis, voicing, and reset edge cases covered by PhonemeTemporalStabilizerTests
    phases:
      - id: phase0_bootstrap
        label: One-time dependency bootstrap
        gapsCovered: [undeclaredDependencies, buildDirectories]
        commands:
          - See dependencyBootstrap.systemPackagesUbuntu and buildCommands
          - export VOICE2VOCALSYNTH_BUILD_DIR="$PWD/build-juce"
          - export VOICE2VOCALSYNTH_APP_BIN="$PWD/build-juce/apps/juce-shell/Voice2VocalSynthApp_artefacts/Voice2VocalSynth"
          - export LIVE_PHONEME_VERIFY_ROOT="${HOME}/.local/share/Voice2VocalSynth/LivePhonemeVerify"
        passCriteria:
          - All dependencyBootstrap.smokeChecks succeed
          - PipeWire null sink exists when using primary route
      - id: phase1_librispeech
        label: LibriSpeech setup
        gapsCovered: [realDatasetDownload]
        commands:
          - scripts/setup_librispeech_test_clean.sh --download
          - scripts/setup_librispeech_test_clean.sh --verify
          - scripts/setup_librispeech_test_clean.sh --manifest
        passCriteria:
          - Voice2VocalSynthLibriSpeechSetup --verify exits 0
          - librispeech-test-clean-manifest.json exists under verify root datasets
          - --list-utterances returns at least one row
      - id: phase2_mfa_labels
        label: MFA reference labels
        gapsCovered: [mfaAlignmentExecution, ffmpegCorpusPrep]
        commands:
          - UTT_ID="$(build/Voice2VocalSynthLibriSpeechSetup --list-utterances --limit 1 | cut -f1)"
          - scripts/generate_librispeech_mfa_labels.sh --utterance-id "$UTT_ID"
          - scripts/generate_librispeech_mfa_labels.sh --subset 20
        passCriteria:
          - Per-utterance JSON labels under labels/librispeech-test-clean/
          - labels manifest records MFA version and stressDigitsStripped
          - Self-compare via Voice2VocalSynthPhonemeEval yields perfect F1 on one label file
      - id: phase3_virtual_audio
        label: Virtual audio routing
        gapsCovered: [livePactlDetection, audioProbe]
        commands:
          - scripts/validate_linux_virtual_audio.sh --check
          - scripts/validate_linux_virtual_audio.sh --check --probe --write-manifest
        passCriteria:
          - JSON report valid true
          - probePassed true after --probe on pipewire-loopback
          - linux-virtual-audio.json written under verify root
        alternates:
          - scripts/validate_linux_virtual_audio.sh --route alsa --check
          - scripts/validate_linux_virtual_audio.sh --route jack --check
        note: --probe only implemented for pipewire-loopback
      - id: phase4_playback
        label: Playback manifest and real-time ffmpeg
        gapsCovered: [ffprobeDurations, ffmpegReOutput, monotonicLaunchAnchors]
        commands:
          - RUN_DIR="$LIVE_PHONEME_VERIFY_ROOT/runs/manual-playback-$(date -u +%Y%m%dT%H%M%SZ)"
          - scripts/play_librispeech_clips_linux.sh --dry-run --subset 1 --utterance-id "$UTT_ID" --run-dir "$RUN_DIR"
          - scripts/play_librispeech_clips_linux.sh --subset 1 --utterance-id "$UTT_ID" --run-dir "$RUN_DIR"
        passCriteria:
          - playback-manifest.json contains routeId playbackDevice clips with flacPath
          - Live play completes without ffmpeg errors
          - Each clip has non-zero playbackStartedSteadyNs after play
          - parec on LivePhonemeVerify.monitor shows energy during playback
      - id: phase5_juce_startup
        label: JUCE live-log export without playback
        gapsCovered: [juceStartupOk, liveLogFileIo, quitFileShutdown, backendDescriptor, deviceSettings]
        commands:
          - Start Voice2VocalSynthApp with --live-log-export --capture-device from linux-virtual-audio.json
          - Use --quit-file or --quit-after-seconds to exit
          - Repeat for pocketsphinx placeholder and onnx_phoneme with real non-identity ONNX model
        passCriteria:
          - live-log.jsonl contains session_start with startup_ok true
          - backend_descriptor and device_settings lines present
          - Shell exits cleanly on quit-file
        optional:
          - --auto-loopback-measure expects latency_measure JSON when loopback works
      - id: phase6_tone_smoke
        label: Live backend and stabilizer without LibriSpeech
        gapsCovered: [audioCallbackToPhFrame, backendInferenceTimestamps]
        commands:
          - Start JUCE export with --quit-after-seconds 5
          - ffmpeg sine tone into LivePhonemeVerify pulse sink in second terminal
        passCriteria:
          - At least one ph_frame or backend_inference record during tone
          - t0 t1 steady_ns monotonic and non-zero
          - pocketsphinx backend field is pocketsphinx_allphone
      - id: phase7_scoring
        label: Post-capture scoring offline
        gapsCovered: [onnxLatencyLines, latencyMeasurePath, multiClipAlignment, onnxPhonemeCanonicalMapping]
        commands:
          - Voice2VocalSynthLivePhonemeVerify with live-log playback-manifest labels-root and gate flags
        passCriteria:
          - Exit 0 gates passed or exit 3 scored but failed gates
          - predictions.json metrics.json report.md non-empty
        variants:
          - backend onnx_phoneme maps to phoneme_onnx
          - backend placeholder must fail gates
          - multi-clip manifest and log
          - log with latency_measure for loopback E2E path
      - id: phase8_orchestration
        label: Full run_live_phoneme_verify_linux.sh
        gapsCovered: [bashOrchestration, rgStartupWait, shellPlaybackHandshake]
        commands:
          - VOICE2VOCALSYNTH_BUILD_DIR=build-juce scripts/run_live_phoneme_verify_linux.sh --dry-run --subset 1 --utterance-id "$UTT_ID"
          - VOICE2VOCALSYNTH_BUILD_DIR=build-juce scripts/run_live_phoneme_verify_linux.sh --subset 1 --utterance-id "$UTT_ID"
          - scripts/run_live_phoneme_verify_linux.sh --subset 20
        passCriteria:
          - Run dir contains live-log.jsonl playback-manifest.json predictions.json metrics.json report.md shell logs
          - ph_frame records present in live-log.jsonl
          - Exit 0 passed gates; 3 scored failed gates; other infrastructure failure
        knownIssue: >-
          Default build/ configured with JUCE OFF breaks script unless
          VOICE2VOCALSYNTH_BUILD_DIR points at build-juce or build is reconfigured ON
      - id: phase9_compare_backends
        label: Multi-backend comparison
        gapsCovered: [compareLivePhonemeBackendsScript]
        commands:
          - scripts/compare_live_phoneme_backends_linux.sh --backends placeholder,pocketsphinx --subset 5
          - With real ONNX model add onnx_phoneme to --backends
        passCriteria:
          - comparison.json ranks backends; placeholder does not pass
          - Exit 0 only when at least one backend passes all gates
      - id: phase10_live_synthesis
        label: StreamingLiveRenderer manual check
        gapsCovered: [streamingLiveRenderer]
        manual: true
        commands:
          - Configure voicebank in shell_settings.json
          - Run phase8 with pocketsphinx on voiced clip
          - Inspect live-log for render sustain and live_timeline debug output
        passCriteria:
          - Live synthesis audible or live_timeline JSON emitted
          - No missing-alias storms for simple vowel phrases
        note: Weakest automated surface; remains manual until headless voicebank fixture test exists
    executionOrder:
      - phase0_bootstrap
      - phase1_librispeech
      - phase2_mfa_labels
      - phase3_virtual_audio
      - phase4_playback
      - phase5_juce_startup
      - phase6_tone_smoke
      - phase8_orchestration
      - phase7_scoring
      - phase9_compare_backends
      - phase10_live_synthesis
    futureHardening:
      - done: Document and check ripgrep dependency in orchestration script
      - done: Add LibriSpeechPlaybackCliTests MfaLabelCliTests LinuxVirtualAudioCliTests
      - done: Add StreamingLiveRenderer unit test with temp voicebank
      - done: Extend LivePhonemeVerificationTests for latency_measure and onnx log kinds
      - done: run_live_phoneme_verify_linux.sh passes -DVOICE2VOCALSYNTH_BUILD_JUCE_APP=ON explicitly
      - done: CI harness for play_librispeech_clips_linux.sh --dry-run (ScriptDryRunTests)
      - done: Integrated VAD/sustain-release/renderer chain (VadSustainPipelineTests)
      - done: OfflineRenderer and StreamingLiveRenderer sustain-release truncation tests
      - done: CI harness for run_live_phoneme_verify_linux.sh --dry-run (LiveVerifyOrchestrationDryRunTests)
      - done: compare_live_phoneme_backends_linux.sh --dry-run covered by ScriptDryRunTests
      - done: PhonemeFrame outputStructSpec contract tests (PhonemeFrameContractTests)
      - done: Latency preset analysis-context range checks in LatencyBudgetTests
      - done: Extend partiallyCovered gaps (dataset validation negatives, MFA self-compare F1, virtual-audio fixture edges, scorer CLI exit codes, stabilizer hysteresis)
      - done: Headless Voice2VocalSynthLiveLogFixture for phases 5–7 (LiveLogFixtureTests LiveLogFixtureHarnessTests)
---

Voice2VocalSynth canonical project specification.

- The YAML frontmatter above is the **source of truth for agents/tools**.
- The body of this document is intentionally minimal; update the YAML when requirements change.
- Linux live-path local testing plan: `livePhonemeVerification.localTestingPlan` in frontmatter; narrative in `docs/live-phoneme-verification-plan.md#local-environment-testing-plan`.
