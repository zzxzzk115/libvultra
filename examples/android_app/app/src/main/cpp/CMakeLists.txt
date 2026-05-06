cmake_minimum_required(VERSION 3.22.1)

project(vultra_android)

find_package(game-activity REQUIRED CONFIG)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBVULTRA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../")

add_library(vultra_android SHARED
    ${LIBVULTRA_ROOT}/source/vultra/src/platform/android/android_demo_app_launcher.cpp
    ${LIBVULTRA_ROOT}/source/vultra/src/platform/android/android_main.cpp
)

target_include_directories(vultra_android PRIVATE
    ${ANDROID_NDK}/sources/android/game-activity/include
    ${LIBVULTRA_ROOT}/source/vultra/include
)

target_link_libraries(vultra_android
    game-activity::game-activity
    android
    dl
    log
    vulkan
)

target_link_options(vultra_android PRIVATE
    "LINKER:--whole-archive"
    "$<TARGET_FILE:game-activity::game-activity>"
    "LINKER:--no-whole-archive"
)
