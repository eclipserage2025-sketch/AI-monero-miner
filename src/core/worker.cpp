#include "core/worker.h"

#include <cstring>
#include <thread>

#include "utils/logger.h"

namespace aiminer::core {

Worker::Worker(int id, crypto::RandomXHandler& rx,
               std::shared_ptr<network::Job> job)
    : id_(id), rx_(rx), job_(std::move(job)) {}

Worker::~Worker() { stop(); }

void Worker::start() {
    vm_ = rx_.create_vm();
    running_ = true;
    thread_ = std::thread(&Worker::run, this);
    LOG_DEBUG("Worker {} started", id_);
}

void Worker::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (vm_) {
        // Note: RandomX VM destruction handled externally in a full implementation
        vm_ = nullptr;
    }
}

void Worker::set_job(std::shared_ptr<network::Job> job) {
    std::lock_guard lock(job_mtx_);
    job_ = std::move(job);
}

void Worker::run() {
    uint8_t hash[32];
    std::vector<uint8_t> blob_buf(128);

    while (running_) {
        std::shared_ptr<network::Job> job;
        {
            std::lock_guard lock(job_mtx_);
            job = job_;
        }

        if (!job) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Apply intensity: occasionally skip batches
        if (intensity_ < 1.0) {
            // Skip this iteration with probability (1 - intensity_)
            // Simple approach: sleep proportional to (1 - intensity_)
            if (intensity_ < 0.99) {
                int pause = static_cast<int>((1.0 - intensity_) * 1000);
                std::this_thread::sleep_for(std::chrono::microseconds(pause));
            }
        }

        // Prepare blob with our nonce
        uint32_t nonce = job->next_nonce();
        std::memcpy(blob_buf.data(), job->blob.data(), job->blob_size);
        std::memcpy(blob_buf.data() + 39, &nonce, sizeof(nonce));  // nonce at offset 39

        // Hash
        crypto::RandomXHandler::calculate_hash(vm_, blob_buf.data(), job->blob_size, hash);
        hashes_.fetch_add(1, std::memory_order_relaxed);

        // Check against target
        uint64_t hash_val = 0;
        for (int i = 24; i < 32; ++i)
            hash_val = (hash_val << 8) | hash[i];

        if (hash_val < job->target_val) {
            LOG_INFO("Worker {}: share found! nonce={}", id_, nonce);
            if (submit_fn_) submit_fn_(*job, nonce, hash);
        }

        // Optional inter-batch sleep
        if (sleep_us_ > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us_));
    }
}

}  // namespace aiminer::core
