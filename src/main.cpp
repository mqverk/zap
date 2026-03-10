#include "cli/commands.hpp"
#include "zap/version.hpp"

#include <CLI/CLI.hpp>
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    CLI::App app{
        std::string(zap::DESCRIPTION) + " v" + zap::VERSION,
        "zap"
    };

    // Show version with -V / --version on the root app as well.
    app.set_version_flag("-V,--version",
                         std::string("zap ") + zap::VERSION);

    // Register all subcommands.
    zap::cli::register_commands(app);

    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e) {
        // CLI11 prints its own error; just return the appropriate exit code.
        return app.exit(e);
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
