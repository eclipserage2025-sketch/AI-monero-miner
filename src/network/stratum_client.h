#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <winsock2.h>
#   include <ws2tcpip.h>
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
    using socket_t = int;
    constexpr socket_t INVALID_SOCK = -1;
#endif

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
    socket_t sock_ = INVALID_SOCK;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread recv_thread_;
    std::mutex send_mtx_;
    JobCallback job_cb_;
    int req_id_ = 1;
    std::string rpc_id_;

#ifdef _WIN32
    bool wsa_initialized_ = false;
#endif
};

}  // namespace aiminer::network
