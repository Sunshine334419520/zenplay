cmake_minimum_required(VERSION 3.23 FATAL_ERROR)
PROJECT(loki)
option(WITH_DEMO "Whether to start demo? ON/OFF" OFF)
option(WITH_TEST "Whether to start test? ON/OFF" OFF)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	add_definitions(-DOS_LINUX)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	add_definitions(-DOS_MACOS)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	add_definitions(-DOS_WIN)
endif()

include_directories(${PROJECT_SOURCE}/src)

if(WIN32)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_CMakePredefinedTargets")

set(CMAKE_CXX_FLAGS_RELEASE "/MD")
set(CMAKE_CXX_FLAGS_DEBUG "/MDd")

list(APPEND CUR_COMPILER_FLAGS
    /MP           # Multiprocess compilation
    /Gy           # Enable function-level linking
    /GR-          # Disable run-time type information
    /W4           # Warning level 4
    /WX           # Treat warnings as errors
    /Zi
    /wd4100       # Ignore "unreferenced formal parameter" warning
    /wd4127       # Ignore "conditional expression is constant" warning
    /wd4244       # Ignore "conversion possible loss of data" warning
    /wd4481       # Ignore "nonstandard extension used: override" warning
    /wd4512       # Ignore "assignment operator could not be generated" warning
    /wd4701       # Ignore "potentially uninitialized local variable" warning
    /wd4702       # Ignore "unreachable code" warning
    /wd4996       # Ignore "function or variable may be unsafe" warning
    /wd4389
    /wd4456
    /wd4275
    /wd4251
    /wd4189
)
list(APPEND CUR_COMPILER_FLAGS_DEBUG
    /RTC1         # Disable optimizations
    /Od           # Enable basic run-time checks
)
list(APPEND CUR_COMPILER_FLAGS_RELEASE
  /O2           # Optimize for maximum speed
  /Ob2          # Inline any suitable function
  /GF           # Enable string pooling
)
list(APPEND CUR_LINKER_FLAGS_DEBUG
  /DEBUG        # Generate debug information
)

list(APPEND CUR_COMPILER_DEFINES
  WIN32 _WIN32 _WINDOWS             # Windows platform
  UNICODE _UNICODE                  # Unicode build
  WINVER=0x0601 _WIN32_WINNT=0x601  # Targeting Windows 7
  NOMINMAX                          # Use the standard's templated min/max
  WIN32_LEAN_AND_MEAN               # Exclude less common API declarations
  _HAS_EXCEPTIONS=0                 # Disable exceptions
)
else()
list(APPEND CUR_COMPILER_FLAGS
  -fPIC
  #-fvisibility=hidden
  -pthread
  -Wall
  -Werror
  -Wno-missing-field-initializers
  -Wno-unused-parameter
  -Wno-unused-function
  -Wno-error=comment
  -Wno-comment
  -fno-exceptions
  -fno-rtti
  -fno-threadsafe-statics
  -fvisibility-inlines-hidden
  -std=gnu++11
)
list(APPEND CUR_COMPILER_DEBUG_FLAGS
  -O0
  -g
)
list(APPEND CEF_COMPILER_FLAGS_RELEASE
  -O2
  -fno-ident
)
endif()

list(APPEND CUR_COMPILER_DEFINES_RELEASE
  NDEBUG _NDEBUG                    # Not a debug build
)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(THREADING_DIR ${SOURCE_DIR}/threading)
set(MESSAGE_LOOP_DIR ${SOURCE_DIR}/message_loop)

file(GLOB SOURCE_DIR_FILES
    "${SOURCE_DIR}/*.cpp"
    "${SOURCE_DIR}/*.cc"
    "${SOURCE_DIR}/*.h"
    "${SOURCE_DIR}/*.hpp"
)

file(GLOB MESSGE_LOOP_FILES
    "${MESSAGE_LOOP_DIR}/*.cpp"
    "${MESSAGE_LOOP_DIR}/*.cc"
    "${MESSAGE_LOOP_DIR}/*.h"
    "${MESSAGE_LOOP_DIR}/*.hpp"
)

file(GLOB THREADING_FILES
    "${THREADING_DIR}/*.cpp"
    "${THREADING_DIR}/*.cc"
    "${THREADING_DIR}/*.h"
    "${THREADING_DIR}/*.hpp"
)

source_group("loki" FILES ${SOURCE_DIR_FILES})
source_group(loki\\\\threading FILES ${THREADING_FILES})
source_group(loki\\\\message_loop FILES ${MESSGE_LOOP_FILES})

list(APPEND SRC_FILES ${SOURCE_DIR_FILES})
list(APPEND SRC_FILES ${MESSGE_LOOP_FILES})
list(APPEND SRC_FILES ${THREADING_FILES})

set(
	CMAKE_SHARED_LINKER_FLAGS_RELEASE 
	"${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF"
)
add_library(loki STATIC ${SRC_FILES})
set_target_properties(loki PROPERTIES FOLDER "loki")

