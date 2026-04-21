#pragma once
#include <atomic>

namespace vrpc::util {
[[nodiscard]]
static auto generate_sequence() -> uint64_t {
    static std::atomic<uint64_t> seq{0};
    return seq.fetch_add(1, std::memory_order_relaxed);
}
} // namespace vrpc::util