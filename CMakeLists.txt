cmake_minimum_required(VERSION 3.20)

project(zap VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for editor tooling (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------
# Dependencies (provided via vcpkg)
# ------------------------------------------------------------
find_package(CLI11 CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

# ------------------------------------------------------------
# Sources
# ------------------------------------------------------------
set(ZAP_SOURCES
    src/main.cpp
    src/cli/commands.cpp
    src/core/project.cpp
    src/core/manifest.cpp
    src/core/cmake_generator.cpp
    src/core/vcpkg_manager.cpp
    src/utils/fs.cpp
    src/utils/process.cpp
)

add_executable(zap ${ZAP_SOURCES})

target_include_directories(zap PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(zap PRIVATE
    CLI11::CLI11
    tomlplusplus::tomlplusplus
)

# On MSVC, use Unicode and suppress CRT warnings
if(MSVC)
    target_compile_definitions(zap PRIVATE
        UNICODE _UNICODE
        _CRT_SECURE_NO_WARNINGS
    )
    target_compile_options(zap PRIVATE /W4 /permissive-)
else()
    target_compile_options(zap PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ------------------------------------------------------------
# Install
# ------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS zap RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
