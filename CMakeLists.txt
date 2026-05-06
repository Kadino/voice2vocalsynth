cmake_minimum_required(VERSION 3.20)

project(Voice2VocalSynth
    VERSION 0.1.0
    DESCRIPTION "Live voice-to-UTAU vocal synthesis experiments"
    LANGUAGES CXX)

option(VOICE2VOCALSYNTH_BUILD_TESTS "Build Voice2VocalSynth tests" ON)

add_library(Voice2VocalSynthCore
    src/PhonemeFallbackMapper.cpp)

target_include_directories(Voice2VocalSynthCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(Voice2VocalSynthCore PUBLIC cxx_std_20)

if(VOICE2VOCALSYNTH_BUILD_TESTS)
    enable_testing()

    add_executable(PhonemeFallbackMapperTests
        tests/PhonemeFallbackMapperTests.cpp)

    target_link_libraries(PhonemeFallbackMapperTests
        PRIVATE
            Voice2VocalSynthCore)

    add_test(NAME PhonemeFallbackMapperTests COMMAND PhonemeFallbackMapperTests)
endif()
