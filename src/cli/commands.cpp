#include "commands.hpp"

#include "core/cmake_generator.hpp"
#include "core/manifest.hpp"
#include "core/project.hpp"
#include "core/vcpkg_manager.hpp"
#include "utils/fs.hpp"
#include "utils/process.hpp"
#include "zap/version.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace zap::cli {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Print a styled error message and return a non-zero exit code.
void print_error(const std::string& msg) {
    std::cerr << "error: " << msg << "\n";
}

// Locate vcpkg and warn if not found.
std::optional<zap::core::VcpkgInfo> require_vcpkg() {
    auto info = zap::core::find_vcpkg();
    if (!info) {
        std::cerr << "warning: vcpkg not found.\n"
                  << "         Set VCPKG_ROOT or install vcpkg to manage dependencies.\n"
                  << "         See: https://vcpkg.io/en/getting-started\n\n";
    }
    return info;
}

// Build the cmake configure command string, injecting the vcpkg toolchain
// when available.
std::string cmake_configure_cmd(bool release,
                                 const std::optional<zap::core::VcpkgInfo>& vcpkg)
{
    std::string cmd = "cmake -B build";
    if (release) cmd += " -DCMAKE_BUILD_TYPE=Release";
    else         cmd += " -DCMAKE_BUILD_TYPE=Debug";

    if (vcpkg) {
        cmd += " -DCMAKE_TOOLCHAIN_FILE=\"" + vcpkg->toolchain.string() + "\"";
    }
    return cmd;
}

