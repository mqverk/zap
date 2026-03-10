#include "process.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  include <direct.h>
#  define popen  _popen
#  define pclose _pclose
#  define getcwd _getcwd
#  define chdir  _chdir
#else
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace zap::utils {

namespace {

// Build a cross-platform "cd into dir, then run cmd" shell string.
std::string with_directory(const std::string& cmd, const std::filesystem::path& dir) {
#ifdef _WIN32
    // cmd /C "cd /d <dir> && <cmd>"
    return "cmd /C \"cd /d " + dir.string() + " && " + cmd + "\"";
#else
    // POSIX: wrap in a subshell so we don't disturb the parent working dir
    return "sh -c 'cd " + dir.string() + " && " + cmd + "'";
#endif
}

} // namespace

int run_command(const std::string& cmd) {
    return std::system(cmd.c_str()); // NOLINT(cert-env33-c)
}

int run_command_in(const std::string& cmd, const std::filesystem::path& directory) {
    return run_command(with_directory(cmd, directory));
}

ProcessResult run_command_capture(const std::string& cmd) {
    ProcessResult result;

    std::string full_cmd = cmd;
#ifndef _WIN32
    // Redirect stderr into stdout so we capture both streams.
    full_cmd += " 2>&1";
#endif

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to spawn process: " + cmd);
    }

    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.output += buffer.data();
    }

    result.exit_code = pclose(pipe);
#ifndef _WIN32
    // pclose returns the raw wait(2) status; extract the exit code.
    if (WIFEXITED(result.exit_code)) {        // NOLINT(hicpp-signed-bitwise)
        result.exit_code = WEXITSTATUS(result.exit_code);
    }
#endif

    return result;
}

bool program_exists(const std::string& name) {
#ifdef _WIN32
    const std::string check = "where " + name + " >NUL 2>&1";
#else
    const std::string check = "command -v " + name + " >/dev/null 2>&1";
#endif
    return std::system(check.c_str()) == 0; // NOLINT(cert-env33-c)
}

} // namespace zap::utils
