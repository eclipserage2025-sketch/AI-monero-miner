#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "ai/neural_net.h"
#include "ai/system_monitor.h"
#include "utils/config.h"

namespace aiminer::ai {

/// Parameters the optimizer controls
struct MiningParams {
    int    thread_count = 4;
    double intensity    = 1.0;   // 0-1 scaling
    int    sleep_us     = 0;     // inter-batch pause
};

/// Callback the miner registers to apply new params
using ApplyParamsFn = std::function<void(const MiningParams&)>;
/// Callback to query the current hashrate
using GetHashrateFn = std::function<double()>;

class Optimizer {
public:
    Optimizer(const utils::AIConfig& cfg, int max_threads);
    ~Optimizer();

    void start(ApplyParamsFn apply_fn, GetHashrateFn hr_fn);
    void stop();

    MiningParams current_params() const { return current_; }

private:
    void loop();

    // Build state vector from metrics + mining state
    std::vector<double> build_state(const SystemMetrics& m, double hashrate) const;
    // Decode NN output → MiningParams
    MiningParams decode_action(const std::vector<double>& action) const;
    // Compute reward signal
    double compute_reward(double hashrate, double prev_hashrate,
                          const SystemMetrics& metrics) const;

    utils::AIConfig cfg_;
    int max_threads_;

    NeuralNet nn_;
    SystemMonitor monitor_;
    MiningParams current_;

    ApplyParamsFn apply_fn_;
    GetHashrateFn hr_fn_;

    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace aiminer::ai
