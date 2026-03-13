#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#else
#   include <csignal>
#endif

#include <iostream>
#include <string>

#include "core/miner.h"
#include "utils/config.h"
#include "utils/logger.h"

static aiminer::core::Miner* g_miner = nullptr;

#ifdef _WIN32
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_BREAK_EVENT:
            if (g_miner) {
                LOG_INFO("Caught console control event {}, shutting down…",
                         static_cast<int>(ctrl_type));
                g_miner->stop();
            }
            return TRUE;
        default:
            return FALSE;
    }
}
#else
static void signal_handler(int sig) {
    if (g_miner) {
        LOG_INFO("Caught signal {}, shutting down…", sig);
        g_miner->stop();
    }
}
#endif

static void print_banner() {
    std::cout << R"(
     _    ___   __  __                           __  __ _
    / \  |_ _| |  \/  | ___  _ __   ___ _ __ __|  \/  (_)_ __   ___ _ __
   / _ \  | |  | |\/| |/ _ \| '_ \ / _ \ '__/ _ \ |\/| | | '_ \ / _ \ '__|
  / ___ \ | |  | |  | | (_) | | | |  __/ | | (_) | |  | | | | | |  __/ |
 /_/   \_\___| |_|  |_|\___/|_| |_|\___|_|  \___/|_|  |_|_|_| |_|\___|_|
                   v0.2.0  —  Neural-Net Optimised XMR Mining
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

#ifdef _WIN32
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
#endif

        miner.start();
        miner.wait();  // blocks until stopped

        LOG_INFO("Miner exited cleanly. Happy hashing! ⛏️");
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
