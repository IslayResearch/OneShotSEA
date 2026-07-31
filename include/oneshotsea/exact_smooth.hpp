#pragma once

#include "oneshotsea/early_abort.hpp"
#include "oneshotsea/smooth_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace oneshotsea {

// Resource controls for exact n^4-smooth-part extraction.  A thread count of
// zero selects the OpenMP default.  The remainder tree is built independently
// for chunks no larger than max_orders_per_batch, which bounds its temporary
// storage even when a search submits a large collection of trace candidates.
struct ExactSmoothOptions {
    int thread_count = 0;
    std::size_t max_orders_per_batch = 4096;
    // Bound the segmented sieve/product-tree working set while constructing
    // the full cache.  The resulting exact prime product is independent of
    // this span.  500 million kept the p125 build below 2 GB before the
    // accumulated product itself was retained.
    std::uint64_t build_segment_span = UINT64_C(500000000);
    SmoothCacheLimits cache_limits{};
};

struct CurveTwistSmoothParts {
    mpz_class trace;
    mpz_class curve_order;
    mpz_class twist_order;
    ExactN4SmoothPart curve_smooth_part;
    ExactN4SmoothPart twist_smooth_part;
};

// An immutable, copyable handle to a complete prime product
//
//     P = product(q prime : q <= bit_length(prime)^4).
//
// Only a full product with lo=0 and exactly the target n^4 upper bound can be
// used to construct ExactN4SmoothPart evidence.  Partial ladder products must
// use a different API and can never enter this class.  Loading verifies the
// portable cache CRC, bound, range metadata, and pinned-engine self-check, but
// intentionally does not rebuild the multi-gigabyte primorial.  Production
// search identities must therefore pin the digest/provenance of a cache built
// by this engine.  Replacing a cache during a search can cause false rejection
// and is forbidden even though final certificate verification prevents false
// primality acceptance.
class ExactSmoothEngine {
public:
    static ExactSmoothEngine build(const mpz_class& prime,
                                   ExactSmoothOptions options = {});

    static ExactSmoothEngine load(const mpz_class& prime,
                                  const std::filesystem::path& cache_path,
                                  ExactSmoothOptions options = {});

    // Load an existing cache.  If and only if the path does not exist, build a
    // complete product and atomically save it.  Missing parent directories are
    // created before the expensive build.  A malformed or wrong-bound existing
    // cache is rejected rather than silently replaced.
    static ExactSmoothEngine load_or_build(
        const mpz_class& prime, const std::filesystem::path& cache_path,
        ExactSmoothOptions options = {});

    void save(const std::filesystem::path& cache_path) const;

    const mpz_class& prime() const;
    unsigned long bit_length() const;
    std::uint64_t bound() const;
    std::uint64_t prime_count() const;
    const ExactSmoothOptions& options() const;

    ExactN4SmoothPart extract_one(const mpz_class& order) const;

    std::vector<ExactN4SmoothPart> extract(
        std::span<const mpz_class> orders) const;

    // For every trace t, compute p+1-t and p+1+t and submit all curve/twist
    // orders to the same batched remainder-tree call sequence.
    std::vector<CurveTwistSmoothParts> extract_curve_twist(
        std::span<const mpz_class> traces) const;

    // Safe to retain after the originating handle is destroyed: the returned
    // callable owns a copy of this shared immutable engine handle.
    SmoothPartExtractor extractor() const;

private:
    struct Data;

    ExactSmoothEngine(std::shared_ptr<const Data> data,
                      ExactSmoothOptions options);

    std::shared_ptr<const Data> data_;
    ExactSmoothOptions options_;
};

}  // namespace oneshotsea
