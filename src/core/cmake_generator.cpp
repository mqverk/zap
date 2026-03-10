#include "cmake_generator.hpp"

#include "utils/fs.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

namespace zap::core {

// ---------------------------------------------------------------------------
// Package → CMake knowledge base
//
// Maps a vcpkg package name to the CMake find_package() name, the imported
// target(s) to link against, and whether CONFIG mode should be used.
// For packages not in this table a reasonable default is synthesised.
// ---------------------------------------------------------------------------

namespace {

struct PkgCMake {
    std::string find_name;   // find_package(<find_name> ...)
    std::string targets;     // space-separated list of CMake targets
    bool        config;      // true → CONFIG REQUIRED,  false → REQUIRED
    std::string components;  // optional COMPONENTS list (e.g., for Boost)
};

// Test-framework package names (linked to test binary only, not the main exe).
const std::set<std::string> TEST_PACKAGES = {
    "catch2", "gtest", "doctest", "benchmark"
};

// clang-format off
const std::unordered_map<std::string, PkgCMake> KNOWN_PACKAGES = {
    // Formatting / logging
    {"fmt",             {"fmt",          "fmt::fmt",                              true,  ""}},
    {"spdlog",          {"spdlog",       "spdlog::spdlog",                        true,  ""}},

    // JSON
    {"nlohmann-json",   {"nlohmann_json","nlohmann_json::nlohmann_json",          true,  ""}},
    {"nlohmann_json",   {"nlohmann_json","nlohmann_json::nlohmann_json",          true,  ""}},
    {"jsoncpp",         {"jsoncpp",      "JsonCpp::JsonCpp",                      true,  ""}},
    {"rapidjson",       {"RapidJSON",    "RapidJSON::RapidJSON",                  false, ""}},

    // TOML / YAML
    {"tomlplusplus",    {"tomlplusplus", "tomlplusplus::tomlplusplus",            true,  ""}},
    {"yaml-cpp",        {"yaml-cpp",     "yaml-cpp::yaml-cpp",                    true,  ""}},

    // CLI
    {"cli11",           {"CLI11",        "CLI11::CLI11",                          true,  ""}},
    {"cxxopts",         {"cxxopts",      "cxxopts::cxxopts",                      true,  ""}},
    {"lyra",            {"lyra",         "bfg::lyra",                             true,  ""}},

    // Networking
    {"curl",            {"CURL",         "CURL::libcurl",                         false, ""}},
    {"openssl",         {"OpenSSL",      "OpenSSL::SSL OpenSSL::Crypto",          false, ""}},
    {"asio",            {"asio",         "asio::asio",                            true,  ""}},
    {"grpc",            {"gRPC",         "gRPC::grpc++",                          true,  ""}},
    {"protobuf",        {"protobuf",     "protobuf::libprotobuf",                 true,  ""}},
    {"zeromq",          {"ZeroMQ",       "libzmq",                                false, ""}},
    {"cppzmq",          {"cppzmq",       "cppzmq",                                true,  ""}},

    // Compression
    {"zlib",            {"ZLIB",         "ZLIB::ZLIB",                            false, ""}},
    {"zstd",            {"zstd",         "zstd::libzstd_shared",                  true,  ""}},
    {"lz4",             {"lz4",          "lz4::lz4",                              true,  ""}},
    {"bzip2",           {"BZip2",        "BZip2::BZip2",                          false, ""}},

    // Databases
    {"sqlite3",         {"SQLite3",      "SQLite::SQLite3",                       false, ""}},
    {"leveldb",         {"leveldb",      "leveldb::leveldb",                      true,  ""}},
    {"rocksdb",         {"RocksDB",      "RocksDB::rocksdb",                      true,  ""}},
    {"redis-plus-plus", {"redis++",      "redis++::redis++",                      true,  ""}},

    // Maths / linear algebra
    {"eigen3",          {"Eigen3",       "Eigen3::Eigen",                         false, ""}},
    {"glm",             {"glm",          "glm::glm",                              true,  ""}},

    // Graphics / multimedia
    {"glfw3",           {"glfw3",        "glfw",                                  true,  ""}},
    {"vulkan",          {"Vulkan",       "Vulkan::Vulkan",                        false, ""}},
    {"imgui",           {"imgui",        "imgui::imgui",                          true,  ""}},
    {"sdl2",            {"SDL2",         "SDL2::SDL2",                            false, ""}},
    {"sfml",            {"SFML",         "sfml-graphics sfml-window sfml-system", false, ""}},

    // Testing
    {"catch2",          {"Catch2",       "Catch2::Catch2WithMain",                true,  ""}},
    {"gtest",           {"GTest",        "GTest::gtest_main",                     false, ""}},
    {"doctest",         {"doctest",      "doctest::doctest",                      true,  ""}},
    {"benchmark",       {"benchmark",    "benchmark::benchmark",                  true,  ""}},

    // Regex / parsing
    {"re2",             {"re2",          "re2::re2",                              true,  ""}},
    {"pcre2",           {"PCRE2",        "PCRE2::8BIT",                           false, ""}},

    // Cryptography
    {"libsodium",       {"unofficial-sodium", "unofficial-sodium::sodium",        true,  ""}},
    {"mbedtls",         {"MbedTLS",      "MbedTLS::mbedtls",                      true,  ""}},

    // Utility / misc
    {"boost-filesystem",{"Boost",        "Boost::filesystem",                     false, "filesystem"}},
    {"boost-asio",      {"Boost",        "Boost::asio",                           false, "asio"}},
    {"boost-regex",     {"Boost",        "Boost::regex",                          false, "regex"}},
    {"abseil",          {"absl",         "absl::base absl::strings",              true,  ""}},
    {"tinyxml2",        {"tinyxml2",     "tinyxml2::tinyxml2",                    true,  ""}},
    {"pugixml",         {"pugixml",      "pugixml::pugixml",                      true,  ""}},
    {"libuv",           {"libuv",        "libuv::libuv",                          true,  ""}},
    {"date",            {"date",         "date::date",                            true,  ""}},
    {"magic-enum",      {"magic_enum",   "magic_enum::magic_enum",                true,  ""}},
    {"range-v3",        {"range-v3",     "range-v3::range-v3",                    true,  ""}},
    {"expected-lite",   {"expected-lite","nonstd::expected-lite",                 true,  ""}},
    {"ms-gsl",          {"Microsoft.GSL","Microsoft.GSL::GSL",                   true,  ""}},
};
// clang-format on

// Guess a find_package / target pair for a package not in the known table.
// Heuristic: strip hyphens→underscores, capitalise, form PkgName::PkgName.
PkgCMake guess_package(const std::string& vcpkg_name) {
    std::string canonical = vcpkg_name;
    std::replace(canonical.begin(), canonical.end(), '-', '_');

    std::string capitalized = canonical;
    if (!capitalized.empty()) {
        capitalized[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(capitalized[0])));
    }
    return PkgCMake{capitalized, capitalized + "::" + capitalized, true, ""};
}

const PkgCMake& lookup_package(const std::string& name) {
    static thread_local std::unordered_map<std::string, PkgCMake> cache;
    auto it = KNOWN_PACKAGES.find(name);
    if (it != KNOWN_PACKAGES.end()) return it->second;
    auto [ins, ok] = cache.emplace(name, guess_package(name));
    (void)ok;
    return ins->second;
}

// Build the find_package() call string for one package.
std::string make_find_package(const PkgCMake& pkg) {
    std::ostringstream ss;
    ss << "find_package(" << pkg.find_name;
    if (!pkg.components.empty()) ss << " COMPONENTS " << pkg.components;
    if (pkg.config) ss << " CONFIG";
    ss << " REQUIRED)";
    return ss.str();
}

// Append the space-separated targets of `info` as individual lines.
void emit_targets(std::ostringstream& ss, const PkgCMake& info, const std::string& indent) {
    std::istringstream ts(info.targets);
    std::string tok;
    while (ts >> tok) ss << indent << tok << "\n";
}

// Resolved dependency sets (Boost is deduplicated/merged).
struct BoostEntry {
    std::vector<std::string> comps;
    std::vector<std::string> targets;
};

struct ResolvedDeps {
    std::unordered_map<std::string, BoostEntry>      boost;      // keyed by vcpkg name
    std::vector<std::pair<std::string, PkgCMake>>    packages;   // ordered, deduped
};

ResolvedDeps resolve_deps(const std::map<std::string, std::string>& deps) {
    ResolvedDeps r;
    for (auto& [name, ver] : deps) {
        const PkgCMake& info = lookup_package(name);
        if (info.find_name == "Boost") {
            auto& be = r.boost[name];
            if (!info.components.empty()) be.comps.push_back(info.components);
            be.targets.push_back(info.targets);
        } else {
            bool already = false;
            for (auto& [n, p] : r.packages)
                if (p.find_name == info.find_name) { already = true; break; }
            if (!already) r.packages.emplace_back(name, info);
        }
    }
    return r;
}

void emit_find_packages(std::ostringstream& ss, const ResolvedDeps& r) {
    if (!r.boost.empty()) {
        std::vector<std::string> all_comps;
        for (auto& [k, be] : r.boost)
            for (auto& c : be.comps) all_comps.push_back(c);
        ss << "find_package(Boost REQUIRED COMPONENTS";
        for (auto& c : all_comps) ss << " " << c;
        ss << ")\n";
    }
    for (auto& [n, info] : r.packages)
        ss << make_find_package(info) << "\n";
}

void emit_link_targets(std::ostringstream& ss, const ResolvedDeps& r, const std::string& indent) {
    if (!r.boost.empty()) {
        for (auto& [k, be] : r.boost)
            for (auto& t : be.targets) ss << indent << t << "\n";
    }
    for (auto& [n, info] : r.packages)
        emit_targets(ss, info, indent);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string generate_cmakelists(
    const Manifest& manifest,
    const std::optional<std::filesystem::path>& vcpkg_toolchain)
{
    const auto& p = manifest.project;
    std::ostringstream ss;

    ss << "cmake_minimum_required(VERSION 3.20)\n\n";

    // vcpkg toolchain – must be set BEFORE project().
    ss << "# ---------------------------------------------------------------\n";
    ss << "# vcpkg integration (must appear before project())\n";
    ss << "# ---------------------------------------------------------------\n";
    if (vcpkg_toolchain) {
        ss << "if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)\n";
        ss << "    set(CMAKE_TOOLCHAIN_FILE \""
           << vcpkg_toolchain->string()
           << "\" CACHE STRING \"vcpkg toolchain\")\n";
        ss << "endif()\n\n";
    } else {
        ss << "if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)\n";
        ss << "    set(CMAKE_TOOLCHAIN_FILE\n";
        ss << "        \"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\"\n";
        ss << "        CACHE STRING \"vcpkg toolchain\")\n";
        ss << "endif()\n\n";
    }

    ss << "project(" << p.name << " VERSION " << p.version << " LANGUAGES CXX)\n\n";

    ss << "set(CMAKE_CXX_STANDARD " << p.cpp_standard << ")\n";
    ss << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    ss << "set(CMAKE_CXX_EXTENSIONS OFF)\n";
    ss << "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n\n";

    // Split runtime and test-only dependencies.
    std::map<std::string, std::string> runtime_deps;
    std::map<std::string, std::string> test_deps;

    for (auto& [name, ver] : manifest.dependencies) {
        if (TEST_PACKAGES.count(name)) test_deps[name] = ver;
        else runtime_deps[name]  = ver;
    }
    for (auto& [name, ver] : manifest.dev_dependencies) {
        test_deps[name] = ver;
    }

    auto runtime = resolve_deps(runtime_deps);
    auto tests   = resolve_deps(test_deps);

    bool has_runtime = !runtime.packages.empty() || !runtime.boost.empty();
    bool has_tests   = !tests.packages.empty()   || !tests.boost.empty();

    // find_package calls
    if (has_runtime || has_tests) {
        ss << "# ---------------------------------------------------------------\n";
        ss << "# Dependencies\n";
        ss << "# ---------------------------------------------------------------\n";
        emit_find_packages(ss, runtime);
        if (has_tests) emit_find_packages(ss, tests);
        ss << "\n";
    }

    // Main executable
    ss << "# ---------------------------------------------------------------\n";
    ss << "# Executable\n";
    ss << "# ---------------------------------------------------------------\n";
    ss << "file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS \"src/*.cpp\")\n\n";
    ss << "add_executable(" << p.name << " ${SOURCES})\n\n";
    ss << "target_include_directories(" << p.name << " PRIVATE\n";
    ss << "    ${CMAKE_SOURCE_DIR}/include\n";
    ss << ")\n\n";

    if (has_runtime) {
        ss << "target_link_libraries(" << p.name << " PRIVATE\n";
        emit_link_targets(ss, runtime, "    ");
        ss << ")\n\n";
    }

    // Test binary
    if (has_tests) {
        ss << "# ---------------------------------------------------------------\n";
        ss << "# Tests\n";
        ss << "# ---------------------------------------------------------------\n";
        ss << "enable_testing()\n";
        ss << "file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS \"tests/*.cpp\")\n";
        ss << "if(TEST_SOURCES)\n";
        ss << "    add_executable(" << p.name << "_tests ${TEST_SOURCES})\n";
        ss << "    target_include_directories(" << p.name << "_tests PRIVATE\n";
        ss << "        ${CMAKE_SOURCE_DIR}/include\n";
        ss << "        ${CMAKE_SOURCE_DIR}/src\n";
        ss << "    )\n";
        ss << "    target_link_libraries(" << p.name << "_tests PRIVATE\n";
        // All test deps (includes the test framework itself via emit_link_targets).
        emit_link_targets(ss, tests,   "        ");
        // Also share runtime deps.
        emit_link_targets(ss, runtime, "        ");
        ss << "    )\n";
        ss << "    add_test(NAME all_tests COMMAND " << p.name << "_tests)\n";
        ss << "endif()\n";
    }

    return ss.str();
}

void write_cmakelists(
    const Manifest& manifest,
    const std::filesystem::path& project_root,
    const std::optional<std::filesystem::path>& vcpkg_toolchain)
{
    zap::utils::write_file(
        project_root / "CMakeLists.txt",
        generate_cmakelists(manifest, vcpkg_toolchain));
}

} // namespace zap::core
