#include "crypto/randomx_handler.h"

#include <randomx.h>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

#include "utils/logger.h"

namespace aiminer::crypto {

// ── Hex helpers ─────────────────────────────────────────────────────────────
static uint8_t hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes(hex.size() / 2);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = (hex_val(hex[2 * i]) << 4) | hex_val(hex[2 * i + 1]);
    return bytes;
}

// ── Constructor / Destructor ────────────────────────────────────────────────
RandomXHandler::RandomXHandler() = default;

RandomXHandler::~RandomXHandler() {
    if (dataset_) randomx_release_dataset(dataset_);
    if (cache_)   randomx_release_cache(cache_);
}

// ── Init ────────────────────────────────────────────────────────────────────
void RandomXHandler::init(const std::string& seed_hash_hex, int thread_count, bool huge_pages) {
    std::lock_guard lock(init_mtx_);

    if (seed_hash_hex == current_seed_) return;  // already seeded

    LOG_INFO("Initialising RandomX (seed: {}…)", seed_hash_hex.substr(0, 16));

    flags_ = randomx_get_flags();
    if (huge_pages) flags_ |= RANDOMX_FLAG_LARGE_PAGES;
    flags_ |= RANDOMX_FLAG_FULL_MEM;  // use dataset for speed

    // Cache
    if (cache_) randomx_release_cache(cache_);
    cache_ = randomx_alloc_cache(static_cast<randomx_flags>(flags_));
    if (!cache_) throw std::runtime_error("Failed to allocate RandomX cache");

    auto seed = hex_to_bytes(seed_hash_hex);
    randomx_init_cache(cache_, seed.data(), seed.size());

    // Dataset (multi-threaded init)
    if (dataset_) randomx_release_dataset(dataset_);
    dataset_ = randomx_alloc_dataset(static_cast<randomx_flags>(flags_));
    if (!dataset_) throw std::runtime_error("Failed to allocate RandomX dataset");

    auto items = randomx_dataset_item_count();
    auto per_thread = items / thread_count;
    std::vector<std::thread> threads;
    for (int t = 0; t < thread_count; ++t) {
        auto start = t * per_thread;
        auto count = (t == thread_count - 1) ? (items - start) : per_thread;
        threads.emplace_back([this, start, count]() {
            randomx_init_dataset(dataset_, cache_, start, count);
        });
    }
    for (auto& t : threads) t.join();

    current_seed_ = seed_hash_hex;
    LOG_INFO("RandomX initialised ✓ ({} dataset items)", items);
}

// ── Create VM ───────────────────────────────────────────────────────────────
randomx_vm* RandomXHandler::create_vm() {
    auto* vm = randomx_create_vm(
        static_cast<randomx_flags>(flags_), nullptr, dataset_);
    if (!vm) throw std::runtime_error("Failed to create RandomX VM");
    return vm;
}

// ── Hash ────────────────────────────────────────────────────────────────────
void RandomXHandler::calculate_hash(randomx_vm* vm, const uint8_t* input, size_t len, uint8_t* output) {
    randomx_calculate_hash(vm, input, len, output);
}

}  // namespace aiminer::crypto
