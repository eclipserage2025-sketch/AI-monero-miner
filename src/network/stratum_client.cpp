#include "network/stratum_client.h"

#ifndef _WIN32
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <sys/socket.h>
#   include <unistd.h>
#endif

#include <cstring>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "utils/logger.h"

using json = nlohmann::json;

namespace aiminer::network {

// ── Hex helpers ─────────────────────────────────────────────────────────────
static uint8_t hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void hex_to_bytes(const std::string& hex, uint8_t* out, size_t max) {
    size_t len = std::min(hex.size() / 2, max);
    for (size_t i = 0; i < len; ++i)
        out[i] = (hex_val(hex[2 * i]) << 4) | hex_val(hex[2 * i + 1]);
}

static std::string bytes_to_hex(const uint8_t* data, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result += hex_chars[data[i] >> 4];
        result += hex_chars[data[i] & 0x0F];
    }
    return result;
}

// ── Target parsing ──────────────────────────────────────────────────────────
static uint64_t target_to_uint64(const std::string& hex) {
    // Compact target: 8-char hex → 4 bytes → expand to 64-bit difficulty target
    uint64_t val = 0;
    size_t len = std::min<size_t>(hex.size(), 16);
    for (size_t i = 0; i < len; ++i) {
        val = (val << 4) | hex_val(hex[i]);
    }
    return val ? (0xFFFFFFFFFFFFFFFFULL / val) : 0;
}

// ── Constructor / Destructor ────────────────────────────────────────────────
StratumClient::StratumClient(const utils::PoolConfig& cfg) : cfg_(cfg) {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0) {
        wsa_initialized_ = true;
    } else {
        LOG_ERR("WSAStartup failed");
    }
#endif
}

StratumClient::~StratumClient() {
    disconnect();
#ifdef _WIN32
    if (wsa_initialized_) {
        WSACleanup();
        wsa_initialized_ = false;
    }
#endif
}

// ── Connect ─────────────────────────────────────────────────────────────────
void StratumClient::connect() {
    // Parse host:port from URL (strip protocol prefix)
    std::string addr = cfg_.url;
    auto pos = addr.find("://");
    if (pos != std::string::npos) addr = addr.substr(pos + 3);

    std::string host;
    int port = 3333;
    auto colon = addr.rfind(':');
    if (colon != std::string::npos) {
        host = addr.substr(0, colon);
        port = std::stoi(addr.substr(colon + 1));
    } else {
        host = addr;
    }

    LOG_INFO("Connecting to {}:{}", host, port);

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
        throw std::runtime_error("DNS resolution failed for " + host);

    sock_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
#ifdef _WIN32
    if (sock_ == INVALID_SOCKET) {
#else
    if (sock_ < 0) {
#endif
        freeaddrinfo(res);
        throw std::runtime_error("Socket creation failed");
    }

    if (::connect(sock_, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
#ifdef _WIN32
        closesocket(sock_);
#else
        ::close(sock_);
#endif
        sock_ = INVALID_SOCK;
        throw std::runtime_error("Connection to pool failed");
    }
    freeaddrinfo(res);

    connected_ = true;
    running_ = true;
    recv_thread_ = std::thread(&StratumClient::recv_loop, this);

    do_login();
    LOG_INFO("Connected to pool ✓");
}

void StratumClient::disconnect() {
    running_ = false;
    connected_ = false;
    if (sock_ != INVALID_SOCK) {
#ifdef _WIN32
        ::shutdown(sock_, SD_BOTH);
        closesocket(sock_);
#else
        ::shutdown(sock_, SHUT_RDWR);
        ::close(sock_);
#endif
        sock_ = INVALID_SOCK;
    }
    if (recv_thread_.joinable()) recv_thread_.join();
}

// ── Login (Stratum) ─────────────────────────────────────────────────────────
void StratumClient::do_login() {
    json req = {
        {"id", req_id_++},
        {"jsonrpc", "2.0"},
        {"method", "login"},
        {"params", {
            {"login", cfg_.user},
            {"pass",  cfg_.pass},
            {"agent", "ai-monero-miner/0.2.0"}
        }}
    };
    send(req.dump() + "\n");
}

// ── Submit share ────────────────────────────────────────────────────────────
void StratumClient::submit(const Job& job, uint32_t nonce, const uint8_t* result) {
    // Build nonce hex (little-endian 4 bytes)
    uint8_t nonce_bytes[4];
    std::memcpy(nonce_bytes, &nonce, 4);
    std::string nonce_hex = bytes_to_hex(nonce_bytes, 4);
    std::string result_hex = bytes_to_hex(result, 32);

    json req = {
        {"id", req_id_++},
        {"jsonrpc", "2.0"},
        {"method", "submit"},
        {"params", {
            {"id",     rpc_id_},
            {"job_id", job.id},
            {"nonce",  nonce_hex},
            {"result", result_hex}
        }}
    };
    send(req.dump() + "\n");
    LOG_INFO("Share submitted (nonce: {})", nonce_hex);
}

// ── Receive loop ────────────────────────────────────────────────────────────
void StratumClient::recv_loop() {
    std::string buffer;
    char chunk[4096];
    while (running_) {
#ifdef _WIN32
        int n = ::recv(sock_, chunk, sizeof(chunk) - 1, 0);
#else
        ssize_t n = ::recv(sock_, chunk, sizeof(chunk) - 1, 0);
#endif
        if (n <= 0) {
            if (running_) LOG_WARN("Pool connection lost");
            connected_ = false;
            break;
        }
        chunk[n] = '\0';
        buffer += chunk;

        // Process newline-delimited JSON messages
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty()) handle_message(line);
        }
    }
}

