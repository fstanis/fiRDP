cmake_minimum_required(VERSION 3.20)
project(fiRDP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use clang + lld for LTO compatibility
add_link_options(-fuse-ld=lld)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -flto=thin -march=native)
    add_link_options(-flto=thin)
endif()

# --- Build FreeRDP as a subdirectory (static, no clients/servers) ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WITH_CLIENT OFF CACHE BOOL "" FORCE)
set(WITH_SERVER OFF CACHE BOOL "" FORCE)
set(WITH_SAMPLE OFF CACHE BOOL "" FORCE)
set(WITH_PROXY OFF CACHE BOOL "" FORCE)
set(WITH_PCSC_WINPR OFF CACHE BOOL "" FORCE)
set(WITH_CLIENT_CHANNELS ON CACHE BOOL "" FORCE)
set(WITH_GFX_AV1 ON CACHE BOOL "" FORCE)
set(WITH_FFMPEG ON CACHE BOOL "" FORCE)
set(WITH_DSP_FFMPEG ON CACHE BOOL "" FORCE)
set(WITH_VIDEO_FFMPEG ON CACHE BOOL "" FORCE)
set(WITH_VAAPI ON CACHE BOOL "" FORCE)
set(WITH_VAAPI_H264_ENCODING ON CACHE BOOL "" FORCE)
set(EXPORT_ALL_SYMBOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(FreeRDP EXCLUDE_FROM_ALL)

# --- Find other dependencies ---
find_package(SDL3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)

# --- FreeRDP SDL3 client sources (core rendering/input — not dialogs) ---
set(FREERDP_SDL_DIR "${CMAKE_SOURCE_DIR}/FreeRDP/client/SDL")
set(SDL3_CLIENT_SOURCES
    ${FREERDP_SDL_DIR}/SDL3/sdl_context.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_utils.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_input.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_touch.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_pointer.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_disp.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_monitor.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_channels.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_window.cpp
    ${FREERDP_SDL_DIR}/SDL3/sdl_clip.cpp
    ${FREERDP_SDL_DIR}/common/sdl_common_utils.cpp
    ${FREERDP_SDL_DIR}/common/sdl_prefs.cpp
)

# --- fiRDP executable ---
add_executable(fiRDP
    src/main.cpp
    src/rdp_file.cpp
    src/password_store.cpp
    src/rdp_connection.cpp
    src/fi_dialogs.cpp
    src/fi_dialog_stubs.cpp
    ${SDL3_CLIENT_SOURCES}
)

target_include_directories(fiRDP PRIVATE
    ${CMAKE_SOURCE_DIR}/src                 # our headers
    ${FREERDP_SDL_DIR}/SDL3                 # sdl_context.hpp, sdl_utils.hpp, etc.
    ${FREERDP_SDL_DIR}/common               # sdl_prefs.hpp, sdl_common_utils.hpp
    # FreeRDP headers (source + generated)
    ${CMAKE_SOURCE_DIR}/FreeRDP/include
    ${CMAKE_BINARY_DIR}/FreeRDP/include
    ${CMAKE_SOURCE_DIR}/FreeRDP/winpr/include
    ${CMAKE_BINARY_DIR}/FreeRDP/winpr/include
)

target_link_libraries(fiRDP PRIVATE
    SDL3::SDL3
    PkgConfig::LIBSECRET
    freerdp-client
    freerdp
    winpr
)
