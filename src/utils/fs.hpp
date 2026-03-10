#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace zap::utils {

// Walk up the directory tree looking for a zap.toml manifest.
// Returns the path to the manifest file if found, std::nullopt otherwise.
std::optional<std::filesystem::path> find_manifest(
    const std::filesystem::path& start = std::filesystem::current_path());

// Create a directory (and all parents). Succeeds silently if it already exists.
void ensure_directory(const std::filesystem::path& path);

// Write the entire string content to a file, overwriting any existing content.
void write_file(const std::filesystem::path& path, const std::string& content);

// Read an entire file into a string. Throws std::runtime_error if the file
// cannot be opened.
std::string read_file(const std::filesystem::path& path);

// Return the canonical path of the project root (directory containing zap.toml).
// Throws std::runtime_error if no manifest is found.
std::filesystem::path require_project_root();

} // namespace zap::utils
