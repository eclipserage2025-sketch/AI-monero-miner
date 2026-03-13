#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include "ai/optimizer.h"
#include "core/worker.h"
#include "crypto/randomx_handler.h"
#include "network/stratum_client.h"
#include "utils/config.h"

namespace aiminer::core {

class Miner {
public:
    explicit Miner(const utils::Config& cfg);
    ~Miner();

    void start();
    void stop();
    void wait();

    double hashrate() const;

private:
    void on_new_job(std::shared_ptr<network::Job> job);
    void apply_ai_params(const ai::MiningParams& params);
    void adjust_workers(int target_count);
    void hashrate_report_loop();

    const utils::Config& cfg_;
    crypto::RandomXHandler rx_;
    network::StratumClient stratum_;
    std::unique_ptr<ai::Optimizer> optimizer_;

    std::vector<std::unique_ptr<Worker>> workers_;
    std::mutex workers_mtx_;
    std::shared_ptr<network::Job> current_job_;

    std::atomic<bool> running_{false};
    std::thread report_thread_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point last_hash_time_;
    mutable uint64_t last_hash_count_ = 0;
};

}  // namespace aiminer::core