// Return the path of the compiled executable (best guess).
fs::path locate_executable(const fs::path& project_root,
                            const std::string& name)
{
    // Try Debug and Release sub-directories first (Windows multi-config generators).
    for (auto& sub : {"Debug", "Release", ""}) {
        fs::path candidate = project_root / "build" / sub / name;
#ifdef _WIN32
        auto wincandidate = candidate;
        wincandidate.replace_extension(".exe");
        if (fs::exists(wincandidate)) return wincandidate;
#endif
        if (fs::exists(candidate)) return candidate;
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// Command: zap new <project-name>
// ---------------------------------------------------------------------------

void cmd_new(const std::string& name, const std::string& std_version,
             bool is_lib, const std::string& tpl) {
    zap::core::NewProjectOptions opts;
    opts.name         = name;
    opts.cpp_standard = std_version;
    opts.is_library   = is_lib;
    if (!tpl.empty()) opts.template_name = tpl;
    zap::core::create_new_project(opts);
}

// ---------------------------------------------------------------------------
// Command: zap add <package> [--dev] [--version <ver>]
// ---------------------------------------------------------------------------

void cmd_add(const std::string& package,
             const std::string& version,
             bool dev)
{
    auto manifest = zap::core::Manifest::load_from_cwd();
    const auto root = manifest.path.parent_path();

    if (manifest.has_dependency(package)) {
        std::cout << "  Already listed: " << package << "\n";
    } else {
        if (dev) manifest.add_dev_dependency(package, version);
        else     manifest.add_dependency(package, version);

        manifest.save();
        std::cout << "  Added " << package << " = \"" << version << "\""
                  << (dev ? " (dev)" : "") << "\n";
    }

    // Sync vcpkg.json with the full dependency list.
    zap::core::write_vcpkg_manifest(
        manifest.project.name,
        manifest.project.version,
        manifest.all_dependency_names(),
        root);

    // Regenerate CMakeLists.txt to include the new find_package() calls.
    auto vcpkg = require_vcpkg();
    std::optional<fs::path> toolchain;
    if (vcpkg) toolchain = vcpkg->toolchain;
    zap::core::write_cmakelists(manifest, root, toolchain);
    std::cout << "  Updated   CMakeLists.txt\n";

    std::cout << "\n  hint: run 'zap build' to install and build.\n";
}

// ---------------------------------------------------------------------------
// Command: zap remove <package>
// ---------------------------------------------------------------------------

void cmd_remove(const std::string& package) {
    auto manifest = zap::core::Manifest::load_from_cwd();
    const auto root = manifest.path.parent_path();

    if (!manifest.has_dependency(package)) {
        print_error("package '" + package + "' is not listed in zap.toml");
        return;
    }

    manifest.remove_dependency(package);
    manifest.save();

    zap::core::write_vcpkg_manifest(
        manifest.project.name,
        manifest.project.version,
        manifest.all_dependency_names(),
        root);

    auto vcpkg = require_vcpkg();
    std::optional<fs::path> toolchain;
    if (vcpkg) toolchain = vcpkg->toolchain;
    zap::core::write_cmakelists(manifest, root, toolchain);

    std::cout << "  Removed   " << package << "\n";
    std::cout << "  hint: run 'zap build' to rebuild without the package.\n";
}

// ---------------------------------------------------------------------------
// Command: zap build [--release]
// ---------------------------------------------------------------------------

void cmd_build(bool release, bool clean,
               const std::string& target_platform,
               const std::string& profile) {
    auto manifest = zap::core::Manifest::load_from_cwd();
    const auto root = manifest.path.parent_path();

    if (clean) {
        auto build_dir = root / "build";
        if (fs::exists(build_dir)) {
            std::cout << "  Cleaning build directory...\n";
            fs::remove_all(build_dir);
        }
    }

    auto vcpkg = require_vcpkg();
    std::optional<fs::path> toolchain;
    if (vcpkg) toolchain = vcpkg->toolchain;

    // Always regenerate CMakeLists.txt to pick up any manifest changes.
    std::cout << "  Generating CMakeLists.txt...\n";
    zap::core::write_cmakelists(manifest, root, toolchain);

    // Ensure build directory exists.
    zap::utils::ensure_directory(root / "build");

    // cmake configure
    std::string configure_cmd = cmake_configure_cmd(release, vcpkg);
    if (!target_platform.empty())
        configure_cmd += " -DCMAKE_SYSTEM_NAME=" + target_platform;
    if (!profile.empty())
        configure_cmd += " -DCMAKE_BUILD_TYPE=" + profile;

    std::cout << "  Configuring...\n";
    int rc = zap::utils::run_command_in(configure_cmd, root);
    if (rc != 0) {
        throw std::runtime_error("CMake configure step failed (exit code " +
                                 std::to_string(rc) + ")");
    }

    // cmake build
    std::string build_cmd = "cmake --build build";
    if (release) build_cmd += " --config Release";

    std::cout << "  Building...\n";
    rc = zap::utils::run_command_in(build_cmd, root);
    if (rc != 0) {
        throw std::runtime_error("CMake build step failed (exit code " +
                                 std::to_string(rc) + ")");
    }

    std::cout << "\n  Build succeeded.\n";
}

// Forward declaration -- defined later in this file.
static void cmd_run_watch();

// ---------------------------------------------------------------------------
// Command: zap run [-- <args...>]
// ---------------------------------------------------------------------------

void cmd_run(const std::vector<std::string>& extra_args,
             bool release, bool watch) {
    if (watch) {
        cmd_run_watch();
        return;
    }

    auto manifest = zap::core::Manifest::load_from_cwd();
    const auto root = manifest.path.parent_path();

    auto exe = locate_executable(root, manifest.project.name);
    if (exe.empty()) {
        std::cout << "  Executable not found, building first...\n\n";
        cmd_build(false, false, "", "");
        exe = locate_executable(root, manifest.project.name);
    }

    if (exe.empty()) {
        throw std::runtime_error(
            "Compiled executable not found in build/. Run 'zap build' first.");
    }

    std::string run_cmd = "\"" + exe.string() + "\"";
    for (auto& arg : extra_args) run_cmd += " " + arg;

    std::cout << "  Running " << exe.filename().string() << " ...\n\n";
    std::exit(zap::utils::run_command(run_cmd));
}

// ---------------------------------------------------------------------------
// Command: zap install  (sync all dependencies from manifest)
// ---------------------------------------------------------------------------

void cmd_install() {
    auto manifest = zap::core::Manifest::load_from_cwd();
    const auto root = manifest.path.parent_path();

    // Ensure vcpkg.json reflects the current manifest.
    zap::core::write_vcpkg_manifest(
        manifest.project.name,
        manifest.project.version,
        manifest.all_dependency_names(),
        root);

    auto vcpkg = zap::core::find_vcpkg();
    if (!vcpkg) {
        print_error("vcpkg not found. Install vcpkg and set VCPKG_ROOT.");
        return;
    }

    std::cout << "  Installing dependencies via vcpkg...\n";
    bool ok = zap::core::vcpkg_install_all(root, *vcpkg);
    if (!ok) {
        throw std::runtime_error("vcpkg install failed.");
    }
    std::cout << "  All dependencies installed.\n";
}

// ---------------------------------------------------------------------------
// Command: zap clean
// ---------------------------------------------------------------------------

void cmd_clean() {
    auto root = zap::utils::require_project_root();
    auto build_dir = root / "build";

    if (!fs::exists(build_dir)) {
        std::cout << "  Nothing to clean (build/ does not exist).\n";
        return;
    }

    std::cout << "  Removing build/...\n";
    fs::remove_all(build_dir);
    fs::create_directory(build_dir); // keep the empty placeholder
    std::cout << "  Done.\n";
}

// ---------------------------------------------------------------------------
// Command: zap doctor
// ---------------------------------------------------------------------------

void cmd_doctor() {
    std::cout << "zap doctor\n";
    std::cout << "==========\n\n";

    // cmake
    bool have_cmake = zap::utils::program_exists("cmake");
    std::cout << "  cmake    : " << (have_cmake ? "found" : "NOT FOUND") << "\n";
    if (!have_cmake) {
        std::cout << "             Install cmake 3.20+: https://cmake.org/download/\n";
    }

    // C++ compiler
#ifdef _WIN32
    bool have_cc = zap::utils::program_exists("cl");
    std::cout << "  MSVC     : " << (have_cc ? "found" : "NOT FOUND") << "\n";
#else
    bool have_gcc   = zap::utils::program_exists("g++");
    bool have_clang = zap::utils::program_exists("clang++");
    std::cout << "  g++      : " << (have_gcc   ? "found" : "NOT FOUND") << "\n";
    std::cout << "  clang++  : " << (have_clang ? "found" : "NOT FOUND") << "\n";
    if (!have_gcc && !have_clang) {
        std::cout << "             Install GCC or Clang from your system package manager.\n";
    }
#endif

    // ninja (optional but recommended)
    bool have_ninja = zap::utils::program_exists("ninja");
    std::cout << "  ninja    : " << (have_ninja ? "found" : "not found (optional)") << "\n";

    // git
    bool have_git = zap::utils::program_exists("git");
    std::cout << "  git      : " << (have_git ? "found" : "NOT FOUND") << "\n";

    // vcpkg
    auto vcpkg = zap::core::find_vcpkg();
    if (vcpkg) {
        std::cout << "  vcpkg    : found at " << vcpkg->root.string() << "\n";
    } else {
        std::cout << "  vcpkg    : NOT FOUND\n";
        std::cout << "             Set VCPKG_ROOT or install: https://vcpkg.io\n";
    }

    std::cout << "\n";
    if (have_cmake && (have_gcc || have_clang || have_ninja) && vcpkg)
        std::cout << "  All essential tools are present — you're good to go!\n";
    else
        std::cout << "  Fix the issues above, then run 'zap doctor' again.\n";
}

// ---------------------------------------------------------------------------
// Command: zap test
// ---------------------------------------------------------------------------

void cmd_test() {
    auto manifest = zap::core::Manifest::load_from_cwd();
    const auto root = manifest.path.parent_path();

    // Build in debug mode first.
    cmd_build(false, false, "", "");

    std::cout << "\n  Running tests...\n\n";
    int rc = zap::utils::run_command_in("ctest --output-on-failure", root / "build");
    if (rc != 0) {
        throw std::runtime_error("Tests failed (exit code " + std::to_string(rc) + ")");
    }
    std::cout << "\n  All tests passed.\n";
}

// ---------------------------------------------------------------------------
// Command: zap update [<package>]
// ---------------------------------------------------------------------------

void cmd_update(const std::string& package) {
    auto manifest = zap::core::Manifest::load_from_cwd();
    const auto root = manifest.path.parent_path();

    if (package.empty()) {
        std::cout << "  Updating all dependencies...\n";
    } else {
        if (!manifest.has_dependency(package)) {
            print_error("package '" + package + "' is not listed in zap.toml");
            return;
        }
        std::cout << "  Updating " << package << "...\n";
    }

    // With vcpkg manifest mode, updating means re-installing (vcpkg resolves
    // the latest compatible version).
    auto vcpkg = zap::core::find_vcpkg();
    if (!vcpkg) {
        print_error("vcpkg not found. Cannot update dependencies.");
        return;
    }

    zap::core::write_vcpkg_manifest(
        manifest.project.name,
        manifest.project.version,
        manifest.all_dependency_names(),
        root);

    bool ok = zap::core::vcpkg_install_all(root, *vcpkg);
    if (!ok) {
        throw std::runtime_error("vcpkg install failed during update.");
    }

    std::cout << "  Dependencies updated.\n";
}

// ---------------------------------------------------------------------------
// Command: zap version
// ---------------------------------------------------------------------------

void cmd_version() {
    std::cout << "zap " << zap::VERSION << "\n";
    std::cout << zap::DESCRIPTION << "\n";
    std::cout << zap::HOMEPAGE << "\n";
}

// ---------------------------------------------------------------------------
// Command: zap run --watch  (rebuild + rerun on file changes)
// ---------------------------------------------------------------------------

static void cmd_run_watch() {
    auto root = zap::utils::require_project_root();
    auto manifest = zap::core::Manifest::load_from_cwd();

    if (!zap::utils::program_exists("inotifywait") &&
        !zap::utils::program_exists("fswatch")) {
        print_error("No file-watch utility found.\n"
                    "         Install inotify-tools (Linux) or fswatch (macOS).");
        return;
    }

    std::cout << "  Watching for changes -- rebuilding and running on each save...\n\n";
    std::string exe_name = manifest.project.name;
#ifdef __APPLE__
    std::string watch_cmd =
        "fswatch -o src/ include/ | xargs -n1 -I{} sh -c "
        "'cmake --build build && ./build/" + exe_name + "'";
#else
    std::string watch_cmd =
        "while inotifywait -r -e modify,create,delete src/ include/ 2>/dev/null; do "
        "cmake --build build && ./build/" + exe_name + "; done";
#endif
    zap::utils::run_command_in(watch_cmd, root);
}

// ---------------------------------------------------------------------------
// Command: zap publish  (stub -- no central C++ registry yet)
// ---------------------------------------------------------------------------

void cmd_publish() {
    std::cout << "  zap publish is not yet implemented.\n";
    std::cout << "  C++ does not have a universal package registry.\n";
    std::cout << "  Consider publishing your vcpkg port to the vcpkg registry:\n";
    std::cout << "  https://learn.microsoft.com/en-us/vcpkg/produce/publish-to-a-git-registry\n";
}

// ---------------------------------------------------------------------------
// CLI registration
// ---------------------------------------------------------------------------

void register_commands(CLI::App& app) {
    app.require_subcommand(1);

    // ---- new ---------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("new", "Create a new C++ project");
        auto* name = new std::string;
        auto* std_ver = new std::string{"20"};
        auto* is_lib = new bool{false};
        auto* tpl = new std::string;
        sub->add_option("name", *name, "Project name")->required();
        sub->add_option("--std", *std_ver, "C++ standard (default: 20)");
        sub->add_flag("--lib", *is_lib, "Create a library project");
        sub->add_option("--template", *tpl, "Project template name");
        sub->callback([name, std_ver, is_lib, tpl] {
            cmd_new(*name, *std_ver, *is_lib, *tpl);
        });
    }

    // ---- add ---------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("add", "Add a dependency");
        auto* pkg = new std::string;
        auto* ver = new std::string{"*"};
        auto* dev = new bool{false};
        sub->add_option("package", *pkg, "Package name (vcpkg)")->required();
        sub->add_option("--version", *ver, "Version constraint (default: *)");
        sub->add_flag("--dev", *dev, "Add as a dev dependency");
        sub->callback([pkg, ver, dev] {
            cmd_add(*pkg, *ver, *dev);
        });
    }

    // ---- remove ------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("remove", "Remove a dependency");
        auto* pkg = new std::string;
        sub->add_option("package", *pkg, "Package name")->required();
        sub->callback([pkg] { cmd_remove(*pkg); });
    }

    // ---- build -------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("build", "Build the project");
        auto* release = new bool{false};
        auto* clean = new bool{false};
        auto* tgt = new std::string;
        auto* prof = new std::string;
        sub->add_flag("--release", *release, "Build in release mode");
        sub->add_flag("--clean", *clean, "Clean build directory before building");
        sub->add_option("--target", *tgt, "Target platform (sets CMAKE_SYSTEM_NAME)");
        sub->add_option("--profile", *prof, "CMake build type (Debug/Release/RelWithDebInfo)");
        sub->callback([release, clean, tgt, prof] {
            cmd_build(*release, *clean, *tgt, *prof);
        });
    }

    // ---- run ---------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("run", "Build (if needed) and run the executable");
        auto* extra = new std::vector<std::string>;
        auto* release = new bool{false};
        auto* watch = new bool{false};
        sub->add_option("args", *extra, "Arguments forwarded to the executable");
        sub->add_flag("--release", *release, "Build and run in release mode");
        sub->add_flag("--watch", *watch, "Rebuild and re-run on file changes");
        sub->callback([extra, release, watch] { cmd_run(*extra, *release, *watch); });
    }

    // ---- install -----------------------------------------------------------
    {
        auto* sub = app.add_subcommand("install", "Install all dependencies from zap.toml");
        sub->callback([] { cmd_install(); });
    }

    // ---- clean -------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("clean", "Remove the build directory");
        sub->callback([] { cmd_clean(); });
    }

    // ---- doctor ------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("doctor", "Check development environment");
        sub->callback([] { cmd_doctor(); });
    }

    // ---- test --------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("test", "Build and run tests");
        sub->callback([] { cmd_test(); });
    }

    // ---- update ------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("update", "Update dependencies to latest versions");
        auto* pkg = new std::string;
        sub->add_option("package", *pkg, "Package to update (omit to update all)");
        sub->callback([pkg] { cmd_update(*pkg); });
    }

    // ---- publish -----------------------------------------------------------
    {
        auto* sub = app.add_subcommand("publish", "Publish the project (experimental)");
        sub->callback([] { cmd_publish(); });
    }

    // ---- version -----------------------------------------------------------
    {
        auto* sub = app.add_subcommand("version", "Print zap version");
        sub->callback([] { cmd_version(); });
    }
}

} // namespace zap::cli
