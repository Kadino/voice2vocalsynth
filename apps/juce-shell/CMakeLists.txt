juce_add_gui_app(Voice2VocalSynthApp
    COMPANY_NAME "Voice2VocalSynth"
    PRODUCT_NAME "Voice2VocalSynth"
    BUNDLE_ID com.voice2vocalsynth.shell
    VERSION ${PROJECT_VERSION}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_WEB_SERVER FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(V2VS_GTK312 IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(V2VS_WEBKIT241 IMPORTED_TARGET webkit2gtk-4.1)
    pkg_check_modules(V2VS_CURL IMPORTED_TARGET libcurl)
endif()

juce_generate_juce_header(Voice2VocalSynthApp)

target_sources(Voice2VocalSynthApp
    PRIVATE
        Main.cpp
        MainComponent.cpp)

target_link_libraries(Voice2VocalSynthApp
    PRIVATE
        Voice2VocalSynthCore
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(Voice2VocalSynthApp
        PRIVATE
            PkgConfig::V2VS_GTK312
            PkgConfig::V2VS_WEBKIT241
            PkgConfig::V2VS_CURL)
endif()

target_compile_features(Voice2VocalSynthApp PRIVATE cxx_std_20)

target_compile_definitions(Voice2VocalSynthApp
    PRIVATE
        "VOICE2VOCALSYNTH_REPOSITORY_PHONEME_ONNX_FIXTURE=\"${_phoneme_fixture_slash}\"")
