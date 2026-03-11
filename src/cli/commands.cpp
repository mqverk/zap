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

void cmd_publish(bool dry_run) {
    if (dry_run) {
        std::cout << "  [dry-run] Would publish package (no files uploaded).\n";
        auto manifest = zap::core::Manifest::load_from_cwd();
        std::cout << "  name:    " << manifest.project.name << "\n";
        std::cout << "  version: " << manifest.project.version << "\n";
        return;
    }
    std::cout << "  zap publish is not yet implemented.\n";
    std::cout << "  C++ does not have a universal package registry.\n";
    std::cout << "  Consider publishing your vcpkg port to the vcpkg registry:\n";
    std::cout << "  https://learn.microsoft.com/en-us/vcpkg/produce/publish-to-a-git-registry\n";
}

// ---------------------------------------------------------------------------
// Command: zap list
// ---------------------------------------------------------------------------

void cmd_list() {
    auto manifest = zap::core::Manifest::load_from_cwd();

    std::cout << "  Dependencies for " << manifest.project.name << ":\n\n";

    bool any = false;
    for (auto& [name, ver] : manifest.dependencies) {
        std::cout << "    " << name << " = \"" << ver << "\"\n";
        any = true;
    }
    for (auto& [name, ver] : manifest.dev_dependencies) {
        std::cout << "    " << name << " = \"" << ver << "\"  (dev)\n";
        any = true;
    }

    if (!any) std::cout << "    (no dependencies listed)\n";
}

// ---------------------------------------------------------------------------
// Command: zap outdated
// ---------------------------------------------------------------------------

void cmd_outdated() {
    auto vcpkg = require_vcpkg();
    if (!vcpkg) return;

    std::cout << "  Checking for outdated packages...\n\n";
    int rc = zap::utils::run_command(
        "\"" + vcpkg->executable.string() + "\" list");
    if (rc != 0) {
        throw std::runtime_error("vcpkg list failed.");
    }
    std::cout << "\n  Tip: Update your vcpkg baseline to pick up newer versions.\n";
}

// ---------------------------------------------------------------------------
// Command: zap search <pkg>
// ---------------------------------------------------------------------------

void cmd_search(const std::string& pkg) {
    auto vcpkg = require_vcpkg();
    if (!vcpkg) return;

    std::cout << "  Searching vcpkg for '" << pkg << "'...\n\n";
    int rc = zap::utils::run_command(
        "\"" + vcpkg->executable.string() + "\" search " + pkg);
    if (rc != 0) {
        throw std::runtime_error("vcpkg search failed.");
    }
}

// ---------------------------------------------------------------------------
// Command: zap info <pkg>
// ---------------------------------------------------------------------------

void cmd_info(const std::string& pkg) {
    auto vcpkg = require_vcpkg();
    if (!vcpkg) return;

    int rc = zap::utils::run_command(
        "\"" + vcpkg->executable.string() + "\" show " + pkg);
    if (rc != 0) {
        zap::utils::run_command(
            "\"" + vcpkg->executable.string() + "\" search \"^" + pkg + "$\"");
    }
}

// ---------------------------------------------------------------------------
// Command: zap init
// ---------------------------------------------------------------------------

void cmd_init() {
    auto cwd = fs::current_path();
    auto manifest_path = cwd / "zap.toml";

    if (fs::exists(manifest_path)) {
        print_error("zap.toml already exists in this directory.");
        return;
    }

    std::string name = cwd.filename().string();
    zap::core::NewProjectOptions opts;
    opts.name = name;
    opts.init_in_place = true;
    zap::core::create_new_project(opts, cwd);

    std::cout << "  Initialized project '" << name << "' in current directory.\n";
}

// ---------------------------------------------------------------------------
// Command: zap fmt
// ---------------------------------------------------------------------------

void cmd_fmt() {
    if (!zap::utils::program_exists("clang-format")) {
        print_error("clang-format not found. Install it from your package manager.");
        return;
    }
    auto root = zap::utils::require_project_root();
    std::cout << "  Formatting source files with clang-format...\n";
    int rc = zap::utils::run_command_in(
        "find src include -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i",
        root);
    if (rc != 0) throw std::runtime_error("clang-format failed.");
    std::cout << "  Done.\n";
}

// ---------------------------------------------------------------------------
// Command: zap lint
// ---------------------------------------------------------------------------

