#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "network/job.h"
#include "utils/config.h"

namespace aiminer::network {

class StratumClient {
public:
    using JobCallback = std::function<void(std::shared_ptr<Job>)>;

    explicit StratumClient(const utils::PoolConfig& cfg);
    ~StratumClient();

    void connect();
    void disconnect();
    bool is_connected() const { return connected_.load(); }

    void set_job_callback(JobCallback cb) { job_cb_ = std::move(cb); }

    // Submit a solved share
    void submit(const Job& job, uint32_t nonce, const uint8_t* result);

private:
    void recv_loop();
    void handle_message(const std::string& line);
    void send(const std::string& msg);
    void do_login();

    utils::PoolConfig cfg_;
    int sock_ = -1;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread recv_thread_;
    std::mutex send_mtx_;
    JobCallback job_cb_;
    int req_id_ = 1;
    std::string rpc_id_;
};

}  // namespace aiminer::network
