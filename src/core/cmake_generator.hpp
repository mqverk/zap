#pragma once

#include "manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace zap::core {

// Generate the CMakeLists.txt content for a user project as a string.
// If vcpkg_toolchain is provided, the generated file will reference it.
std::string generate_cmakelists(
    const Manifest& manifest,
    const std::optional<std::filesystem::path>& vcpkg_toolchain = std::nullopt);

// Write (or overwrite) CMakeLists.txt in the project root directory.
void write_cmakelists(
    const Manifest& manifest,
    const std::filesystem::path& project_root,
    const std::optional<std::filesystem::path>& vcpkg_toolchain = std::nullopt);

} // namespace zap::core