# Compile flags
target_compile_options(loki PRIVATE ${CUR_COMPILER_FLAGS} ${CUR_DEMO_CXX_COMPILER_FLAGS})
target_compile_options(loki PRIVATE $<$<CONFIG:Debug>:${CUR_COMPILER_FLAGS_DEBUG} ${CUR_CXX_COMPILER_FLAGS_DEBUG}>)
target_compile_options(loki PRIVATE $<$<CONFIG:Release>:${CUR_COMPILER_FLAGS_RELEASE} ${CUR_CXX_COMPILER_FLAGS_RELEASE}>)
# Compile definitions.
target_compile_definitions(loki PRIVATE ${CUR_COMPILER_DEFINES})
target_compile_definitions(loki PRIVATE $<$<CONFIG:Debug>:${CUR_COMPILER_DEFINES_DEBUG}>)
target_compile_definitions(loki PRIVATE $<$<CONFIG:Release>:${CUR_COMPILER_DEFINES_RELEASE}>)

set_property(TARGET loki APPEND PROPERTY LINK_FLAGS_RELEASE /DEBUG)

set(LibLists
)

target_link_libraries(loki ${LibLists})
target_include_directories(loki PUBLIC ${SOURCE_DIR})

file(GLOB INCLUDE_LOKI_ROOT_FILES
  ${SOURCE_DIR}/loki_export.h
  ${SOURCE_DIR}/bind_util.h
  ${SOURCE_DIR}/callback.h
  ${SOURCE_DIR}/hash.h
  ${SOURCE_DIR}/location.h
  ${SOURCE_DIR}/single_thread_task_runner.h
  ${SOURCE_DIR}/sequenced_task_runner_helpers.h
  ${SOURCE_DIR}/sequenced_task_runner.h
  ${SOURCE_DIR}/macor.h
  ${SOURCE_DIR}/main_message_loop.h
  ${SOURCE_DIR}/main_message_loop_std.h
  ${SOURCE_DIR}/main_message_loop_with_not_main_thread.h
  ${SOURCE_DIR}/post_task_and_reply_with_result_internal.h
  ${SOURCE_DIR}/post_task_interface.h
  ${SOURCE_DIR}/task_runner.h
  ${SOURCE_DIR}/task_runner_util.h
)

file(GLOB INCLUDE_LOKI_THREADING_FILES
  ${SOURCE_DIR}/threading/thread_define.h
)

install(TARGETS loki
  RUNTIME DESTINATION "bin"
  LIBRARY DESTINATION "lib"
  ARCHIVE DESTINATION "lib"
)


install(
  FILES
  ${INCLUDE_LOKI_ROOT_FILES}
  DESTINATION "include/loki"
)

install(
  FILES
  ${INCLUDE_LOKI_THREADING_FILES}
  DESTINATION "include/loki/threading"
)

if(WITH_DEMO)
add_executable(demo ${CMAKE_CURRENT_SOURCE_DIR}/demo/sample.cpp)
target_include_directories(demo PRIVATE ${SOURCE_DIR})
target_link_libraries(demo loki pthread)

# Compile flags
target_compile_options(demo PRIVATE ${CUR_COMPILER_FLAGS} ${CUR_DEMO_CXX_COMPILER_FLAGS})
target_compile_options(demo PRIVATE $<$<CONFIG:Debug>:${CUR_COMPILER_FLAGS_DEBUG} ${CUR_CXX_COMPILER_FLAGS_DEBUG}>)
target_compile_options(demo PRIVATE $<$<CONFIG:Release>:${CUR_COMPILER_FLAGS_RELEASE} ${CUR_CXX_COMPILER_FLAGS_RELEASE}>)
# Compile definitions.
target_compile_definitions(demo PRIVATE ${CUR_COMPILER_DEFINES})
target_compile_definitions(demo PRIVATE $<$<CONFIG:Debug>:${CUR_COMPILER_DEFINES_DEBUG}>)
target_compile_definitions(demo PRIVATE $<$<CONFIG:Release>:${CUR_COMPILER_DEFINES_RELEASE}>)
endif()

if(WITH_TEST)

find_package(GTest REQUIRED)

add_executable(loki_unittest 
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/loki_unittest.cpp
)

if(WIN32)
set(LibLists
GTest::gtest
loki
)
else()
set(LibLists
GTest::gtest
loki
pthread
)
endif()
target_include_directories(loki_unittest PRIVATE ${SOURCE_DIR})
target_link_libraries(loki_unittest ${LibLists})
# Compile flags
target_compile_options(loki_unittest PRIVATE ${CUR_COMPILER_FLAGS} ${CUR_DEMO_CXX_COMPILER_FLAGS})
target_compile_options(loki_unittest PRIVATE $<$<CONFIG:Debug>:${CUR_COMPILER_FLAGS_DEBUG} ${CUR_CXX_COMPILER_FLAGS_DEBUG}>)
target_compile_options(loki_unittest PRIVATE $<$<CONFIG:Release>:${CUR_COMPILER_FLAGS_RELEASE} ${CUR_CXX_COMPILER_FLAGS_RELEASE}>)
# Compile definitions.
target_compile_definitions(loki_unittest PRIVATE ${CUR_COMPILER_DEFINES})
target_compile_definitions(loki_unittest PRIVATE $<$<CONFIG:Debug>:${CUR_COMPILER_DEFINES_DEBUG}>)
target_compile_definitions(loki_unittest PRIVATE $<$<CONFIG:Release>:${CUR_COMPILER_DEFINES_RELEASE}>)
endif()
