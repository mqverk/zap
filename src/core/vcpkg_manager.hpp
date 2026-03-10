#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace zap::core {

// Resolved information about a local vcpkg installation.
struct VcpkgInfo {
    std::filesystem::path root;       // e.g. /opt/vcpkg
    std::filesystem::path executable; // e.g. /opt/vcpkg/vcpkg
    std::filesystem::path toolchain;  // e.g. /opt/vcpkg/scripts/buildsystems/vcpkg.cmake
};

// Search for a vcpkg installation via:
//  1. VCPKG_ROOT environment variable
//  2. Common platform-specific default paths
// Returns std::nullopt when vcpkg cannot be located.
std::optional<VcpkgInfo> find_vcpkg();

// Install a single package in the given project directory (manifest mode).
// The project must already have a vcpkg.json written.
// Returns true on success.
bool vcpkg_install(const std::string& package,
                   const std::filesystem::path& project_root,
                   const VcpkgInfo& vcpkg);

// Install all packages declared in vcpkg.json found in project_root.
bool vcpkg_install_all(const std::filesystem::path& project_root,
                       const VcpkgInfo& vcpkg);

// Generate vcpkg.json content as a string.
std::string generate_vcpkg_json(const std::string& project_name,
                                 const std::string& version,
                                 const std::vector<std::string>& packages);

// Write a vcpkg.json manifest to project_root.
void write_vcpkg_manifest(const std::string& project_name,
                           const std::string& version,
                           const std::vector<std::string>& packages,
                           const std::filesystem::path& project_root);

} // namespace zap::core
