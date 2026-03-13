#include <csignal>
#include <iostream>
#include <string>

#include "core/miner.h"
#include "utils/config.h"
#include "utils/logger.h"

static aiminer::core::Miner* g_miner = nullptr;

static void signal_handler(int sig) {
    if (g_miner) {
        LOG_INFO("Caught signal {}, shutting down…", sig);
        g_miner->stop();
    }
}

static void print_banner() {
    std::cout << R"(
     _    ___   __  __                           __  __ _
    / \  |_ _| |  \/  | ___  _ __   ___ _ __ __|  \/  (_)_ __   ___ _ __
   / _ \  | |  | |\/| |/ _ \| '_ \ / _ \ '__/ _ \ |\/| | | '_ \ / _ \ '__|
  / ___ \ | |  | |  | | (_) | | | |  __/ | | (_) | |  | | | | | |  __/ |
 /_/   \_\___| |_|  |_|\___/|_| |_|\___|_|  \___/|_|  |_|_|_| |_|\___|_|
                   v0.1.0  —  Neural-Net Optimised XMR Mining
)" << std::endl;
}

int main(int argc, char* argv[]) {
    print_banner();

    std::string config_path = "config/default_config.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ai-monero-miner [--config <path>]\n";
            return 0;
        }
    }

    try {
        auto& cfg = aiminer::utils::Config::instance();
        cfg.load(config_path);

        aiminer::utils::Logger::init(cfg.log_level(), cfg.log_file());
        LOG_INFO("Configuration loaded from {}", config_path);

        aiminer::core::Miner miner(cfg);
        g_miner = &miner;

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        miner.start();
        miner.wait();  // blocks until stopped

        LOG_INFO("Miner exited cleanly. Happy hashing! ⛏️");
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
