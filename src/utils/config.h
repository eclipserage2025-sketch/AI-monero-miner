#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "utils/logger.h"

namespace aiminer::utils {

struct PoolConfig {
    std::string url;
    std::string user;
    std::string pass = "x";
    bool keepalive = true;
    bool tls = false;
};

struct MiningConfig {
    int threads = 0;               // 0 = auto-detect
    std::vector<int> cpu_affinity;
    int priority = 2;
    bool huge_pages = true;
};

struct AIConfig {
    bool enabled = true;
    int optimization_interval_sec = 30;
    double learning_rate = 0.001;
    std::vector<int> hidden_layers = {64, 32};
    double exploration_rate = 0.15;
    bool power_aware = true;
    double target_temp_celsius = 80.0;
    int history_window = 500;
};

class Config {
public:
    static Config& instance();

    void load(const std::string& path);

    // Accessors
    const PoolConfig&   pool()   const { return pool_; }
    const MiningConfig& mining() const { return mining_; }
    const AIConfig&     ai()     const { return ai_; }
    LogLevel            log_level() const { return log_level_; }
    const std::string&  log_file()  const { return log_file_; }

private:
    Config() = default;
    PoolConfig   pool_;
    MiningConfig mining_;
    AIConfig     ai_;
    LogLevel     log_level_ = LogLevel::INFO;
    std::string  log_file_;
};

}  // namespace aiminer::utils
