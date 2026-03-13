#include "core/miner.h"

#include <algorithm>
#include <numeric>
#include <thread>

#include "utils/logger.h"

namespace aiminer::core {

Miner::Miner(const utils::Config& cfg)
    : cfg_(cfg),
      stratum_(cfg.pool()) {
    if (cfg.ai().enabled) {
        optimizer_ = std::make_unique<ai::Optimizer>(cfg.ai(), cfg.mining().threads);
    }
}

Miner::~Miner() { stop(); }

// ── Start ───────────────────────────────────────────────────────────────────
void Miner::start() {
    LOG_INFO("Starting miner with {} threads", cfg_.mining().threads);

    // Connect to pool
    stratum_.set_job_callback([this](auto job) { on_new_job(std::move(job)); });
    stratum_.connect();

    running_ = true;

    // Start hashrate reporting
    last_hash_time_ = Clock::now();
    report_thread_ = std::thread(&Miner::hashrate_report_loop, this);

    // Start AI optimizer
    if (optimizer_) {
        optimizer_->start(
            [this](const ai::MiningParams& p) { apply_ai_params(p); },
            [this]() { return hashrate(); }
        );
    }
}

void Miner::stop() {
    running_ = false;
    if (optimizer_) optimizer_->stop();

    {
        std::lock_guard lock(workers_mtx_);
        for (auto& w : workers_) w->stop();
        workers_.clear();
    }

    stratum_.disconnect();
    if (report_thread_.joinable()) report_thread_.join();
}

void Miner::wait() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ── Hashrate ────────────────────────────────────────────────────────────────
double Miner::hashrate() const {
    std::lock_guard lock(const_cast<std::mutex&>(workers_mtx_));
    uint64_t total = 0;
    for (auto& w : workers_) total += w->hashes();

    auto now = Clock::now();
    double secs = std::chrono::duration<double>(now - last_hash_time_).count();
    if (secs < 0.1) return 0;

    double hr = (total - last_hash_count_) / secs;
    last_hash_count_ = total;
    const_cast<Clock::time_point&>(last_hash_time_) = now;
    return hr;
}

// ── New job from pool ───────────────────────────────────────────────────────
void Miner::on_new_job(std::shared_ptr<network::Job> job) {
    current_job_ = job;

    // Initialise RandomX if seed changed
    rx_.init(job->seed_hash, cfg_.mining().threads, cfg_.mining().huge_pages);

    std::lock_guard lock(workers_mtx_);

    if (workers_.empty()) {
        // First job → spawn workers
        int n = cfg_.mining().threads;
        for (int i = 0; i < n; ++i) {
            auto w = std::make_unique<Worker>(i, rx_, job);
            w->set_submit_fn([this](const network::Job& j, uint32_t nonce, const uint8_t* hash) {
                stratum_.submit(j, nonce, hash);
            });
            w->start();
            workers_.push_back(std::move(w));
        }
        LOG_INFO("Spawned {} worker threads", n);
    } else {
        // Update existing workers
        for (auto& w : workers_) w->set_job(job);
    }
}

// ── AI param application ────────────────────────────────────────────────────
void Miner::apply_ai_params(const ai::MiningParams& params) {
    LOG_DEBUG("Applying AI params: threads={} intensity={:.2f} sleep={}µs",
              params.thread_count, params.intensity, params.sleep_us);

    adjust_workers(params.thread_count);

    std::lock_guard lock(workers_mtx_);
    for (auto& w : workers_) {
        w->set_intensity(params.intensity);
        w->set_sleep_us(params.sleep_us);
    }
}

void Miner::adjust_workers(int target_count) {
    std::lock_guard lock(workers_mtx_);
    int current = static_cast<int>(workers_.size());

    if (target_count > current && current_job_) {
        for (int i = current; i < target_count; ++i) {
            auto w = std::make_unique<Worker>(i, rx_, current_job_);
            w->set_submit_fn([this](const network::Job& j, uint32_t nonce, const uint8_t* hash) {
                stratum_.submit(j, nonce, hash);
            });
            w->start();
            workers_.push_back(std::move(w));
        }
        LOG_INFO("AI: scaled up to {} workers", target_count);
    } else if (target_count < current) {
        for (int i = current - 1; i >= target_count; --i) {
            workers_[i]->stop();
            workers_.erase(workers_.begin() + i);
        }
        LOG_INFO("AI: scaled down to {} workers", target_count);
    }
}

// ── Hashrate reporter ───────────────────────────────────────────────────────
void Miner::hashrate_report_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!running_) break;
        double hr = hashrate();
        if (hr > 0) {
            std::string unit = "H/s";
            double display = hr;
            if (hr > 1e6)      { display = hr / 1e6; unit = "MH/s"; }
            else if (hr > 1e3) { display = hr / 1e3; unit = "KH/s"; }
            LOG_INFO("Hashrate: {:.2f} {}", display, unit);
        }
    }
}

}  // namespace aiminer::core
