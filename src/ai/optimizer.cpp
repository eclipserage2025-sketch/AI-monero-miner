#include "ai/optimizer.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <random>

#include "utils/logger.h"

// MSVC does not always define M_PI — use a portable constant
static constexpr double PI = 3.14159265358979323846;

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
      }(), cfg.learning_rate, cfg.adam_beta1, cfg.adam_beta2),
      replay_buffer_(static_cast<size_t>(cfg.history_window)),
      current_exploration_rate_(cfg.exploration_rate) {
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
    LOG_INFO("AI Optimizer started (interval={}s, ε={:.2f}, Adam β1={:.3f} β2={:.4f})",
             cfg_.optimization_interval_sec, current_exploration_rate_,
             cfg_.adam_beta1, cfg_.adam_beta2);
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
        MiningParams prev_params = current_;

        // ε-greedy: explore or exploit
        std::vector<double> action;
        bool exploring = uniform(rng) < current_exploration_rate_;

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

        // Compute reward
        double raw_reward = compute_reward(new_hashrate, hashrate, metrics,
                                           prev_params, params);

        // Update moving average baseline
        if (!baseline_initialized_) {
            reward_baseline_ = raw_reward;
            baseline_initialized_ = true;
        } else {
            reward_baseline_ = baseline_alpha_ * raw_reward
                             + (1.0 - baseline_alpha_) * reward_baseline_;
        }

        // Advantage = reward - baseline (reduces variance)
        double advantage = raw_reward - reward_baseline_;

        // Store experience in replay buffer
        replay_buffer_.push({state, action, advantage});

        // Target = action adjusted by advantage gradient
        std::vector<double> target = action;
        for (size_t i = 0; i < target.size(); ++i) {
            target[i] = std::clamp(action[i] + 0.1 * advantage, 0.0, 1.0);
        }

        double loss = nn_.train(state, target);

        // ── Mini-batch experience replay ────────────────────────────────
        double replay_loss = 0.0;
        int replay_count = 0;
        if (replay_buffer_.size() > static_cast<size_t>(cfg_.replay_batch_size)) {
            auto batch = replay_buffer_.sample(
                static_cast<size_t>(cfg_.replay_batch_size), rng);
            for (auto& exp : batch) {
                std::vector<double> replay_target = exp.action;
                for (size_t i = 0; i < replay_target.size(); ++i) {
                    replay_target[i] = std::clamp(
                        exp.action[i] + 0.1 * exp.reward, 0.0, 1.0);
                }
                replay_loss += nn_.train(exp.state, replay_target);
                ++replay_count;
            }
            if (replay_count > 0) replay_loss /= replay_count;
        }

        // Decay exploration rate
        current_exploration_rate_ = std::max(
            cfg_.min_exploration_rate,
            current_exploration_rate_ * cfg_.exploration_decay);

        LOG_INFO("AI tick: HR {:.1f}→{:.1f} H/s | threads={} intensity={:.2f} | "
                 "reward={:.3f} baseline={:.3f} loss={:.5f} replay_loss={:.5f} "
                 "ε={:.4f} buf={} {}",
                 hashrate, new_hashrate, params.thread_count, params.intensity,
                 raw_reward, reward_baseline_, loss, replay_loss,
                 current_exploration_rate_, replay_buffer_.size(),
                 exploring ? "[explore]" : "[exploit]");

        prev_hashrate = new_hashrate;
    }
}

// ── State vector ────────────────────────────────────────────────────────────
std::vector<double> Optimizer::build_state(const SystemMetrics& m, double hashrate) const {
    double time_of_day = []{
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);

        std::tm tm_buf{};
#if defined(_MSC_VER)
        localtime_s(&tm_buf, &tt);
#elif defined(__linux__) || defined(__APPLE__)
        localtime_r(&tt, &tm_buf);
#else
        std::tm* tmp = std::localtime(&tt);
        if (tmp) tm_buf = *tmp;
#endif
        return (tm_buf.tm_hour * 3600 + tm_buf.tm_min * 60 + tm_buf.tm_sec) / 86400.0;
    }();

    return {
        m.cpu_temp / 100.0,                                          // 0: temp
        m.cpu_load,                                                   // 1: load
        m.cpu_freq_norm,                                              // 2: freq ratio
        m.power_draw / 200.0,                                        // 3: power (normalise ~200W)
        std::min(hashrate / 10000.0, 1.0),                           // 4: hashrate
        static_cast<double>(current_.thread_count) / max_threads_,   // 5: thread ratio
        0.5,                                                          // 6: difficulty (placeholder)
        std::sin(2 * PI * time_of_day)                                // 7: time encoding
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

// ── Improved reward function ────────────────────────────────────────────────
double Optimizer::compute_reward(double hashrate, double prev_hashrate,
                                 const SystemMetrics& metrics,
                                 const MiningParams& prev_params,
                                 const MiningParams& new_params) const {
    // 1. Hashrate improvement (weight 0.5)
    double hr_delta = 0.0;
    if (prev_hashrate > 0) {
        hr_delta = (hashrate - prev_hashrate) / prev_hashrate;
    }
    double hr_reward = 0.5 * hr_delta;

    // 2. Thermal penalty with smooth sigmoid (not hard cutoff)
    double temp_penalty = 0.0;
    if (cfg_.power_aware) {
        // Sigmoid centred at target_temp, steepness = 0.2
        double x = (metrics.cpu_temp - cfg_.target_temp_celsius) * 0.2;
        double sigmoid = 1.0 / (1.0 + std::exp(-x));
        // sigmoid is ~0.5 at target, rises toward 1 above target
        temp_penalty = -0.3 * sigmoid;
    }

    // 3. Power efficiency bonus (H/s per Watt)
    double efficiency_bonus = 0.0;
    if (cfg_.power_aware && metrics.power_draw > 0) {
        double hpw = hashrate / metrics.power_draw;
        efficiency_bonus = 0.15 * std::tanh(hpw / 100.0);
    }

    // 4. Stability bonus (penalize large parameter swings)
    double stability_penalty = 0.0;
    {
        double thread_change = std::abs(
            static_cast<double>(new_params.thread_count - prev_params.thread_count))
            / std::max(1, max_threads_);
        double intensity_change = std::abs(new_params.intensity - prev_params.intensity);
        double sleep_change = std::abs(new_params.sleep_us - prev_params.sleep_us) / 1000.0;
        stability_penalty = -0.05 * (thread_change + intensity_change + sleep_change);
    }

    return hr_reward + temp_penalty + efficiency_bonus + stability_penalty;
}

}  // namespace aiminer::ai
