#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

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

/// A single experience tuple for replay
struct Experience {
    std::vector<double> state;
    std::vector<double> action;
    double reward;
};

/// Circular buffer of experience tuples
class ExperienceBuffer {
public:
    explicit ExperienceBuffer(size_t capacity) : capacity_(capacity) {}

    void push(Experience exp) {
        std::lock_guard lock(mtx_);
        if (buffer_.size() >= capacity_)
            buffer_.pop_front();
        buffer_.push_back(std::move(exp));
    }

    /// Sample n random experiences. Returns fewer if buffer is smaller.
    std::vector<Experience> sample(size_t n, std::mt19937& rng) {
        std::lock_guard lock(mtx_);
        std::vector<Experience> batch;
        if (buffer_.empty()) return batch;

        n = std::min(n, buffer_.size());
        // Fisher-Yates-style partial shuffle via index sampling
        std::vector<size_t> indices(buffer_.size());
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
        for (size_t i = 0; i < n; ++i) {
            std::uniform_int_distribution<size_t> dist(i, indices.size() - 1);
            std::swap(indices[i], indices[dist(rng)]);
            batch.push_back(buffer_[indices[i]]);
        }
        return batch;
    }

    size_t size() const {
        std::lock_guard lock(mtx_);
        return buffer_.size();
    }

private:
    size_t capacity_;
    std::deque<Experience> buffer_;
    mutable std::mutex mtx_;
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
                          const SystemMetrics& metrics,
                          const MiningParams& prev_params,
                          const MiningParams& new_params) const;

    utils::AIConfig cfg_;
    int max_threads_;

    NeuralNet nn_;
    SystemMonitor monitor_;
    MiningParams current_;

    ApplyParamsFn apply_fn_;
    GetHashrateFn hr_fn_;

    // Experience replay buffer
    ExperienceBuffer replay_buffer_;

    // Moving average reward baseline (exponential, α=0.1)
    double reward_baseline_ = 0.0;
    bool baseline_initialized_ = false;
    static constexpr double baseline_alpha_ = 0.1;

    // Current exploration rate (decays over time)
    double current_exploration_rate_;

    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace aiminer::ai
