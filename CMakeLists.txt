cmake_minimum_required(VERSION 3.23)
project(zenplay)

#set(CMAKE_TOOLCHAIN_FILE ${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake)
#set(CMAKE_TOOLCHAIN_FILE ${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake)
#message(${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)
find_package(ffmpeg REQUIRED)
find_package(spdlog REQUIRED)
find_package(SDL2 REQUIRED)
#find_package(ZLIB REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (WIN32)
    add_definitions(-DOS_WIN)
    add_definitions(-DNOMINMAX)
elseif(APPLE)
    add_definitions(-DOS_MAC)
elseif(UNIX)
    add_definitions(-DOS_LINUX)
endif()

add_subdirectory(third_party/loki)

# src files
file(GLOB SRC_FILES "src/main.cpp")
file(GLOB PLAYER_MAIN_FILES 
    "src/player/zen_player.cpp"
    "src/player/zen_player.h"
    "src/player/playback_controller.h"
    "src/player/playback_controller.cpp")
file(GLOB PLAYER_COMMON_FILES
    "src/player/common/*.cpp"
    "src/player/common/*.h"
)
file(GLOB_RECURSE PLAYER_CODEC_FILES "src/player/codec/*.cpp" "src/player/codec/*.h")
file(GLOB_RECURSE PLAYER_DEMUXER_FILES "src/player/demuxer/*.cpp" "src/player/demuxer/*.h")
file(GLOB PLAYER_AUDIO_OUTPUT_FILES "src/player/audio/*.cpp" "src/player/audio/*.h")
file(GLOB_RECURSE PLAYER_VIDEO_FILES "src/player/video/*.cpp" "src/player/video/*.h")
file(GLOB_RECURSE PLAYER_SYNC_FILES "src/player/sync/*.cpp" "src/player/sync/*.h")
file(GLOB_RECURSE PLAYER_LOADER_FILES "src/player/loader/*.cpp" "src/player/loader/*.h")
file(GLOB_RECURSE VIEW_FILES "src/view/*.cpp" "src/view/*.h" "src/view/*.ui")

if (WIN32)
    list(APPEND PLAYER_AUDIO_OUTPUT_FILES "src/player/audio/impl/wasapi_audio_output.cpp" "src/player/audio/impl/wasapi_audio_output.h")
elseif(APPLE)
    list(APPEND PLAYER_AUDIO_OUTPUT_FILES "src/player/audio/impl/core_audio_output.mm" "src/player/audio/impl/core_audio_output.h")
elseif(UNIX)
    list(APPEND PLAYER_AUDIO_OUTPUT_FILES "src/player/audio/impl/alsa_audio_output.cpp" "src/player/audio/impl/alsa_audio_output.h")
endif()


list(APPEND SRC_FILES ${PLAYER_MAIN_FILES})
list(APPEND SRC_FILES ${PLAYER_COMMON_FILES})
list(APPEND SRC_FILES ${PLAYER_CODEC_FILES})
list(APPEND SRC_FILES ${PLAYER_DEMUXER_FILES})
list(APPEND SRC_FILES ${PLAYER_AUDIO_OUTPUT_FILES})
list(APPEND SRC_FILES ${PLAYER_VIDEO_FILES})
list(APPEND SRC_FILES ${PLAYER_SYNC_FILES})
list(APPEND SRC_FILES ${PLAYER_LOADER_FILES})
list(APPEND SRC_FILES ${VIEW_FILES})

# resource files
set(QRC_FILES resources/zenplay.qrc)

add_executable(${PROJECT_NAME} ${SRC_FILES} ${QRC_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE 
    nlohmann_json::nlohmann_json
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    ffmpeg::avutil
    ffmpeg::avcodec
    ffmpeg::avformat
    ffmpeg::avfilter
    ffmpeg::swscale
    spdlog::spdlog
    SDL2::SDL2
    SDL2::SDL2main
    loki
)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if (MSVC)
    find_program(DEPLOYQT_EXECUTABLE NAMES windeployqt)
    if (DEPLOYQT_EXECUTABLE)
        # 只在 Release 模式下运行 windeployqt
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND $<$<CONFIG:Release>:${DEPLOYQT_EXECUTABLE}> $<$<CONFIG:Release>:$<TARGET_FILE:${PROJECT_NAME}>>
            COMMENT "Deploying Qt libraries (Release mode only)")
    else()
        message(WARNING "windeployqt not found, Qt libraries will not be deployed automatically.")
    endif()
endif()

if (APPLE)
    # macOS specific deployment
    find_program(DEPLOYQT_EXECUTABLE NAMES macdeployqt)
        if (DEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${DEPLOYQT_EXECUTABLE} $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Deploying Qt libraries with macdeployqt")
        else()
            message(WARNING "macdeployqt not found, Qt libraries will not be deployed automatically.")
        endif()
endif()