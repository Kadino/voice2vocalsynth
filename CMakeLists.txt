cmake_minimum_required(VERSION 3.22)

project(Voice2VocalSynth
    VERSION 0.1.0
    DESCRIPTION "Live voice-to-UTAU vocal synthesis experiments"
    LANGUAGES C CXX)

option(VOICE2VOCALSYNTH_BUILD_TESTS "Build Voice2VocalSynth tests" ON)
option(VOICE2VOCALSYNTH_BUILD_JUCE_APP "Build JUCE standalone shell (fetches JUCE)" ON)

set(VOICE2VOCALSYNTH_PHONEME_REPOSITORY_FIXTURE "${CMAKE_SOURCE_DIR}/tests/fixtures/onnx/dummy_identity.onnx"
    CACHE PATH "Configurable path to the repository phoneme ONNX test fixture (presets + unit tests)")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OnnxRuntime.cmake")

set(_phoneme_fixture_slash "${VOICE2VOCALSYNTH_PHONEME_REPOSITORY_FIXTURE}")
file(TO_CMAKE_PATH "${_phoneme_fixture_slash}" _phoneme_fixture_slash)
string(REPLACE "\\" "/" _phoneme_fixture_slash "${_phoneme_fixture_slash}")

add_library(Voice2VocalSynthCore
    src/AppSettings.cpp
    src/DebugTimeline.cpp
    src/LatencyBudget.cpp
    src/OfflineRenderer.cpp
    src/PcmWavReader.cpp
    src/PcmWavWriter.cpp
    src/PhonemeOnnxAsyncRunner.cpp
    src/PhonemeOnnxRunner.cpp
    src/PhonemeTemporalStabilizer.cpp
    src/PhonemeFallbackMapper.cpp
    src/PitchHistory.cpp
    src/PitchTarget.cpp
    src/RenderPlanner.cpp
    src/SimplePitchEstimator.cpp
    src/VoicebankAliasIndex.cpp
    src/VoicebankMappingPlanner.cpp
    src/VoicebankScanner.cpp)

