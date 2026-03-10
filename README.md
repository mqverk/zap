# zap
> "Modern C++ development without touching CMake."

`zap` is a CLI tool that makes starting and managing C++ projects as simple as
`cargo` for Rust or `npm` for JavaScript.  
It wraps **vcpkg** for dependency management and **CMake** for builds so you
never have to write a `CMakeLists.txt` by hand.

---

## Quick start

```sh
zap new hello       # create a new project
cd hello
zap add fmt         # add a dependency
zap build           # configure + build (no cmake knowledge required)
zap run             # run the compiled binary
```

That's it.

---

## Installation

### Prerequisites

| Tool | Minimum version | Purpose |
|------|----------------|---------|
| CMake | 3.20 | Build system |
| C++ compiler | GCC 11 / Clang 13 / MSVC 2022 | Compile C++20 |
| [vcpkg](https://vcpkg.io) | any recent | Dependency manager |
| git | any | Used by vcpkg |

### Build zap from source

```sh
# 1. Clone and set up vcpkg (if not already installed)
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=$HOME/vcpkg

# 2. Clone zap
git clone https://github.com/mqverk/zap
cd zap

# 3. Build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build

# 4. Install (optional)
sudo cmake --install build
```

---

## Commands

| Command | Description |
|---------|-------------|
| `zap new <name>` | Create a new C++ project |
| `zap add <pkg>` | Add a runtime dependency |
| `zap add <pkg> --dev` | Add a dev/test dependency |
| `zap remove <pkg>` | Remove a dependency |
| `zap build` | Build the project (Debug) |
| `zap build --release` | Build in Release mode |
| `zap run` | Build (if needed) and run |
| `zap run -- <args>` | Run with arguments |
| `zap install` | Install all dependencies from manifest |
| `zap test` | Build and run tests |
| `zap update` | Update all dependencies |
| `zap update <pkg>` | Update a specific package |
| `zap clean` | Remove the build directory |
| `zap doctor` | Check your development environment |
| `zap version` | Print zap version |
| `zap publish` | Publish the project (experimental) |

---

## Manifest — `zap.toml`

```toml
[project]
name = "hello"
version = "0.1.0"
cpp_standard = "20"
description = "My awesome C++ project"

[dependencies]
fmt = "*"
spdlog = "*"

[dev-dependencies]
catch2 = "*"
```

---

## Project layout

```
hello/
├── zap.toml          # project manifest (managed by zap)
├── vcpkg.json        # vcpkg dependency manifest (auto-generated)
├── CMakeLists.txt    # cmake config       (auto-generated)
├── src/
│   └── main.cpp
├── include/          # public headers
├── tests/
│   └── test_main.cpp
└── build/            # cmake output (git-ignored)
```

---

## Generated `CMakeLists.txt`

When you run `zap build` or `zap add`, zap regenerates your `CMakeLists.txt`
automatically.  Example output for a project using `fmt` and `catch2`:

```cmake
cmake_minimum_required(VERSION 3.20)

# vcpkg integration (must appear before project())
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain")
endif()

project(hello VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(fmt CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(hello ${SOURCES})

target_include_directories(hello PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(hello PRIVATE fmt::fmt)

# Tests
enable_testing()
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/*.cpp")
if(TEST_SOURCES)
    find_package(Catch2 CONFIG REQUIRED)
    add_executable(hello_tests ${TEST_SOURCES})
    target_link_libraries(hello_tests PRIVATE Catch2::Catch2WithMain fmt::fmt)
    add_test(NAME all_tests COMMAND hello_tests)
endif()
```

---

## Architecture

```
zap/
├── CMakeLists.txt          # build zap itself
├── vcpkg.json              # zap's own dependencies
├── include/zap/
│   └── version.hpp         # compile-time version constants
└── src/
    ├── main.cpp             # entry point, CLI11 app setup
    ├── cli/
    │   ├── commands.hpp     # register_commands() declaration
    │   └── commands.cpp     # all CLI command implementations
    ├── core/
    │   ├── manifest.hpp/cpp        # parse / write zap.toml
    │   ├── cmake_generator.hpp/cpp # generate CMakeLists.txt
    │   ├── vcpkg_manager.hpp/cpp   # vcpkg discovery & install
    │   └── project.hpp/cpp         # project scaffolding
    └── utils/
        ├── fs.hpp/cpp       # filesystem helpers
        └── process.hpp/cpp  # cross-platform subprocess execution
```

---

## Contributing

PRs welcome!  Please keep the code C++20, zero-dependency outside of vcpkg/CMake,
and follow the existing module boundaries.

## License

MIT
