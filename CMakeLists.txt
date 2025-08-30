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

add_subdirectory(third_party/loki)

# src files
file(GLOB_RECURSE SRC_FILES "src/*.cpp" "src/*.h" "src/*.ui")

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
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${DEPLOYQT_EXECUTABLE} $<TARGET_FILE:${PROJECT_NAME}>
        COMMENT "Deploying Qt libraries with windeployqt")
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