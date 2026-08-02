#pragma once

#include "oneshotsea/early_abort.hpp"
#include "oneshotsea/smooth_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace oneshotsea {

// Resource controls for exact n^4-smooth-part extraction.  A thread count of
// zero selects the OpenMP default.  The remainder tree is built independently
// for chunks no larger than max_orders_per_batch, which bounds its temporary
// storage even when a search submits a large collection of trace candidates.
struct ExactSmoothOptions {
    int thread_count = 0;
    std::size_t max_orders_per_batch = 4096;
    // Cap the two modulus-sized residue tables used while reducing the full
    // prime product at the batch root.  The product itself, the small order
    // product tree, per-thread GMP scratch, and allocator overhead are not
    // included in this cap.
    std::size_t max_root_auxiliary_bytes = 128U * 1024U * 1024U;
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
// by this engine.  Existing caches cannot be loaded without an explicit
// trusted digest.  Replacing a cache during a search can cause false rejection
// and is forbidden even though final certificate verification prevents false
// primality acceptance.
class ExactSmoothEngine {
public:
    static ExactSmoothEngine build(const mpz_class& prime,
                                   ExactSmoothOptions options = {});

    static ExactSmoothEngine load(const mpz_class& prime,
                                  const std::filesystem::path& cache_path,
                                  const std::string& trusted_sha256,
                                  ExactSmoothOptions options = {});

    // Load an existing cache.  If and only if the path does not exist, build a
    // complete product and atomically save it.  Missing parent directories are
    // created before the expensive build.  A malformed or wrong-bound existing
    // cache is rejected rather than silently replaced.
    static ExactSmoothEngine load_or_build(
        const mpz_class& prime, const std::filesystem::path& cache_path,
        const std::optional<std::string>& trusted_sha256 = std::nullopt,
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

    // Flatten several independent curve trace sets into one exact extraction
    // call and restore the original request boundaries in the result.  This
    // lets a search coordinator share the expensive full-product root
    // reduction across curves without changing any mathematical evidence.
    std::vector<std::vector<CurveTwistSmoothParts>>
    extract_curve_twist_groups(
        std::span<const std::span<const mpz_class>> trace_groups) const;

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

// Resource-only telemetry for the no-delay cross-curve coordinator. The sparse
// histogram records the number of curve/twist orders in successful underlying
// bounded-root scan chunks.
struct ExactSmoothScanChunkSizeCount {
    std::size_t order_count = 0U;
    std::uint64_t scan_chunks = 0U;
};

struct ExactSmoothBatchTelemetry {
    std::uint64_t submitted_requests = 0U;
    std::uint64_t completed_requests = 0U;
    std::uint64_t failed_requests = 0U;
    std::uint64_t cancelled_requests = 0U;
    std::uint64_t coordinator_batches = 0U;
    // Bounded-root scan chunks belonging to grouped extractions that returned
    // successfully. A failed grouped extraction contributes no scan counts,
    // even if a lower layer completed work before throwing.
    std::uint64_t successful_cache_scan_chunks = 0U;
    std::uint64_t submitted_orders = 0U;
    std::size_t max_queued_requests = 0U;
    std::size_t max_requests_per_batch = 0U;
    std::size_t max_orders_per_successful_scan_chunk = 0U;
    std::vector<ExactSmoothScanChunkSizeCount>
        successful_scan_chunks_by_order_count;
};

struct ExactSmoothBatchCoordinatorOptions {
    // Called on the coordinator thread after a FIFO batch has been removed
    // from the queue and immediately before its exact extraction begins.
    // This is intended for profiling and deterministic concurrency tests;
    // production should normally leave it empty. Because it runs on the sole
    // worker, it must not call extract_curve_twist on, or destroy/join, the
    // same coordinator. Calling telemetry() or cancel() is safe.
    std::function<void(std::size_t request_count, std::size_t order_count)>
        batch_start_callback;
};

// Serialize access to one immutable exact-smooth cache without introducing a
// collection delay.  The first ready request starts immediately.  Requests
// arriving during that scan remain in FIFO order; after the scan, as many
// complete request groups as fit in max_orders_per_batch are removed
// atomically and flattened into the next scan.
class ExactSmoothBatchCoordinator {
public:
    explicit ExactSmoothBatchCoordinator(
        ExactSmoothEngine engine,
        ExactSmoothBatchCoordinatorOptions options = {});
    ~ExactSmoothBatchCoordinator();

    ExactSmoothBatchCoordinator(const ExactSmoothBatchCoordinator&) = delete;
    ExactSmoothBatchCoordinator& operator=(
        const ExactSmoothBatchCoordinator&) = delete;
    ExactSmoothBatchCoordinator(ExactSmoothBatchCoordinator&&) = delete;
    ExactSmoothBatchCoordinator& operator=(
        ExactSmoothBatchCoordinator&&) = delete;

    std::vector<CurveTwistSmoothParts> extract_curve_twist(
        std::span<const mpz_class> traces) const;

    // A complete exact cache is uniquely determined by its target prime and
    // n^4 bound. Search entry points use this before doing any curve work so
    // an accidentally supplied coordinator cannot screen with another cache.
    bool compatible_with(const ExactSmoothEngine& engine) const;

    // Stop accepting work and fail requests that have not started.  An active
    // GMP extraction is not interruptible and is allowed to finish.  Safe to
    // call repeatedly; the destructor performs the same cancellation and
    // joins the coordinator thread.
    void cancel();

    ExactSmoothBatchTelemetry telemetry() const;

private:
    struct State;
    std::shared_ptr<State> state_;
    std::thread worker_;
};

}  // namespace oneshotsea
