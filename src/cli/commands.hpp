#pragma once

#include <CLI/CLI.hpp>

namespace zap::cli {

// Register all subcommands on the root CLI11 application object.
// Each subcommand sets a callback that runs the corresponding handler.
void register_commands(CLI::App& app);

} // namespace zap::cli
