#pragma once

#include <filesystem>
#include <string>

namespace zap::utils {

// Result of running an external process.
struct ProcessResult {
    int         exit_code{0};
    std::string output;   // Combined stdout (and stderr when redirected)
};

// Run a shell command, printing its output to the terminal in real time.
// Returns the exit code.
int run_command(const std::string& cmd);

// Run a shell command inside a specific directory.
// The working directory is restored after the call.
// Returns the exit code.
int run_command_in(const std::string& cmd, const std::filesystem::path& directory);

// Run a shell command and capture its stdout.
// Returns a ProcessResult with the exit code and captured output.
ProcessResult run_command_capture(const std::string& cmd);

// Check whether a program is available on PATH.
bool program_exists(const std::string& name);

} // namespace zap::utils
