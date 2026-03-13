#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward-declare RandomX types
struct randomx_cache;
struct randomx_dataset;
struct randomx_vm;

namespace aiminer::crypto {

/// RAII wrapper around the RandomX library
class RandomXHandler {
public:
    RandomXHandler();
    ~RandomXHandler();

    RandomXHandler(const RandomXHandler&) = delete;
    RandomXHandler& operator=(const RandomXHandler&) = delete;

    /// Initialise / re-seed when the seed hash changes
    void init(const std::string& seed_hash_hex, int thread_count, bool huge_pages);

    /// Create a per-thread VM (each worker needs its own)
    randomx_vm* create_vm();

    /// Compute a RandomX hash
    static void calculate_hash(randomx_vm* vm, const uint8_t* input, size_t len, uint8_t* output);

    const std::string& current_seed() const { return current_seed_; }

private:
    randomx_cache*   cache_   = nullptr;
    randomx_dataset* dataset_ = nullptr;
    std::string      current_seed_;
    int              flags_   = 0;
    std::mutex       init_mtx_;
};

}  // namespace aiminer::crypto
