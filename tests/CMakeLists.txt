# 单元测试 CMakeLists.txt
# 基于 execution_plan_priority_features.md 的测试实现

cmake_minimum_required(VERSION 3.23)

# 查找 GTest（已在 conanfile.py 中引入）
find_package(GTest REQUIRED)

# 收集被测试的源文件（不包括 main.cpp）
set(PLAYER_SOURCES
    # 通用工具（ThreadSafeQueue 是 header-only，无需编译）
    # 如果有其他 .cpp 文件需要添加
    
    # 错误处理系统
    ${CMAKE_SOURCE_DIR}/src/player/common/error.cpp
    ${CMAKE_SOURCE_DIR}/src/player/common/ffmpeg_error_utils.cpp
    
    # AVSyncController
    ${CMAKE_SOURCE_DIR}/src/player/sync/av_sync_controller.cpp
    
    # 日志管理（AVSyncController 依赖）
    ${CMAKE_SOURCE_DIR}/src/player/common/log_manager.cpp
    
    # 统计管理（AVSyncController 依赖）
    ${CMAKE_SOURCE_DIR}/src/player/stats/statistics_manager.cpp
    
    # PlayerStateManager（WaitForResume 测试依赖）
    ${CMAKE_SOURCE_DIR}/src/player/common/player_state_manager.cpp
    
    # 其他依赖（根据实际情况添加）
    ${CMAKE_SOURCE_DIR}/src/player/common/timer.cpp
)

# Windows 平台专用源文件
if (WIN32)
    list(APPEND PLAYER_SOURCES
        ${CMAKE_SOURCE_DIR}/src/player/common/win32_error_utils.cpp
    )
endif()

# 收集测试文件
set(TEST_SOURCES
    test_main.cpp
    test_result_error.cpp
    test_thread_safe_queue.cpp
    test_av_sync_controller.cpp
    test_blocking_queue.cpp
    test_player_state_manager_wait_resume.cpp
    test_error_utils.cpp
)

# Windows 平台专用测试文件
if (WIN32)
    list(APPEND TEST_SOURCES
        test_win32_error_utils.cpp
    )
endif()

# 创建测试可执行文件
add_executable(zenplay_tests
    ${TEST_SOURCES}
    ${PLAYER_SOURCES}
)

# 链接库
target_link_libraries(zenplay_tests PRIVATE
    GTest::gtest
    # GTest::gtest_main  # 不链接，因为我们使用自定义的 test_main.cpp
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ffmpeg::avutil  # FFmpeg 错误工具需要
    # Qt6::Core  # 如果测试涉及 Qt 组件
)

# 包含目录
target_include_directories(zenplay_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

# 编译定义（与主项目保持一致）
if (WIN32)
    target_compile_definitions(zenplay_tests PRIVATE
        OS_WIN
        NOMINMAX
    )
elseif(APPLE)
    target_compile_definitions(zenplay_tests PRIVATE OS_MAC)
elseif(UNIX)
    target_compile_definitions(zenplay_tests PRIVATE OS_LINUX)
endif()

# spdlog 编译期日志级别（测试时使用 TRACE 级别以便调试）
target_compile_definitions(zenplay_tests PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

# 启用 CTest（可选）
enable_testing()

# 注册测试（使用 GTest 发现功能）
include(GoogleTest)
gtest_discover_tests(zenplay_tests)

# 手动添加测试（替代方案）
# add_test(NAME zenplay_all_tests COMMAND zenplay_tests)

# 设置工作目录（如果测试需要读取资源文件）
# set_tests_properties(zenplay_all_tests PROPERTIES
#     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
# )

# 提示信息
message(STATUS "Unit tests configured:")
message(STATUS "  - Test executable: zenplay_tests")
message(STATUS "  - Test sources: ${TEST_SOURCES}")
message(STATUS "  - Player sources: ${PLAYER_SOURCES}")
message(STATUS "  - Run with: ctest or ./build/tests/zenplay_tests")