void StratumClient::handle_message(const std::string& line) {
    try {
        auto j = json::parse(line);

        // Login response
        if (j.contains("result") && j["result"].contains("id") && j["result"].contains("job")) {
            rpc_id_ = j["result"]["id"].get<std::string>();
            auto& jj = j["result"]["job"];
            auto job = std::make_shared<Job>();
            job->id = jj["job_id"].get<std::string>();
            hex_to_bytes(jj["blob"].get<std::string>(), job->blob.data(), job->blob.size());
            job->blob_size = jj["blob"].get<std::string>().size() / 2;
            auto target_hex = jj["target"].get<std::string>();
            hex_to_bytes(target_hex, job->target.data(), job->target.size());
            job->target_val = target_to_uint64(target_hex);
            if (jj.contains("seed_hash"))
                job->seed_hash = jj["seed_hash"].get<std::string>();
            if (jj.contains("height"))
                job->height = jj["height"].get<uint64_t>();
            LOG_INFO("New job: {} (height {})", job->id, job->height);
            if (job_cb_) job_cb_(job);
        }
        // New job notification
        else if (j.contains("method") && j["method"] == "job") {
            auto& jj = j["params"];
            auto job = std::make_shared<Job>();
            job->id = jj["job_id"].get<std::string>();
            hex_to_bytes(jj["blob"].get<std::string>(), job->blob.data(), job->blob.size());
            job->blob_size = jj["blob"].get<std::string>().size() / 2;
            auto target_hex = jj["target"].get<std::string>();
            hex_to_bytes(target_hex, job->target.data(), job->target.size());
            job->target_val = target_to_uint64(target_hex);
            if (jj.contains("seed_hash"))
                job->seed_hash = jj["seed_hash"].get<std::string>();
            if (jj.contains("height"))
                job->height = jj["height"].get<uint64_t>();
            LOG_INFO("New job: {} (height {})", job->id, job->height);
            if (job_cb_) job_cb_(job);
        }
    } catch (const std::exception& e) {
        LOG_WARN("Failed to parse pool message: {}", e.what());
    }
}

// ── Send ────────────────────────────────────────────────────────────────────
void StratumClient::send(const std::string& msg) {
    std::lock_guard lock(send_mtx_);
    if (sock_ != INVALID_SOCK)
        ::send(sock_, msg.c_str(), static_cast<int>(msg.size()), 0);
}

}  // namespace aiminer::network
