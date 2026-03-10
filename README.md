# zap

A no-nonsense package manager and project scaffolder for C++. The idea is simple: you shouldn't have to write CMake just to start a project or add a library. `zap` handles all of that for you, wrapping vcpkg for dependencies and CMake for builds while keeping both completely out of your way.

Think of it like `cargo` for C++.

## Demo

```sh
zap new demo
cd demo
zap add fmt
zap build       # generates CMakeLists.txt -> cmake configure -> cmake build
zap run
```

That's the whole workflow. No touching CMake, no editing vcpkg manifests by hand.

## Getting started

You'll need CMake 3.20+, a C++20 compiler, and [vcpkg](https://vcpkg.io). Once those are in place, point `VCPKG_ROOT` at your vcpkg install and build zap itself:

```sh
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=$HOME/vcpkg

git clone https://github.com/mqverk/zap && cd zap
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
sudo cmake --install build
```

## What a project looks like

Running `zap new demo` gives you:

```
demo/
|-- zap.toml
|-- vcpkg.json          (auto-generated, don't edit by hand)
|-- CMakeLists.txt      (auto-generated, same story)
|-- src/
|   \-- main.cpp
|-- include/
|-- tests/
|   \-- test_main.cpp
\-- build/
```

`zap.toml` is the only file you actually need to care about:

```toml
[project]
name = "demo"
version = "0.1.0"
cpp_standard = "20"

[dependencies]
fmt = "*"
spdlog = "*"

[dev-dependencies]
catch2 = "*"
```

Every time you run `zap build` or `zap add`, the `CMakeLists.txt` and `vcpkg.json` are regenerated automatically to match whatever is in the manifest.

## Commands

```
zap new <name>          create a new project
zap add <pkg>           add a dependency
zap add <pkg> --dev     add a dev/test-only dependency
zap remove <pkg>        remove a dependency
zap build               build (debug by default)
zap build --release     build in release mode
zap run                 build if needed, then run
zap run -- <args>       pass arguments to the binary
zap install             install all deps from zap.toml
zap test                build and run tests via ctest
zap update              update all dependencies
zap clean               wipe the build directory
zap doctor              check your environment is set up correctly
zap version             print the current zap version
```

## How the CMake generation works

When you add `fmt` and `catch2 --dev`, for example, zap generates something like this:

```cmake
cmake_minimum_required(VERSION 3.20)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain")
endif()

project(demo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(fmt CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(demo ${SOURCES})
target_include_directories(demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(demo PRIVATE fmt::fmt)

enable_testing()
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/*.cpp")
if(TEST_SOURCES)
    find_package(Catch2 CONFIG REQUIRED)
    add_executable(demo_tests ${TEST_SOURCES})
    target_link_libraries(demo_tests PRIVATE Catch2::Catch2WithMain fmt::fmt)
    add_test(NAME all_tests COMMAND demo_tests)
endif()
```

zap knows the correct `find_package` name and CMake target for about 50 common vcpkg packages out of the box. For anything not in that list it falls back to a reasonable guess based on the package name.

## If something's not working

Run `zap doctor` -- it checks for cmake, a C++ compiler, git, ninja, and vcpkg, and tells you what's missing. Most problems come down to vcpkg not being installed or `VCPKG_ROOT` not being set.

## Contributing

The codebase is split into four layers: `cli/` dispatches commands, `core/` contains the actual logic (manifest parsing, cmake generation, vcpkg integration, project scaffolding), and `utils/` handles filesystem and process execution. Keep things in those boundaries and you'll be fine. C++20, no heavy dependencies.

PRs welcome.

## License

MIT
