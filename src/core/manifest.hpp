#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace zap::core {

// Metadata from the [project] section of zap.toml.
struct ProjectMeta {
    std::string name;
    std::string version      = "0.1.0";
    std::string cpp_standard = "20";
    std::string description;
};

// In-memory representation of a zap.toml manifest.
// Use Manifest::load() to parse from disk, and save() to persist changes.
struct Manifest {
    ProjectMeta                    project;
    std::map<std::string, std::string> dependencies;      // runtime deps
    std::map<std::string, std::string> dev_dependencies;  // test / tooling deps

    // Path to the zap.toml file on disk (set by load()).
    std::filesystem::path path;

    // ----------------------------------------------------------------
    // Factory helpers
    // ----------------------------------------------------------------

    // Parse a manifest from an explicit file path.
    static Manifest load(const std::filesystem::path& manifest_path);

    // Walk up from the current directory and load the first zap.toml found.
    // Throws std::runtime_error if no manifest is found.
    static Manifest load_from_cwd();

    // ----------------------------------------------------------------
    // Persistence
    // ----------------------------------------------------------------

    // Serialise and write the manifest back to disk (to `path`).
    void save() const;

    // ----------------------------------------------------------------
    // Dependency helpers
    // ----------------------------------------------------------------

    void add_dependency(const std::string& name, const std::string& version = "*");
    void add_dev_dependency(const std::string& name, const std::string& version = "*");
    void remove_dependency(const std::string& name);
    bool has_dependency(const std::string& name) const;

    // Return all dependency names (runtime + dev).
    std::vector<std::string> all_dependency_names() const;
};

} // namespace zap::core
