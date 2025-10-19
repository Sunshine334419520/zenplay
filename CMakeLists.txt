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

# spdlog 编译期日志级别配置
# 编译期：决定哪些日志代码会被编译进程序
# 运行期：LogManager::Initialize() 决定哪些日志实际输出
# 
# Debug 模式：编译所有级别（包括 TRACE/DEBUG），方便调试
# Release 模式：只编译 INFO 及以上，减小程序体积和性能开销
#
# 注意：Visual Studio 是多配置生成器，CMAKE_BUILD_TYPE 在配置时为空
# 需要使用生成器表达式在编译时根据配置选择不同的宏定义
if (CMAKE_CONFIGURATION_TYPES)
    # 多配置生成器（Visual Studio, Xcode）
    # 使用生成器表达式在编译时动态选择
    add_compile_definitions(
        $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
        $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
        $<$<CONFIG:RelWithDebInfo>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
        $<$<CONFIG:MinSizeRel>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    )
    message(STATUS "spdlog compile-time level: Multi-config (Debug=TRACE, Release=INFO)")
else()
    # 单配置生成器（Ninja, Unix Makefiles）
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
        message(STATUS "spdlog compile-time level: TRACE (Debug build)")
    else()
        add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
        message(STATUS "spdlog compile-time level: INFO (Release build)")
    endif()
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
file(GLOB_RECURSE PLAYER_STATS_FILES "src/player/stats/*.cpp" "src/player/stats/*.h")
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
list(APPEND SRC_FILES ${PLAYER_STATS_FILES})
list(APPEND SRC_FILES ${VIEW_FILES})

# resource files
set(QRC_FILES resources/zenplay.qrc)

# Windows 控制台配置
# Debug 模式：显示控制台窗口（方便查看日志）
# Release 模式：不显示控制台（纯 GUI 应用）
# 
# 注意：对于 Visual Studio 多配置生成器，需要在构建时指定配置
# Debug 构建：cmake --build build --config Debug
# Release 构建：cmake --build build --config Release
if (WIN32)
    # 默认不添加 WIN32 标志，显示控制台（开发友好）
    # 如果需要 Release 无控制台，手动构建时添加 WIN32 属性
    add_executable(${PROJECT_NAME} ${SRC_FILES} ${QRC_FILES})
    
    # 设置 Release 配置为 WIN32 应用（无控制台）
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
    )
else()
    add_executable(${PROJECT_NAME} ${SRC_FILES} ${QRC_FILES})
endif()

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

# 单元测试（可选，通过 BUILD_TESTING 控制）
option(BUILD_TESTING "Build the testing tree" ON)
if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
    message(STATUS "Unit tests enabled. Run with: ctest or ./build/tests/zenplay_tests")
endif()