void cmd_lint() {
    if (!zap::utils::program_exists("clang-tidy")) {
        print_error("clang-tidy not found. Install it from your package manager.");
        return;
    }
    auto root = zap::utils::require_project_root();
    auto cc = root / "build" / "compile_commands.json";
    if (!fs::exists(cc)) {
        std::cout << "  compile_commands.json not found -- running cmake configure first...\n";
        auto vcpkg = require_vcpkg();
        std::string conf = cmake_configure_cmd(false, vcpkg)
                         + " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";
        int rc = zap::utils::run_command_in(conf, root);
        if (rc != 0) throw std::runtime_error("cmake configure failed.");
    }
    std::cout << "  Running clang-tidy...\n";
    int rc = zap::utils::run_command_in(
        "find src -name '*.cpp' | xargs clang-tidy -p build", root);
    if (rc != 0) throw std::runtime_error("clang-tidy reported issues.");
    std::cout << "  Lint clean.\n";
}

// ---------------------------------------------------------------------------
// Command: zap check
// ---------------------------------------------------------------------------

void cmd_check() {
    auto root = zap::utils::require_project_root();
    bool found = false;

    if (zap::utils::program_exists("clang-tidy")) {
        found = true;
        auto cc = root / "build" / "compile_commands.json";
        if (!fs::exists(cc)) {
            auto vcpkg = require_vcpkg();
            std::string conf = cmake_configure_cmd(false, vcpkg)
                             + " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";
            zap::utils::run_command_in(conf, root);
        }
        std::cout << "  Running clang-tidy (static analysis)...\n";
        zap::utils::run_command_in(
            "find src -name '*.cpp' | xargs clang-tidy -p build --quiet", root);
    }

    if (zap::utils::program_exists("cppcheck")) {
        found = true;
        std::cout << "  Running cppcheck...\n";
        zap::utils::run_command_in("cppcheck --enable=all --quiet src/", root);
    }

    if (!found) {
        std::cout << "  No static analysis tools found.\n";
        std::cout << "  Install clang-tidy or cppcheck for source checking.\n";
        return;
    }
    std::cout << "  Check complete.\n";
}

// ---------------------------------------------------------------------------
// Command: zap bench
// ---------------------------------------------------------------------------

void cmd_bench() {
    auto manifest = zap::core::Manifest::load_from_cwd();
    const auto root = manifest.path.parent_path();

    cmd_build(true, false, "", ""); // benchmarks run in release

    std::cout << "\n  Running benchmarks...\n\n";
    int rc = zap::utils::run_command_in(
        "ctest --output-on-failure -L benchmark", root / "build");
    if (rc != 0) {
        for (auto& entry : fs::directory_iterator(root / "build")) {
            auto n = entry.path().filename().string();
            if (n.find("bench") != std::string::npos) {
                zap::utils::run_command("\"" + entry.path().string() + "\"");
                return;
            }
        }
        throw std::runtime_error("No benchmark targets found. "
            "Add a CTest label 'benchmark' or name your binary '*bench*'.");
    }
}

// ---------------------------------------------------------------------------
// Command: zap coverage
// ---------------------------------------------------------------------------

void cmd_coverage() {
    if (!zap::utils::program_exists("lcov") &&
        !zap::utils::program_exists("gcovr")) {
        print_error("lcov or gcovr not found. Install one to generate coverage reports.");
        return;
    }
    auto root = zap::utils::require_project_root();
    auto vcpkg = require_vcpkg();
    std::string conf = cmake_configure_cmd(false, vcpkg)
                     + " -DCMAKE_CXX_FLAGS=\"--coverage\""
                     + " -DCMAKE_EXE_LINKER_FLAGS=\"--coverage\"";
    std::cout << "  Configuring with coverage instrumentation...\n";
    if (zap::utils::run_command_in(conf, root) != 0)
        throw std::runtime_error("cmake configure failed.");
    if (zap::utils::run_command_in("cmake --build build", root) != 0)
        throw std::runtime_error("Build failed.");
    if (zap::utils::run_command_in("ctest --output-on-failure", root / "build") != 0)
        throw std::runtime_error("Tests failed.");
    std::cout << "\n  Generating coverage report...\n";
    int rc = 0;
    if (zap::utils::program_exists("gcovr")) {
        rc = zap::utils::run_command_in(
            "gcovr --root . --exclude tests/ --html-details coverage.html", root);
        if (rc == 0) std::cout << "  Report written to coverage.html\n";
    } else {
        rc = zap::utils::run_command_in(
            "lcov --capture --directory build/ --output-file coverage.info --quiet", root);
        if (rc == 0) {
            rc = zap::utils::run_command_in(
                "genhtml coverage.info --output-directory coverage/ --quiet", root);
            if (rc == 0) std::cout << "  Report written to coverage/index.html\n";
        }
    }
    if (rc != 0) throw std::runtime_error("Coverage report generation failed.");
}

// ---------------------------------------------------------------------------
// Commands: zap login / logout / yank
// ---------------------------------------------------------------------------

