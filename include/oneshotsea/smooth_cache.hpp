#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>

#include <gmp.h>

extern "C" {
#include "../../third_party/oneshot_fast_ecpp/smooth.h"
}

namespace oneshotsea {

inline constexpr std::uint32_t kPortableSmoothCacheVersion = 1;
inline constexpr std::uint64_t kPortableSmoothCacheHeaderBytes = 64;
inline constexpr std::uint64_t kDefaultMaxSmoothProductBytes =
    UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024);

struct SmoothCacheLimits {
    std::uint64_t max_product_bytes = kDefaultMaxSmoothProductBytes;
};

class SmoothCacheError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Write an architecture-independent cache with this 64-byte header:
// magic[8], version:u32, header_bytes:u32, y:u64, lo:u64, nprimes:u64,
// product_bytes:u64, crc64_ecma:u64, reserved:u64. Integers and base.P are
// canonical unsigned big-endian bytes; no mp_limb_t is persisted. The target
// becomes visible only after the complete temporary file is flushed and
// atomically renamed in the same directory.
void save_portable_smooth_base(const smooth_base& base,
                               const std::filesystem::path& cache_path,
                               SmoothCacheLimits limits = {});

// Load into an already initialized smooth_base. On any validation or I/O
// failure, destination is unchanged. A successful call replaces destination.P
// and its y, lo, and nprimes metadata.
void load_portable_smooth_base(smooth_base& destination,
                               const std::filesystem::path& cache_path,
                               SmoothCacheLimits limits = {});

}  // namespace oneshotsea
