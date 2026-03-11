#include "project.hpp"

#include "cmake_generator.hpp"
#include "manifest.hpp"
#include "vcpkg_manager.hpp"
#include "utils/fs.hpp"

#include <iostream>
#include <stdexcept>

namespace zap::core {

namespace {

// ---------------------------------------------------------------------------
// Embedded templates
// ---------------------------------------------------------------------------

// Default main.cpp written when a new executable project is created.
constexpr const char* MAIN_CPP_TEMPLATE = R"cpp(#include <iostream>

int main() {
    std::cout << "Hello from zap!\n";
    return 0;
}
)cpp";

// Default lib.hpp/lib.cpp written when --lib is used.
constexpr const char* LIB_HPP_TEMPLATE = R"cpp(#pragma once

namespace {NAME} {{

void hello();

}} // namespace {NAME}
)cpp";

constexpr const char* LIB_CPP_TEMPLATE = R"cpp(#include "{NAME}/{NAME}.hpp"
#include <iostream>

namespace {NAME} {{

void hello() {{
    std::cout << "Hello from {NAME}!\n";
}}

}} // namespace {NAME}
)cpp";

// Minimal test stub – requires Catch2 (add with: zap add catch2 --dev)
constexpr const char* TEST_STUB_TEMPLATE = R"cpp(// Add your tests here.
// Run with: zap test
//
// To use Catch2 add it as a dev dependency first:
//   zap add catch2 --dev
//
// Then write tests like:
//
//   #define CATCH_CONFIG_MAIN
//   #include <catch2/catch_test_macros.hpp>
//
//   TEST_CASE("example") {
//       REQUIRE(1 + 1 == 2);
//   }
)cpp";

// .gitignore for new projects.
constexpr const char* GITIGNORE_TEMPLATE = R"(# Build output
build/

# Editor files
.vscode/
.idea/
*.user

# Compile commands (symlinked or generated)
compile_commands.json
)";

// Initial README for new projects.
const std::string make_readme(const std::string& name) {
    return
        "# " + name + "\n\n"
        "A C++ project managed by [zap](https://github.com/mqverk/zap).\n\n"
        "## Build\n\n"
        "```sh\nzap build\n```\n\n"
        "## Run\n\n"
        "```sh\nzap run\n```\n\n"
        "## Add a dependency\n\n"
        "```sh\nzap add <package>\n```\n";
}

} // namespace

// ---------------------------------------------------------------------------
// Public implementation
// ---------------------------------------------------------------------------

void create_new_project(const NewProjectOptions& opts,
                        const std::filesystem::path& parent_dir)
{
    // For init_in_place, parent_dir IS already the target directory.
    // Otherwise create a new sub-directory named opts.name.
    const auto dir = opts.init_in_place ? parent_dir : parent_dir / opts.name;

    if (!opts.init_in_place && std::filesystem::exists(dir)) {
        throw std::runtime_error(
            "error: directory already exists: " + dir.string());
    }

    std::cout << "  Creating  " << opts.name << "\n";

    // ---- Directory structure -----------------------------------------------
    zap::utils::ensure_directory(dir / "src");
    zap::utils::ensure_directory(dir / "include" / opts.name);
    if (opts.create_tests) {
        zap::utils::ensure_directory(dir / "tests");
    }
    zap::utils::ensure_directory(dir / "build");

    // ---- zap.toml ----------------------------------------------------------
    Manifest manifest;
    manifest.path               = dir / "zap.toml";
    manifest.project.name       = opts.name;
    manifest.project.version    = opts.version;
    manifest.project.cpp_standard = opts.cpp_standard;
    manifest.project.description  = opts.description;
    manifest.save();

    std::cout << "  Writing   zap.toml\n";

    // ---- vcpkg.json --------------------------------------------------------
    write_vcpkg_manifest(opts.name, opts.version, {}, dir);
    std::cout << "  Writing   vcpkg.json\n";

    // ---- CMakeLists.txt ----------------------------------------------------
    write_cmakelists(manifest, dir);
    std::cout << "  Writing   CMakeLists.txt\n";

    // ---- Source files ------------------------------------------------------
    const bool make_lib = opts.is_library ||
                         (!opts.template_name.empty() && opts.template_name == "lib");
    if (make_lib) {
        auto replace_name = [&](std::string s) -> std::string {
            for (std::size_t pos = 0;
                 (pos = s.find("{NAME}", pos)) != std::string::npos; ) {
                s.replace(pos, 6, opts.name);
                pos += opts.name.size();
            }
            return s;
        };
        zap::utils::write_file(dir / "include" / opts.name / (opts.name + ".hpp"),
                               replace_name(LIB_HPP_TEMPLATE));
        zap::utils::write_file(dir / "src" / (opts.name + ".cpp"),
                               replace_name(LIB_CPP_TEMPLATE));
        std::cout << "  Writing   include/" << opts.name << "/" << opts.name << ".hpp\n";
        std::cout << "  Writing   src/" << opts.name << ".cpp\n";
    } else {
        zap::utils::write_file(dir / "src" / "main.cpp", MAIN_CPP_TEMPLATE);
        std::cout << "  Writing   src/main.cpp\n";
    }

    // ---- tests/  stub ------------------------------------------------------
    if (opts.create_tests) {
        zap::utils::write_file(dir / "tests" / "test_main.cpp", TEST_STUB_TEMPLATE);
        std::cout << "  Writing   tests/test_main.cpp\n";
    }

    // ---- .gitignore --------------------------------------------------------
    zap::utils::write_file(dir / ".gitignore", GITIGNORE_TEMPLATE);
    std::cout << "  Writing   .gitignore\n";

    // ---- README.md ---------------------------------------------------------
    zap::utils::write_file(dir / "README.md", make_readme(opts.name));
    std::cout << "  Writing   README.md\n";

    std::cout << "\n";
    std::cout << "  Project '" << opts.name << "' created successfully!\n\n";
    if (!opts.init_in_place) {
        std::cout << "  Next steps:\n";
        std::cout << "    cd " << opts.name << "\n";
    }
    std::cout << "    zap build\n";
    std::cout << "    zap run\n";
}

} // namespace zap::core
