cmake_minimum_required(VERSION 3.22)

project(Voice2VocalSynth
    VERSION 0.1.0
    DESCRIPTION "Live voice-to-UTAU vocal synthesis experiments"
    LANGUAGES CXX)

option(VOICE2VOCALSYNTH_BUILD_TESTS "Build Voice2VocalSynth tests" ON)
option(VOICE2VOCALSYNTH_BUILD_JUCE_APP "Build JUCE standalone shell (fetches JUCE)" ON)

add_library(Voice2VocalSynthCore
    src/AppSettings.cpp
    src/DebugTimeline.cpp
    src/LatencyBudget.cpp
    src/PhonemeFallbackMapper.cpp
    src/PitchHistory.cpp
    src/PitchTarget.cpp
    src/RenderPlanner.cpp
    src/VoicebankAliasIndex.cpp
    src/VoicebankMappingPlanner.cpp
    src/VoicebankScanner.cpp)

target_include_directories(Voice2VocalSynthCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(Voice2VocalSynthCore PUBLIC cxx_std_20)

if(VOICE2VOCALSYNTH_BUILD_JUCE_APP)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(apps/juce-shell)
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
endif()
