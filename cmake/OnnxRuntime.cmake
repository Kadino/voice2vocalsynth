# Resolves ONNX Runtime headers + library for PhonemeOnnxRunner.
#
# Controls:
#   -DVOICE2VOCALSYNTH_WITH_ONNX=OFF   Skip ONNX entirely (stub runner).
#   -DVOICE2VOCALSYNTH_ONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-x.y.z
#                                     Use a pre-extracted official distribution.
#
# Auto-download is implemented for Linux x64 only (official Microsoft tarball).

option(VOICE2VOCALSYNTH_WITH_ONNX "Build with ONNX Runtime (phoneme runner)" ON)

set(VOICE2VOCALSYNTH_ONNXRUNTIME_VERSION "1.26.0" CACHE STRING "ONNX Runtime release version for auto-download")
set(VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT "" CACHE PATH "Extracted ONNX Runtime root (include/ + lib/). Empty triggers auto-download on supported hosts.")

set(VOICE2VOCALSYNTH_ONNX_AVAILABLE FALSE)

if(VOICE2VOCALSYNTH_WITH_ONNX)
    if(NOT VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT STREQUAL "")
        if(EXISTS "${VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
            set(VOICE2VOCALSYNTH_ONNX_AVAILABLE TRUE)
        else()
            message(WARNING "VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT is set but onnxruntime_cxx_api.h was not found: ${VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(_ort_dir "onnxruntime-linux-x64-${VOICE2VOCALSYNTH_ONNXRUNTIME_VERSION}")
        set(_ort_root "${CMAKE_BINARY_DIR}/_deps/${_ort_dir}")
        set(_ort_tgz "${CMAKE_BINARY_DIR}/_deps/${_ort_dir}.tgz")
        if(NOT EXISTS "${_ort_root}/include/onnxruntime_cxx_api.h")
            set(_url "https://github.com/microsoft/onnxruntime/releases/download/v${VOICE2VOCALSYNTH_ONNXRUNTIME_VERSION}/${_ort_dir}.tgz")
            message(STATUS "Fetching ONNX Runtime ${_url}")
            file(DOWNLOAD "${_url}" "${_ort_tgz}" STATUS _dl_status LOG _dl_log)
            if(NOT _dl_status EQUAL 0)
                message(WARNING "ONNX Runtime download failed (status=${_dl_status}). Disabling ONNX. Log tail:\n${_dl_log}")
            else()
                execute_process(
                    COMMAND "${CMAKE_COMMAND}" -E tar xf "${_ort_tgz}"
                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/_deps"
                    RESULT_VARIABLE _untar)
                if(NOT _untar EQUAL 0)
                    message(WARNING "Failed to extract ONNX Runtime archive (code ${_untar}). Disabling ONNX.")
                elseif(NOT EXISTS "${_ort_root}/include/onnxruntime_cxx_api.h")
                    message(WARNING "ONNX Runtime extract did not produce expected layout under ${_ort_root}. Disabling ONNX.")
                else()
                    set(VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT "${_ort_root}" CACHE PATH "Auto-downloaded ONNX Runtime root" FORCE)
                    set(VOICE2VOCALSYNTH_ONNX_AVAILABLE TRUE)
                endif()
            endif()
        else()
            set(VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT "${_ort_root}" CACHE PATH "Auto-downloaded ONNX Runtime root" FORCE)
            set(VOICE2VOCALSYNTH_ONNX_AVAILABLE TRUE)
        endif()
    else()
        message(STATUS "ONNX Runtime auto-download is not configured for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}. "
                       "Set VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT or turn off VOICE2VOCALSYNTH_WITH_ONNX.")
    endif()
endif()

if(VOICE2VOCALSYNTH_ONNX_AVAILABLE)
    if(WIN32)
        set(VOICE2VOCALSYNTH_ONNXRUNTIME_LIB "${VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    else()
        set(VOICE2VOCALSYNTH_ONNXRUNTIME_LIB "${VOICE2VOCALSYNTH_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
    endif()
    if(NOT EXISTS "${VOICE2VOCALSYNTH_ONNXRUNTIME_LIB}")
        message(WARNING "ONNX Runtime library not found at ${VOICE2VOCALSYNTH_ONNXRUNTIME_LIB}; disabling ONNX.")
        set(VOICE2VOCALSYNTH_ONNX_AVAILABLE FALSE)
    endif()
endif()
