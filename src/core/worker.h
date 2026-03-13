#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "crypto/randomx_handler.h"
#include "network/job.h"

struct randomx_vm;

namespace aiminer::core {

class Worker {
public:
    Worker(int id, crypto::RandomXHandler& rx,
           std::shared_ptr<network::Job> job);
    ~Worker();

    void start();
    void stop();
    void set_job(std::shared_ptr<network::Job> job);
    void set_intensity(double intensity) { intensity_ = intensity; }
    void set_sleep_us(int us) { sleep_us_ = us; }

    uint64_t hashes() const { return hashes_.load(std::memory_order_relaxed); }
    void reset_hashes() { hashes_.store(0, std::memory_order_relaxed); }

    using SubmitFn = std::function<void(const network::Job&, uint32_t, const uint8_t*)>;
    void set_submit_fn(SubmitFn fn) { submit_fn_ = std::move(fn); }

private:
    void run();

    int id_;
    crypto::RandomXHandler& rx_;
    randomx_vm* vm_ = nullptr;
    std::shared_ptr<network::Job> job_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> hashes_{0};
    double intensity_ = 1.0;
    int sleep_us_ = 0;
    std::thread thread_;
    SubmitFn submit_fn_;
    std::mutex job_mtx_;
};

}  // namespace aiminer::core
