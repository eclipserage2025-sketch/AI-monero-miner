#include "utils/config.h"

#include <fstream>
#include <stdexcept>
#include <thread>

namespace aiminer::utils {

Config& Config::instance() {
    static Config inst;
    return inst;
}

static LogLevel parse_log_level(const std::string& s) {
    if (s == "trace") return LogLevel::TRACE;
    if (s == "debug") return LogLevel::DEBUG;
    if (s == "info")  return LogLevel::INFO;
    if (s == "warn")  return LogLevel::WARN;
    if (s == "error") return LogLevel::ERR;
    return LogLevel::INFO;
}

void Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open config file: " + path);

    nlohmann::json j;
    f >> j;

    // Pool
    if (j.contains("pool")) {
        auto& p = j["pool"];
        pool_.url       = p.value("url", pool_.url);
        pool_.user      = p.value("user", pool_.user);
        pool_.pass      = p.value("pass", pool_.pass);
        pool_.keepalive = p.value("keepalive", pool_.keepalive);
        pool_.tls       = p.value("tls", pool_.tls);
    }

    // Mining
    if (j.contains("mining")) {
        auto& m = j["mining"];
        mining_.threads    = m.value("threads", mining_.threads);
        mining_.priority   = m.value("priority", mining_.priority);
        mining_.huge_pages = m.value("huge_pages", mining_.huge_pages);
        if (m.contains("cpu_affinity"))
            mining_.cpu_affinity = m["cpu_affinity"].get<std::vector<int>>();
    }
    if (mining_.threads <= 0)
        mining_.threads = static_cast<int>(std::thread::hardware_concurrency());

    // AI
    if (j.contains("ai")) {
        auto& a = j["ai"];
        ai_.enabled                   = a.value("enabled", ai_.enabled);
        ai_.optimization_interval_sec = a.value("optimization_interval_sec", ai_.optimization_interval_sec);
        ai_.learning_rate             = a.value("learning_rate", ai_.learning_rate);
        ai_.exploration_rate          = a.value("exploration_rate", ai_.exploration_rate);
        ai_.power_aware               = a.value("power_aware", ai_.power_aware);
        ai_.target_temp_celsius       = a.value("target_temp_celsius", ai_.target_temp_celsius);
        ai_.history_window            = a.value("history_window", ai_.history_window);
        ai_.adam_beta1                = a.value("adam_beta1", ai_.adam_beta1);
        ai_.adam_beta2                = a.value("adam_beta2", ai_.adam_beta2);
        ai_.replay_batch_size         = a.value("replay_batch_size", ai_.replay_batch_size);
        ai_.exploration_decay         = a.value("exploration_decay", ai_.exploration_decay);
        ai_.min_exploration_rate      = a.value("min_exploration_rate", ai_.min_exploration_rate);
        if (a.contains("hidden_layers"))
            ai_.hidden_layers = a["hidden_layers"].get<std::vector<int>>();
    }

    // Logging
    if (j.contains("logging")) {
        auto& l = j["logging"];
        log_level_ = parse_log_level(l.value("level", "info"));
        log_file_  = l.value("file", std::string{});
    }
}

}  // namespace aiminer::utils
