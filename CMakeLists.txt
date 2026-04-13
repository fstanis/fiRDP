cmake_minimum_required(VERSION 3.20)
project(fiRDP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native)
    if(NOT APPLE)
        add_link_options(-fuse-ld=lld)
    endif()
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
if(NOT APPLE)
    set(WITH_VAAPI ON CACHE BOOL "" FORCE)
    set(WITH_VAAPI_H264_ENCODING ON CACHE BOOL "" FORCE)
endif()
set(EXPORT_ALL_SYMBOLS OFF CACHE BOOL "" FORCE)

# --- Apply patches to FreeRDP before building ---
file(GLOB FREERDP_PATCHES "${CMAKE_SOURCE_DIR}/patches/*.patch")
foreach(patch ${FREERDP_PATCHES})
    execute_process(
        COMMAND git apply --check ${patch}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/FreeRDP
        RESULT_VARIABLE patch_needed
        OUTPUT_QUIET ERROR_QUIET
    )
    if(patch_needed EQUAL 0)
        execute_process(
            COMMAND git apply ${patch}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/FreeRDP
        )
        message(STATUS "Applied FreeRDP patch: ${patch}")
    endif()
endforeach()

add_subdirectory(FreeRDP EXCLUDE_FROM_ALL)

# --- Find other dependencies ---
find_package(SDL3 REQUIRED)

if(APPLE)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(APPSERVICES_FRAMEWORK ApplicationServices REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
endif()

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
    src/host_keys.cpp
    src/gpu_renderer.cpp
    src/fi_dialogs.cpp
    src/fi_dialog_stubs.cpp
    ${SDL3_CLIENT_SOURCES}
)

target_include_directories(fiRDP PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FREERDP_SDL_DIR}/SDL3
    ${FREERDP_SDL_DIR}/common
    ${CMAKE_SOURCE_DIR}/FreeRDP/include
    ${CMAKE_BINARY_DIR}/FreeRDP/include
    ${CMAKE_SOURCE_DIR}/FreeRDP/winpr/include
    ${CMAKE_BINARY_DIR}/FreeRDP/winpr/include
)

target_compile_options(fiRDP PRIVATE -include cstdlib)

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT APPLE)
    set_target_properties(fiRDP PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_link_libraries(fiRDP PRIVATE
    SDL3::SDL3
    freerdp-client
    freerdp
    winpr
)

if(APPLE)
    target_link_libraries(fiRDP PRIVATE ${SECURITY_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${APPSERVICES_FRAMEWORK})
else()
    target_link_libraries(fiRDP PRIVATE PkgConfig::LIBSECRET)
endif()
