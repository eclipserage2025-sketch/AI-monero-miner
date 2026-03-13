#include "ai/optimizer.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "utils/logger.h"

namespace aiminer::ai {

Optimizer::Optimizer(const utils::AIConfig& cfg, int max_threads)
    : cfg_(cfg),
      max_threads_(max_threads),
      nn_([&]() {
          // Build layer sizes: input(8) → hidden → output(3)
          std::vector<int> layers;
          layers.push_back(8);  // state vector size
          for (int h : cfg.hidden_layers) layers.push_back(h);
          layers.push_back(3);  // action vector size
          return layers;
      }(), cfg.learning_rate) {
    current_.thread_count = max_threads;
    current_.intensity    = 1.0;
    current_.sleep_us     = 0;

    // Try to load saved weights
    nn_.load("ai_weights.bin");
}

Optimizer::~Optimizer() { stop(); }

void Optimizer::start(ApplyParamsFn apply_fn, GetHashrateFn hr_fn) {
    apply_fn_ = std::move(apply_fn);
    hr_fn_    = std::move(hr_fn);
    running_  = true;
    thread_   = std::thread(&Optimizer::loop, this);
    LOG_INFO("AI Optimizer started (interval={}s, ε={:.2f})",
             cfg_.optimization_interval_sec, cfg_.exploration_rate);
}

void Optimizer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    nn_.save("ai_weights.bin");
}

// ── Main optimisation loop ──────────────────────────────────────────────────
void Optimizer::loop() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<> uniform(0.0, 1.0);

    double prev_hashrate = 0;

    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::seconds(cfg_.optimization_interval_sec));
        if (!running_) break;

        double hashrate = hr_fn_();
        auto metrics = monitor_.sample();

        auto state = build_state(metrics, hashrate);

        // ε-greedy: explore or exploit
        std::vector<double> action;
        bool exploring = uniform(rng) < cfg_.exploration_rate;

        if (exploring) {
            // Random perturbation
            action.resize(3);
            for (auto& a : action) a = uniform(rng);
            LOG_DEBUG("AI: exploring (random action)");
        } else {
            action = nn_.forward(state);
            LOG_DEBUG("AI: exploiting (NN action: [{:.3f}, {:.3f}, {:.3f}])",
                      action[0], action[1], action[2]);
        }

        auto params = decode_action(action);

        // Apply new parameters
        apply_fn_(params);
        current_ = params;

        // Wait a short period to measure impact
        std::this_thread::sleep_for(std::chrono::seconds(5));
        double new_hashrate = hr_fn_();

        // Compute reward and train
        double reward = compute_reward(new_hashrate, hashrate, metrics);

        // Target = action adjusted by reward gradient
        std::vector<double> target = action;
        for (size_t i = 0; i < target.size(); ++i) {
            target[i] = std::clamp(action[i] + 0.1 * reward, 0.0, 1.0);
        }

        double loss = nn_.train(state, target);

        LOG_INFO("AI tick: HR {:.1f}→{:.1f} H/s | threads={} intensity={:.2f} | "
                 "reward={:.3f} loss={:.5f} {}",
                 hashrate, new_hashrate, params.thread_count, params.intensity,
                 reward, loss, exploring ? "[explore]" : "[exploit]");

        prev_hashrate = new_hashrate;
    }
}

// ── State vector ────────────────────────────────────────────────────────────
std::vector<double> Optimizer::build_state(const SystemMetrics& m, double hashrate) const {
    double time_of_day = []{
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        auto* tm = std::localtime(&tt);
        return (tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec) / 86400.0;
    }();

    return {
        m.cpu_temp / 100.0,                                          // 0: temp
        m.cpu_load,                                                   // 1: load
        m.cpu_freq_norm,                                              // 2: freq ratio
        m.power_draw / 200.0,                                        // 3: power (normalise ~200W)
        std::min(hashrate / 10000.0, 1.0),                           // 4: hashrate
        static_cast<double>(current_.thread_count) / max_threads_,   // 5: thread ratio
        0.5,                                                          // 6: difficulty (placeholder)
        std::sin(2 * M_PI * time_of_day)                              // 7: time encoding
    };
}

// ── Decode action → params ──────────────────────────────────────────────────
MiningParams Optimizer::decode_action(const std::vector<double>& action) const {
    MiningParams p;
    p.thread_count = std::clamp(
        static_cast<int>(std::round(action[0] * max_threads_)), 1, max_threads_);
    p.intensity = std::clamp(action[1], 0.1, 1.0);
    p.sleep_us  = static_cast<int>(action[2] * 1000);  // 0–1000 µs
    return p;
}

// ── Reward function ─────────────────────────────────────────────────────────
double Optimizer::compute_reward(double hashrate, double prev_hashrate,
                                 const SystemMetrics& metrics) const {
    // Primary: hashrate improvement
    double hr_delta = (prev_hashrate > 0)
        ? (hashrate - prev_hashrate) / prev_hashrate
        : 0.0;

    // Penalty: thermal throttling
    double temp_penalty = 0;
    if (cfg_.power_aware && metrics.cpu_temp > cfg_.target_temp_celsius) {
        temp_penalty = -0.5 * (metrics.cpu_temp - cfg_.target_temp_celsius)
                            / cfg_.target_temp_celsius;
    }

    // Efficiency bonus: H/s per watt
    double efficiency_bonus = 0;
    if (cfg_.power_aware && metrics.power_draw > 0) {
        double hpw = hashrate / metrics.power_draw;
        efficiency_bonus = 0.1 * std::tanh(hpw / 100.0);
    }

    return hr_delta + temp_penalty + efficiency_bonus;
}

}  // namespace aiminer::ai
