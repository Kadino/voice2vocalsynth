juce_add_gui_app(Voice2VocalSynthApp
    COMPANY_NAME "Voice2VocalSynth"
    PRODUCT_NAME "Voice2VocalSynth"
    BUNDLE_ID com.voice2vocalsynth.shell
    VERSION ${PROJECT_VERSION}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_WEB_SERVER FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE)

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

target_compile_features(Voice2VocalSynthApp PRIVATE cxx_std_20)
