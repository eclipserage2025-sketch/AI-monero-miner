#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace aiminer::network {

struct Job {
    std::string           id;
    std::array<uint8_t,76> blob{};
    size_t                blob_size = 0;
    std::array<uint8_t,32> target{};
    uint64_t              target_val = 0;
    uint64_t              height = 0;
    std::string           seed_hash;
    std::atomic<uint32_t> nonce{0};

    uint32_t next_nonce() { return nonce.fetch_add(1, std::memory_order_relaxed); }
};

}  // namespace aiminer::network
