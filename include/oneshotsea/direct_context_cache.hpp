#pragma once

#include "oneshotsea/sea.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace oneshotsea {

inline constexpr std::uint32_t kClassicalDirectContextCacheVersion = 1U;
inline constexpr std::uint64_t kClassicalDirectContextCacheHeaderBytes = 96U;
inline constexpr std::uint64_t kDefaultMaxClassicalDirectContextCacheBytes =
    UINT64_C(4) * UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024);

struct ClassicalDirectContextCacheLimits {
    std::uint64_t max_file_bytes =
        kDefaultMaxClassicalDirectContextCacheBytes;
    std::uint64_t max_levels = 4096U;
    std::uint64_t max_witnesses_per_level = 1000000U;
};

class ClassicalDirectContextCacheError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Materialize every level in the schedule and atomically publish a canonical,
// architecture-independent cache. Integers and uint64 matrix entries use
// unsigned big-endian encodings; no GMP limbs or host layout are persisted.
// The returned SHA-256 is the trust anchor that must be distributed with the
// artifact and supplied to the loader.
std::string save_classical_direct_context_cache(
    const ClassicalDirectSeaContext& context,
    const std::filesystem::path& cache_path,
    ClassicalDirectContextCacheLimits limits = {});

// Authenticate the complete file against trusted_sha256 before parsing it.
// Loading also re-derives every suitable order, coefficient bound, and
// deterministic CRT witness sequence from the expected target/schedule/caps;
// validates matrix dimensions, canonical residues, Lagrange partition of
// unity, and neighbor monicity; and rejects trailing data. The caller-supplied
// digest is an external authenticity anchor, not merely a corruption check.
ClassicalDirectSeaContext load_classical_direct_context_cache(
    const Field& target_field, const std::vector<std::uint64_t>& levels,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface,
    std::size_t preparation_threads,
    const std::filesystem::path& cache_path,
    const std::string& trusted_sha256,
    ClassicalDirectContextCacheLimits limits = {});

}  // namespace oneshotsea