target_include_directories(Voice2VocalSynthCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(Voice2VocalSynthCore PUBLIC cxx_std_20)

target_compile_definitions(Voice2VocalSynthCore
    PRIVATE
        "VOICE2VOCALSYNTH_REPOSITORY_PHONEME_ONNX_FIXTURE=\"${_phoneme_fixture_slash}\"")

if(VOICE2VOCALSYNTH_ONNX_AVAILABLE)
    target_compile_definitions(Voice2VocalSynthCore PUBLIC VOICE2VOCALSYNTH_WITH_ONNX=1)
    target_include_directories(Voice2VocalSynthCore
        PRIVATE
            "${VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT}/include")
    target_link_libraries(Voice2VocalSynthCore
        PRIVATE
            "${VOICE2VOCALSYNTH_ONNXRUNTIME_LIB}")
    if(UNIX AND NOT APPLE)
        list(APPEND CMAKE_BUILD_RPATH "${VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT}/lib")
    endif()
endif()

if(VOICE2VOCALSYNTH_BUILD_JUCE_APP)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(apps/juce-shell)
    if(VOICE2VOCALSYNTH_ONNX_AVAILABLE)
        voice2vocalsynth_copy_onnx_runtime_runtime_deps(Voice2VocalSynthApp)
    endif()
endif()

if(VOICE2VOCALSYNTH_BUILD_TESTS)
    enable_testing()

    add_executable(PhonemeFallbackMapperTests
        tests/PhonemeFallbackMapperTests.cpp)

    target_link_libraries(PhonemeFallbackMapperTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME PhonemeFallbackMapperTests COMMAND PhonemeFallbackMapperTests)

    add_executable(PitchTargetTests
        tests/PitchTargetTests.cpp)

    target_link_libraries(PitchTargetTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME PitchTargetTests COMMAND PitchTargetTests)

    add_executable(SimplePitchEstimatorTests
        tests/SimplePitchEstimatorTests.cpp)

    target_link_libraries(SimplePitchEstimatorTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME SimplePitchEstimatorTests COMMAND SimplePitchEstimatorTests)

    add_executable(PhonemeTemporalStabilizerTests
        tests/PhonemeTemporalStabilizerTests.cpp)

    target_link_libraries(PhonemeTemporalStabilizerTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME PhonemeTemporalStabilizerTests COMMAND PhonemeTemporalStabilizerTests)

    add_executable(PitchHistoryTests
        tests/PitchHistoryTests.cpp)

    target_link_libraries(PitchHistoryTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME PitchHistoryTests COMMAND PitchHistoryTests)

    add_executable(VoicebankAliasIndexTests
        tests/VoicebankAliasIndexTests.cpp)

    target_link_libraries(VoicebankAliasIndexTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME VoicebankAliasIndexTests COMMAND VoicebankAliasIndexTests)

    add_executable(VoicebankScannerTests
        tests/VoicebankScannerTests.cpp)

    target_link_libraries(VoicebankScannerTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME VoicebankScannerTests COMMAND VoicebankScannerTests)

    add_executable(VoicebankMappingPlannerTests
        tests/VoicebankMappingPlannerTests.cpp)

    target_link_libraries(VoicebankMappingPlannerTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME VoicebankMappingPlannerTests COMMAND VoicebankMappingPlannerTests)

    add_executable(RenderPlannerTests
        tests/RenderPlannerTests.cpp)

    target_link_libraries(RenderPlannerTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME RenderPlannerTests COMMAND RenderPlannerTests)

    add_executable(DebugTimelineTests
        tests/DebugTimelineTests.cpp)

    target_link_libraries(DebugTimelineTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME DebugTimelineTests COMMAND DebugTimelineTests)

    add_executable(LatencyBudgetTests
        tests/LatencyBudgetTests.cpp)

    target_link_libraries(LatencyBudgetTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME LatencyBudgetTests COMMAND LatencyBudgetTests)

    add_executable(AppSettingsTests
        tests/AppSettingsTests.cpp)

    target_link_libraries(AppSettingsTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME AppSettingsTests COMMAND AppSettingsTests)

    add_executable(OfflineRendererTests
        tests/OfflineRendererTests.cpp)

    target_link_libraries(OfflineRendererTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME OfflineRendererTests COMMAND OfflineRendererTests)

    add_executable(PhonemeOnnxRunnerTests
        tests/PhonemeOnnxRunnerTests.cpp)

    target_link_libraries(PhonemeOnnxRunnerTests
        PRIVATE
            Voice2VocalSynthCore)

    target_compile_definitions(PhonemeOnnxRunnerTests
        PRIVATE
            "VOICE2VOCALSYNTH_TEST_ONNX_MODEL=\"${_phoneme_fixture_slash}\"")

    add_test(NAME PhonemeOnnxRunnerTests COMMAND PhonemeOnnxRunnerTests)

    if(VOICE2VOCALSYNTH_ONNX_AVAILABLE)
        voice2vocalsynth_copy_onnx_runtime_runtime_deps(PhonemeOnnxRunnerTests)
    endif()

    add_executable(PhonemeOnnxAsyncRunnerTests
        tests/PhonemeOnnxAsyncRunnerTests.cpp)

    target_link_libraries(PhonemeOnnxAsyncRunnerTests
        PRIVATE
            Voice2VocalSynthCore)

    target_compile_definitions(PhonemeOnnxAsyncRunnerTests
        PRIVATE
            "VOICE2VOCALSYNTH_TEST_ONNX_MODEL=\"${_phoneme_fixture_slash}\"")

    add_test(NAME PhonemeOnnxAsyncRunnerTests COMMAND PhonemeOnnxAsyncRunnerTests)

    if(VOICE2VOCALSYNTH_ONNX_AVAILABLE)
        voice2vocalsynth_copy_onnx_runtime_runtime_deps(PhonemeOnnxAsyncRunnerTests)
    endif()
endif()
