#pragma once

#include <filesystem>
#include <string>

namespace zap::core {

// Options controlling new project creation.
struct NewProjectOptions {
    std::string name;
    std::string version      = "0.1.0";
    std::string cpp_standard = "20";
    std::string description;
    std::string template_name;
    bool        create_tests  = true;
    bool        is_library    = false;
    bool        init_in_place = false;
};

// Scaffold a new project directory under parent_dir.
// Creates the full directory tree, writes zap.toml, vcpkg.json,
// CMakeLists.txt, a starter main.cpp, and an empty test stub.
// Throws std::runtime_error if the target directory already exists.
void create_new_project(const NewProjectOptions& opts,
                        const std::filesystem::path& parent_dir =
                            std::filesystem::current_path());

} // namespace zap::core
