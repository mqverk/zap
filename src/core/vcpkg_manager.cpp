#include "vcpkg_manager.hpp"

#include "utils/fs.hpp"
#include "utils/process.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace zap::core {

// ---------------------------------------------------------------------------
// vcpkg discovery
// ---------------------------------------------------------------------------

namespace {

// Common default install locations, checked in order when VCPKG_ROOT is unset.
std::vector<std::filesystem::path> default_vcpkg_paths() {
    return {
#ifdef _WIN32
        "C:/vcpkg",
        "C:/src/vcpkg",
        std::string(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "") + "/vcpkg",
        std::string(std::getenv("LOCALAPPDATA") ? std::getenv("LOCALAPPDATA") : "") + "/vcpkg",
#else
        "/usr/local/share/vcpkg",
        "/opt/vcpkg",
        std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/vcpkg",
        std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.vcpkg",
#endif
    };
}

#ifdef _WIN32
const std::string VCPKG_EXE = "vcpkg.exe";
#else
const std::string VCPKG_EXE = "vcpkg";
#endif

VcpkgInfo make_info(const std::filesystem::path& root) {
    return VcpkgInfo{
        root,
        root / VCPKG_EXE,
        root / "scripts" / "buildsystems" / "vcpkg.cmake"
    };
}

} // namespace

std::optional<VcpkgInfo> find_vcpkg() {
    // 1. Respect VCPKG_ROOT if set.
    const char* env_root = std::getenv("VCPKG_ROOT");
    if (env_root && *env_root) {
        std::filesystem::path root(env_root);
        auto exe = root / VCPKG_EXE;
        if (std::filesystem::exists(exe)) {
            return make_info(root);
        }
    }

    // 2. Check whether vcpkg is on PATH.
    if (zap::utils::program_exists("vcpkg")) {
        // Find the actual path via which/where.
#ifdef _WIN32
        auto res = zap::utils::run_command_capture("where vcpkg");
#else
        auto res = zap::utils::run_command_capture("which vcpkg");
#endif
        if (res.exit_code == 0 && !res.output.empty()) {
            // Trim trailing whitespace / newline.
            auto out = res.output;
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
                out.pop_back();
            std::filesystem::path exe(out);
            if (std::filesystem::exists(exe)) {
                return make_info(exe.parent_path());
            }
        }
    }

    // 3. Try well-known default paths.
    for (auto& candidate : default_vcpkg_paths()) {
        if (candidate.empty()) continue;
        auto exe = candidate / VCPKG_EXE;
        if (std::filesystem::exists(exe)) {
            return make_info(candidate);
        }
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Package installation
// ---------------------------------------------------------------------------

bool vcpkg_install(const std::string& package,
                   const std::filesystem::path& project_root,
                   const VcpkgInfo& vcpkg)
{
    // When a vcpkg.json is present we install via manifest mode:
    // vcpkg.json is the source of truth, so we just trigger an install-all.
    (void)package; // the manifest already has the package listed
    return vcpkg_install_all(project_root, vcpkg);
}

bool vcpkg_install_all(const std::filesystem::path& project_root,
                        const VcpkgInfo& vcpkg)
{
    // Run: <vcpkg_exe> install --triplet <host-triplet>
    // from the project root so that vcpkg picks up vcpkg.json.
    std::string cmd = "\"" + vcpkg.executable.string() + "\" install";

    int rc = zap::utils::run_command_in(cmd, project_root);
    return rc == 0;
}

// ---------------------------------------------------------------------------
// vcpkg.json generation
// ---------------------------------------------------------------------------

std::string generate_vcpkg_json(const std::string& project_name,
                                 const std::string& version,
                                 const std::vector<std::string>& packages)
{
    // Produce a vcpkg manifest JSON without pulling in a JSON library.
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"name\": \"" << project_name << "\",\n";
    ss << "  \"version\": \"" << version << "\",\n";
    ss << "  \"dependencies\": [\n";
    for (std::size_t i = 0; i < packages.size(); ++i) {
        ss << "    \"" << packages[i] << "\"";
        if (i + 1 < packages.size()) ss << ",";
        ss << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

void write_vcpkg_manifest(const std::string& project_name,
                           const std::string& version,
                           const std::vector<std::string>& packages,
                           const std::filesystem::path& project_root)
{
    auto content = generate_vcpkg_json(project_name, version, packages);
    zap::utils::write_file(project_root / "vcpkg.json", content);
}

} // namespace zap::core
