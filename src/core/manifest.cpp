#include "manifest.hpp"

#include "utils/fs.hpp"

#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace zap::core {

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

Manifest Manifest::load(const std::filesystem::path& manifest_path) {
    Manifest m;
    m.path = std::filesystem::absolute(manifest_path);

    try {
        auto config = toml::parse_file(manifest_path.string());

        // [project]
        m.project.name         = config["project"]["name"].value<std::string>().value_or("unnamed");
        m.project.version      = config["project"]["version"].value<std::string>().value_or("0.1.0");
        m.project.cpp_standard = config["project"]["cpp_standard"].value<std::string>().value_or("20");
        m.project.description  = config["project"]["description"].value<std::string>().value_or("");

        // [dependencies]
        if (auto* deps = config["dependencies"].as_table()) {
            for (auto&& [key, val] : *deps) {
                m.dependencies[std::string(key.str())] =
                    val.value<std::string>().value_or("*");
            }
        }

        // [dev-dependencies]
        if (auto* dev = config["dev-dependencies"].as_table()) {
            for (auto&& [key, val] : *dev) {
                m.dev_dependencies[std::string(key.str())] =
                    val.value<std::string>().value_or("*");
            }
        }

    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            std::string("error: failed to parse zap.toml: ") + e.description().data());
    }

    return m;
}

Manifest Manifest::load_from_cwd() {
    auto manifest_path = zap::utils::find_manifest();
    if (!manifest_path) {
        throw std::runtime_error(
            "error: no zap.toml found in this directory or any parent directory\n"
            "hint: run 'zap new <name>' to create a new project");
    }
    return load(*manifest_path);
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

void Manifest::save() const {
    std::ostringstream ss;

    ss << "[project]\n";
    ss << "name = \"" << project.name << "\"\n";
    ss << "version = \"" << project.version << "\"\n";
    ss << "cpp_standard = \"" << project.cpp_standard << "\"\n";
    if (!project.description.empty()) {
        ss << "description = \"" << project.description << "\"\n";
    }
    ss << "\n";

    ss << "[dependencies]\n";
    if (dependencies.empty()) {
        ss << "# add dependencies with: zap add <package>\n";
    } else {
        for (auto& [name, ver] : dependencies) {
            ss << name << " = \"" << ver << "\"\n";
        }
    }

    if (!dev_dependencies.empty()) {
        ss << "\n[dev-dependencies]\n";
        for (auto& [name, ver] : dev_dependencies) {
            ss << name << " = \"" << ver << "\"\n";
        }
    }

    zap::utils::write_file(path, ss.str());
}

// ---------------------------------------------------------------------------
// Dependency helpers
// ---------------------------------------------------------------------------

void Manifest::add_dependency(const std::string& name, const std::string& version) {
    dependencies[name] = version;
}

void Manifest::add_dev_dependency(const std::string& name, const std::string& version) {
    dev_dependencies[name] = version;
}

void Manifest::remove_dependency(const std::string& name) {
    dependencies.erase(name);
    dev_dependencies.erase(name);
}

bool Manifest::has_dependency(const std::string& name) const {
    return dependencies.count(name) > 0 || dev_dependencies.count(name) > 0;
}

std::vector<std::string> Manifest::all_dependency_names() const {
    std::vector<std::string> names;
    names.reserve(dependencies.size() + dev_dependencies.size());
    for (auto& [k, _] : dependencies)     names.push_back(k);
    for (auto& [k, _] : dev_dependencies) names.push_back(k);
    return names;
}

} // namespace zap::core
