# PrimeGram: on-device voice transcription (whisper.cpp 1.8.4, CPU only),
# the same sources that PrimeGram for Android ships. MIT licensed, see LICENSE.
cmake_minimum_required(VERSION 3.16)
project(external_whisper C CXX)

set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_LLAMAFILE OFF CACHE BOOL "" FORCE)
set(GGML_CPU ON CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_AVX2 ON CACHE BOOL "" FORCE)
set(GGML_FMA ON CACHE BOOL "" FORCE)
set(GGML_F16C ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(ggml ${CMAKE_CURRENT_BINARY_DIR}/ggml EXCLUDE_FROM_ALL)

add_library(external_whisper STATIC src/whisper.cpp)
add_library(desktop-app::external_whisper ALIAS external_whisper)

target_compile_definitions(external_whisper
PRIVATE
    GGML_USE_CPU
    WHISPER_VERSION="1.8.4"
)
target_include_directories(external_whisper
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ggml/include
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/ggml/src
)
target_link_libraries(external_whisper PUBLIC ggml)
