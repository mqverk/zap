#include "fs.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace zap::utils {

std::optional<std::filesystem::path> find_manifest(const std::filesystem::path& start) {
    auto current = std::filesystem::absolute(start);

    // Walk upward until we find zap.toml or hit the root.
    while (true) {
        auto candidate = current / "zap.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }

        auto parent = current.parent_path();
        if (parent == current) {
            // Reached the filesystem root without finding a manifest.
            break;
        }
        current = parent;
    }

    return std::nullopt;
}

void ensure_directory(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    ensure_directory(path.parent_path());

    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot write file: " + path.string());
    }
    file << content;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot read file: " + path.string());
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::filesystem::path require_project_root() {
    auto manifest = find_manifest();
    if (!manifest) {
        throw std::runtime_error(
            "error: no zap.toml found in this directory or any parent directory\n"
            "hint: run 'zap new <name>' to create a new project");
    }
    return manifest->parent_path();
}

} // namespace zap::utils