void cmd_login() {
    std::cout << "  zap login is a no-op for now.\n";
    std::cout << "  There is no central C++ package registry requiring authentication.\n";
    std::cout << "  For private vcpkg registries, configure credentials in your git config.\n";
}

void cmd_logout() {
    std::cout << "  zap logout is a no-op for now.\n";
    std::cout << "  For private vcpkg registry credentials, use 'git credential reject'.\n";
}

void cmd_yank(const std::string& version) {
    std::cout << "  zap yank is not yet implemented.\n";
    std::cout << "  Version to yank: " << version << "\n";
    std::cout << "  To remove a published vcpkg port, submit a PR to the vcpkg registry.\n";
}

// ---------------------------------------------------------------------------
// Command: zap config [<key> [<value>]]
// ---------------------------------------------------------------------------

void cmd_config(const std::string& key, const std::string& value) {
    auto config_dir = fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".")
                    / ".config" / "zap";
    auto config_file = config_dir / "config.toml";

    if (key.empty()) {
        if (!fs::exists(config_file)) {
            std::cout << "  No configuration found (" << config_file.string() << ")\n";
            return;
        }
        std::ifstream f(config_file);
        std::cout << f.rdbuf();
        return;
    }

    if (value.empty()) {
        if (!fs::exists(config_file)) { std::cout << "  (not set)\n"; return; }
        std::ifstream f(config_file);
        for (std::string line; std::getline(f, line);) {
            if (line.rfind(key + " ", 0) == 0 || line.rfind(key + "=", 0) == 0) {
                std::cout << line << "\n";
                return;
            }
        }
        std::cout << "  (not set)\n";
        return;
    }

    zap::utils::ensure_directory(config_dir);
    std::string content;
    bool updated = false;
    if (fs::exists(config_file)) {
        std::ifstream f(config_file);
        for (std::string line; std::getline(f, line);) {
            if (line.rfind(key + " ", 0) == 0 || line.rfind(key + "=", 0) == 0) {
                content += key + " = \"" + value + "\"\n";
                updated = true;
            } else {
                content += line + "\n";
            }
        }
    }
    if (!updated) content += key + " = \"" + value + "\"\n";
    zap::utils::write_file(config_file, content);
    std::cout << "  Set " << key << " = \"" << value << "\"\n";
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

    // ---- search ------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("search", "Search vcpkg for packages");
        auto* pkg = new std::string;
        sub->add_option("package", *pkg, "Search query")->required();
        sub->callback([pkg] { cmd_search(*pkg); });
    }

    // ---- info --------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("info", "Show details about a vcpkg package");
        auto* pkg = new std::string;
        sub->add_option("package", *pkg, "Package name")->required();
        sub->callback([pkg] { cmd_info(*pkg); });
    }
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
        auto* dry_run = new bool{false};
        sub->add_flag("--dry-run", *dry_run, "Simulate publish without uploading");
        sub->callback([dry_run] { cmd_publish(*dry_run); });
    }

    // ---- version -----------------------------------------------------------
    {
        auto* sub = app.add_subcommand("version", "Print zap version");
        sub->callback([] { cmd_version(); });
    }

    // ---- init --------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("init", "Initialize a zap project in the current directory");
        sub->callback([] { cmd_init(); });

    // ---- config ------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("config", "Get or set zap configuration values");
        auto* key = new std::string;
        auto* val = new std::string;
        sub->add_option("key", *key, "Config key (omit to list all)");
        sub->add_option("value", *val, "New value (omit to read)");
        sub->callback([key, val] { cmd_config(*key, *val); });
    }
    }

    // ---- fmt ---------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("fmt", "Format source files with clang-format");
        sub->callback([] { cmd_fmt(); });
    }

    // ---- lint --------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("lint", "Lint source files with clang-tidy");
        sub->callback([] { cmd_lint(); });
    }

    // ---- check -------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("check", "Run static analysis without building");
        sub->callback([] { cmd_check(); });
    }

    // ---- bench -------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("bench", "Build (release) and run benchmarks");
        sub->callback([] { cmd_bench(); });
    }

    // ---- coverage ----------------------------------------------------------
    {
        auto* sub = app.add_subcommand("coverage", "Generate test coverage report");
        sub->callback([] { cmd_coverage(); });
    }

    // ---- login -------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("login", "Authenticate with a package registry");
        sub->callback([] { cmd_login(); });
    }

    // ---- logout ------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("logout", "Remove stored registry credentials");
        sub->callback([] { cmd_logout(); });
    }

    // ---- yank --------------------------------------------------------------
    {
        auto* sub = app.add_subcommand("yank", "Remove a published version");
        auto* ver = new std::string;
        sub->add_option("version", *ver, "Version to yank")->required();
        sub->callback([ver] { cmd_yank(*ver); });
    }
}

} // namespace zap::cli
