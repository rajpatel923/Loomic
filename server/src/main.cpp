#include <iostream>
#include <stdexcept>
#include <string>

#include "LoomicServer/util/Config.hpp"
#include "LoomicServer/util/Logger.hpp"
#include "LoomicServer/core/Server.hpp"

int main(int argc, char* argv[])
{
    std::string config_path = "config/server.json";

    // Parse --config <path>
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--config") {
            config_path = argv[i + 1];
        }
    }

    try {
        auto cfg = Loomic::Config::from_file(config_path);
        Loomic::Config::from_env(cfg);

        Loomic::Logger::init(cfg);

        Loomic::Server server(cfg);
        server.run();

    } catch (const std::exception& ex) {
        std::cerr << "[fatal] " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
