cmake_minimum_required(VERSION 3.16)
project(horror_voice_npc CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GGML_CUDA "Build llama.cpp / whisper.cpp with CUDA support" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Dependencies (fetched at configure time)
# ---------------------------------------------------------------------------
include(FetchContent)

# 1. miniaudio - single header audio capture library
# (Only fetch/populate the folder for headers; do not compile its optional targets)
FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        master
)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
  FetchContent_Populate(miniaudio)
endif()

# 2. llama.cpp - local LLM inference
FetchContent_Declare(
  llama_cpp
  GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git
  GIT_TAG        master
)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
if(GGML_CUDA)
  set(GGML_CUDA ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(llama_cpp)

# 3. whisper.cpp - local speech-to-text
FetchContent_Declare(
  whisper_cpp
  GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
  GIT_TAG        master
)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(whisper_cpp)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(horror_voice_npc
  src/main.cpp
  src/audio_capture.cpp
  src/persona_manager.cpp
  src/stt_engine.cpp
  src/llm_engine.cpp
  src/output_writer.cpp
)

target_include_directories(horror_voice_npc PRIVATE
  ${miniaudio_SOURCE_DIR}
  src
)

target_link_libraries(horror_voice_npc PRIVATE
  whisper
  llama
)

# miniaudio needs pthread + dl on Linux
if(UNIX AND NOT APPLE)
  target_link_libraries(horror_voice_npc PRIVATE pthread dl m)
endif()

message(STATUS "===========================================================")
message(STATUS " Horror Voice NPC — build configured")
message(STATUS " CUDA offload: ${GGML_CUDA}")
message(STATUS " Remember to download a whisper model + a GGUF LLM model")
message(STATUS " into the models/ folder before running. See README.md.")
message(STATUS "===========================================================")

# On Windows, copy DLLs next to executable
if(WIN32)
  add_custom_command(TARGET horror_voice_npc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
            "$<TARGET_FILE_DIR:horror_voice_npc>"
    COMMENT "Copying dependency DLLs next to horror_voice_npc.exe"
  )
endif()
