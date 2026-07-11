option(VOICE2VOCALSYNTH_WITH_POCKETSPHINX
    "Build the streaming PocketSphinx all-phone backend" ON)

set(VOICE2VOCALSYNTH_POCKETSPHINX_MODEL_ROOT ""
    CACHE PATH "Optional PocketSphinx en-us model root (contains en-us/ and en-us-phone.lm.bin)")

set(VOICE2VOCALSYNTH_POCKETSPHINX_AVAILABLE OFF)

if(VOICE2VOCALSYNTH_WITH_POCKETSPHINX)
    if(CMAKE_VERSION VERSION_LESS 3.25)
        message(FATAL_ERROR
            "PocketSphinx 5.1.1 requires CMake 3.25+. Upgrade CMake or configure "
            "with -DVOICE2VOCALSYNTH_WITH_POCKETSPHINX=OFF.")
    endif()
    include(FetchContent)

    # 5.1.1 is the first release containing the June 2026 model-loading
    # security fixes (CVE-2026-54559). Keep the archive hash pinned.
    FetchContent_Declare(
        pocketsphinx_dependency
        URL https://github.com/cmusphinx/pocketsphinx/archive/refs/tags/v5.1.1.tar.gz
        URL_HASH SHA256=e2db414eb66618cd0a98de77507db32517a48f6900b06bfb94c0acc4bef5761d
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(pocketsphinx_dependency)

    # PocketSphinx 5.1.1 still uses CMAKE_SOURCE_DIR/CMAKE_BINARY_DIR for these
    # paths. Correct them when it is consumed with FetchContent.
    target_include_directories(pocketsphinx
        PRIVATE
            "${pocketsphinx_dependency_BINARY_DIR}"
        PUBLIC
            "${pocketsphinx_dependency_SOURCE_DIR}/include"
            "${pocketsphinx_dependency_BINARY_DIR}/include")

    if(NOT VOICE2VOCALSYNTH_POCKETSPHINX_MODEL_ROOT)
        set(VOICE2VOCALSYNTH_POCKETSPHINX_MODEL_ROOT
            "${pocketsphinx_dependency_SOURCE_DIR}/model/en-us")
    endif()

    set(VOICE2VOCALSYNTH_POCKETSPHINX_AVAILABLE ON)
endif()
