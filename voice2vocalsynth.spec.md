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
---

Voice2VocalSynth canonical project specification.

- The YAML frontmatter above is the **source of truth for agents/tools**.
- The body of this document is intentionally minimal; update the YAML when requirements change.
