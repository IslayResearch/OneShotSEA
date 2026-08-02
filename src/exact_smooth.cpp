#include "oneshotsea/exact_smooth.hpp"

#include "oneshotsea/integrity.hpp"
#include "oneshotsea/smooth_bounded.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <future>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace oneshotsea {
namespace {

std::uint64_t checked_telemetry_add(std::uint64_t left,
                                    std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error("exact-smooth telemetry counter overflow");
    }
    return left + right;
}

std::uint64_t telemetry_count(std::size_t value) {
    if (value > std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("exact-smooth telemetry size overflow");
    }
    return static_cast<std::uint64_t>(value);
}

class SmoothBaseOwner {
public:
    SmoothBaseOwner() {
        mpz_init_set_ui(base_.P, 1);
        base_.y = 0;
        base_.lo = 0;
        base_.nprimes = 0;
    }

    SmoothBaseOwner(const SmoothBaseOwner&) = delete;
    SmoothBaseOwner& operator=(const SmoothBaseOwner&) = delete;

    SmoothBaseOwner(SmoothBaseOwner&& other) noexcept : SmoothBaseOwner() {
        swap(other);
    }

    SmoothBaseOwner& operator=(SmoothBaseOwner&& other) noexcept {
        if (this != &other) {
            swap(other);
        }
        return *this;
    }

    ~SmoothBaseOwner() { mpz_clear(base_.P); }

    smooth_base& get() { return base_; }
    const smooth_base& get() const { return base_; }

    void adopt(smooth_base& source) {
        mpz_swap(base_.P, source.P);
        std::swap(base_.y, source.y);
        std::swap(base_.lo, source.lo);
        std::swap(base_.nprimes, source.nprimes);
    }

private:
    void swap(SmoothBaseOwner& other) noexcept {
        mpz_swap(base_.P, other.base_.P);
        std::swap(base_.y, other.base_.y);
        std::swap(base_.lo, other.base_.lo);
        std::swap(base_.nprimes, other.base_.nprimes);
    }

    smooth_base base_{};
};

void validate_options(const ExactSmoothOptions& options) {
    if (options.thread_count < 0) {
        throw std::invalid_argument("exact smooth thread count must be nonnegative");
    }
    if (options.max_orders_per_batch == 0) {
        throw std::invalid_argument("exact smooth batch size must be positive");
    }
    if (options.max_root_auxiliary_bytes == 0) {
        throw std::invalid_argument(
            "exact smooth root auxiliary byte cap must be positive");
    }
    if (options.build_segment_span == 0) {
        throw std::invalid_argument(
            "exact smooth build segment span must be positive");
    }
    if (options.cache_limits.max_product_bytes == 0) {
        throw std::invalid_argument("exact smooth cache byte limit must be positive");
    }
}

std::pair<unsigned long, std::uint64_t> target_parameters(
    const mpz_class& prime) {
    if (prime <= 1) {
        throw std::invalid_argument("exact smooth target must exceed one");
    }
    const unsigned long bits = mpz_sizeinbase(prime.get_mpz_t(), 2);
    const std::uint64_t n = static_cast<std::uint64_t>(bits);
    if (n != 0 && n > std::numeric_limits<std::uint64_t>::max() / n) {
        throw std::overflow_error("target bit length squared does not fit uint64");
    }
    const std::uint64_t n2 = n * n;
    if (n2 != 0 && n2 > std::numeric_limits<std::uint64_t>::max() / n2) {
        throw std::overflow_error("target n^4 smoothness bound does not fit uint64");
    }
    return {bits, n2 * n2};
}

void validate_full_base(const smooth_base& base, std::uint64_t expected_bound) {
    if (base.lo != 0) {
        throw SmoothCacheError("exact smooth cache is a segment, not a full product");
    }
    if (base.y != expected_bound) {
        throw SmoothCacheError("exact smooth cache has the wrong n^4 bound");
    }
    if (base.nprimes == 0 || base.nprimes > base.y) {
        throw SmoothCacheError("exact smooth cache has an invalid prime count");
    }
    if (mpz_sgn(base.P) <= 0 || smooth_base_selfcheck(&base) == 0) {
        throw SmoothCacheError("exact smooth cache failed prime-product self-check");
    }
}

void validate_product_size(const smooth_base& base, SmoothCacheLimits limits) {
    const std::size_t bits = mpz_sizeinbase(base.P, 2);
    const std::size_t bytes = (bits + 7U) / 8U;
    if (bytes > limits.max_product_bytes) {
        throw SmoothCacheError(
            "exact smooth product exceeds configured cache/memory byte limit");
    }
}

}  // namespace

struct ExactSmoothEngine::Data {
    mpz_class prime;
    unsigned long bit_length = 0;
    std::uint64_t bound = 0;
    SmoothBaseOwner base;
};

ExactSmoothEngine::ExactSmoothEngine(std::shared_ptr<const Data> data,
                                     ExactSmoothOptions options)
    : data_(std::move(data)), options_(options) {}

ExactSmoothEngine ExactSmoothEngine::build(const mpz_class& prime,
                                           ExactSmoothOptions options) {
    validate_options(options);
    const auto [bits, bound] = target_parameters(prime);
    auto data = std::make_shared<Data>();
    data->prime = prime;
    data->bit_length = bits;
    data->bound = bound;

    std::uint64_t total_primes = 0;
    // Retain one completed segment product at each binary level.  Folding a
    // new segment through occupied levels gives a balanced multiplication
    // forest instead of repeatedly multiplying a small segment into the
    // entire accumulated multi-gigabyte product.  The product is commutative,
    // so this changes only setup cost and peak temporaries, never cache bytes.
    std::vector<std::optional<SmoothBaseOwner>> product_levels;
    for (std::uint64_t lower = 0; lower < bound;) {
        const std::uint64_t remaining = bound - lower;
        const std::uint64_t upper =
            lower + std::min(options.build_segment_span, remaining);
        smooth_base segment{};
        smooth_base_build_range(
            &segment, lower, upper, options.thread_count);
        try {
            if (segment.lo != lower || segment.y != upper ||
                segment.nprimes > upper - lower || mpz_sgn(segment.P) <= 0) {
                throw std::runtime_error(
                    "segmented smooth-base builder returned invalid metadata");
            }
            if (segment.nprimes >
                std::numeric_limits<std::uint64_t>::max() - total_primes) {
                throw std::overflow_error(
                    "exact smooth prime count does not fit uint64");
            }
            total_primes += segment.nprimes;
            SmoothBaseOwner current;
            current.adopt(segment);
            std::size_t level = 0U;
            for (;;) {
                if (level == product_levels.size()) {
                    product_levels.emplace_back(std::move(current));
                    break;
                }
                if (!product_levels[level].has_value()) {
                    product_levels[level].emplace(std::move(current));
                    break;
                }
                mpz_mul(current.get().P,
                        product_levels[level]->get().P,
                        current.get().P);
                product_levels[level].reset();
                ++level;
            }
        } catch (...) {
            smooth_base_clear(&segment);
            throw;
        }
        smooth_base_clear(&segment);
        lower = upper;
    }
    // At most log2(number of segments) products remain.  Descending levels
    // keeps the largest product on the accumulator side and bounds the number
    // of final unbalanced multiplications logarithmically.
    for (auto level = product_levels.rbegin();
         level != product_levels.rend(); ++level) {
        if (level->has_value()) {
            mpz_mul(data->base.get().P, data->base.get().P,
                    (*level)->get().P);
        }
    }
    data->base.get().lo = 0;
    data->base.get().y = bound;
    data->base.get().nprimes = total_primes;
    validate_full_base(data->base.get(), bound);
    validate_product_size(data->base.get(), options.cache_limits);
    return ExactSmoothEngine(std::move(data), options);
}

ExactSmoothEngine ExactSmoothEngine::load(
    const mpz_class& prime, const std::filesystem::path& cache_path,
    const std::string& trusted_sha256, ExactSmoothOptions options) {
    validate_options(options);
    if (!is_lower_sha256(trusted_sha256)) {
        throw SmoothCacheError(
            "trusted exact-smooth cache SHA-256 is malformed");
    }
    if (sha256_file(cache_path) != trusted_sha256) {
        throw SmoothCacheError(
            "exact-smooth cache does not match its trusted SHA-256");
    }
    const auto [bits, bound] = target_parameters(prime);
    auto data = std::make_shared<Data>();
    load_portable_smooth_base(data->base.get(), cache_path,
                              options.cache_limits);
    validate_full_base(data->base.get(), bound);
    validate_product_size(data->base.get(), options.cache_limits);
    if (sha256_file(cache_path) != trusted_sha256) {
        throw SmoothCacheError(
            "exact-smooth cache changed while it was being loaded");
    }
    data->prime = prime;
    data->bit_length = bits;
    data->bound = bound;
    return ExactSmoothEngine(std::move(data), options);
}

ExactSmoothEngine ExactSmoothEngine::load_or_build(
    const mpz_class& prime, const std::filesystem::path& cache_path,
    const std::optional<std::string>& trusted_sha256,
    ExactSmoothOptions options) {
    validate_options(options);
    if (cache_path.empty()) {
        throw SmoothCacheError("exact smooth cache path is empty");
    }
    std::error_code error;
    const std::filesystem::path parent = cache_path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, error)) {
        if (error) {
            throw SmoothCacheError("cannot inspect smooth cache directory " +
                                   parent.string() + ": " + error.message());
        }
        if (!std::filesystem::create_directories(parent, error) && error) {
            throw SmoothCacheError("cannot create smooth cache directory " +
                                   parent.string() + ": " + error.message());
        }
    }
    error.clear();
    const bool exists = std::filesystem::exists(cache_path, error);
    if (error) {
        throw SmoothCacheError("cannot inspect smooth cache " +
                               cache_path.string() + ": " + error.message());
    }
    if (exists) {
        if (!trusted_sha256.has_value()) {
            throw SmoothCacheError(
                "existing exact-smooth cache requires a trusted SHA-256");
        }
        return load(prime, cache_path, *trusted_sha256, options);
    }
    ExactSmoothEngine engine = build(prime, options);
    engine.save(cache_path);
    if (trusted_sha256.has_value() &&
        sha256_file(cache_path) != *trusted_sha256) {
        throw SmoothCacheError(
            "new exact-smooth cache does not match its trusted SHA-256");
    }
    return engine;
}

void ExactSmoothEngine::save(const std::filesystem::path& cache_path) const {
    save_portable_smooth_base(data_->base.get(), cache_path,
                              options_.cache_limits);
}

const mpz_class& ExactSmoothEngine::prime() const { return data_->prime; }

unsigned long ExactSmoothEngine::bit_length() const { return data_->bit_length; }

std::uint64_t ExactSmoothEngine::bound() const { return data_->bound; }

std::uint64_t ExactSmoothEngine::prime_count() const {
    return data_->base.get().nprimes;
}

const ExactSmoothOptions& ExactSmoothEngine::options() const { return options_; }

ExactN4SmoothPart ExactSmoothEngine::extract_one(const mpz_class& order) const {
    const std::array<mpz_class, 1> orders = {order};
    return extract(orders).front();
}

std::vector<ExactN4SmoothPart> ExactSmoothEngine::extract(
    std::span<const mpz_class> orders) const {
    std::vector<ExactN4SmoothPart> result;
    result.reserve(orders.size());
    for (std::size_t begin = 0; begin < orders.size();) {
        const std::size_t count =
            std::min(options_.max_orders_per_batch, orders.size() - begin);
        std::vector<mpz_class> values = bounded_smooth_parts(
            data_->base.get(), orders.subspan(begin, count),
            options_.thread_count, options_.max_root_auxiliary_bytes);
        for (std::size_t index = 0; index < count; ++index) {
            mpz_class value = std::move(values[index]);
            const mpz_class& order = orders[begin + index];
            if (value <= 0 || order % value != 0) {
                throw std::runtime_error(
                    "exact smooth engine violated the divisibility invariant");
            }
            result.push_back({std::move(value)});
        }
        begin += count;
    }
    return result;
}

std::vector<CurveTwistSmoothParts> ExactSmoothEngine::extract_curve_twist(
    std::span<const mpz_class> traces) const {
    const std::array<std::span<const mpz_class>, 1> groups = {traces};
    auto result = extract_curve_twist_groups(groups);
    return std::move(result.front());
}

std::vector<std::vector<CurveTwistSmoothParts>>
ExactSmoothEngine::extract_curve_twist_groups(
    std::span<const std::span<const mpz_class>> trace_groups) const {
    std::size_t trace_count = 0U;
    for (const std::span<const mpz_class> traces : trace_groups) {
        if (traces.size() >
            std::numeric_limits<std::size_t>::max() - trace_count) {
            throw std::length_error(
                "too many traces for grouped curve/twist smoothness batch");
        }
        trace_count += traces.size();
    }
    if (trace_count > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::length_error(
            "too many traces for grouped curve/twist smoothness batch");
    }
    std::vector<mpz_class> orders;
    orders.reserve(2U * trace_count);
    for (const std::span<const mpz_class> traces : trace_groups) {
        for (const mpz_class& trace : traces) {
            const mpz_class curve_order = data_->prime + 1 - trace;
            const mpz_class twist_order = data_->prime + 1 + trace;
            if (curve_order <= 1 || twist_order <= 1) {
                throw std::invalid_argument(
                    "trace produces a nonpositive curve or twist order");
            }
            orders.push_back(curve_order);
            orders.push_back(twist_order);
        }
    }

    std::vector<ExactN4SmoothPart> parts = extract(orders);
    std::vector<std::vector<CurveTwistSmoothParts>> result;
    result.reserve(trace_groups.size());
    std::size_t offset = 0U;
    for (const std::span<const mpz_class> traces : trace_groups) {
        std::vector<CurveTwistSmoothParts>& group = result.emplace_back();
        group.reserve(traces.size());
        for (const mpz_class& trace : traces) {
            group.push_back({trace, std::move(orders[2U * offset]),
                             std::move(orders[2U * offset + 1U]),
                             std::move(parts[2U * offset]),
                             std::move(parts[2U * offset + 1U])});
            ++offset;
        }
    }
    return result;
}

SmoothPartExtractor ExactSmoothEngine::extractor() const {
    const ExactSmoothEngine retained = *this;
    return [retained](const mpz_class& order) -> SmoothPartEvidence {
        return retained.extract_one(order);
    };
}

struct ExactSmoothBatchCoordinator::State {
    struct Request {
        explicit Request(std::span<const mpz_class> source)
            : traces(source.begin(), source.end()) {}

        std::vector<mpz_class> traces;
        std::promise<std::vector<CurveTwistSmoothParts>> promise;
        bool start_immediately = false;
    };

    State(ExactSmoothEngine source_engine,
          ExactSmoothBatchCoordinatorOptions source_options)
        : engine(std::move(source_engine)),
          options(std::move(source_options)) {}

    void run() {
        for (;;) {
            std::vector<std::shared_ptr<Request>> batch;
            std::size_t order_count = 0U;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock, [&] { return !queue.empty() || !accepting; });
                if (queue.empty()) {
                    return;
                }

                const std::size_t cap =
                    engine.options().max_orders_per_batch;
                do {
                    const std::shared_ptr<Request>& next = queue.front();
                    const std::size_t next_orders = 2U * next->traces.size();
                    if (!batch.empty() &&
                        (order_count >= cap || next_orders > cap - order_count)) {
                        break;
                    }
                    order_count += next_orders;
                    batch.push_back(next);
                    queue.pop_front();
                } while (!batch.front()->start_immediately && !queue.empty());

                telemetry.max_requests_per_batch =
                    std::max(telemetry.max_requests_per_batch, batch.size());
            }

            try {
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    telemetry.coordinator_batches = checked_telemetry_add(
                        telemetry.coordinator_batches, 1U);
                }
                if (options.batch_start_callback) {
                    options.batch_start_callback(batch.size(), order_count);
                }
                std::vector<std::span<const mpz_class>> groups;
                groups.reserve(batch.size());
                for (const std::shared_ptr<Request>& request : batch) {
                    groups.emplace_back(request->traces);
                }
                auto results = engine.extract_curve_twist_groups(groups);
                if (results.size() != batch.size()) {
                    throw std::logic_error(
                        "grouped exact-smooth extraction lost a request boundary");
                }
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    ExactSmoothBatchTelemetry updated = telemetry;
                    const std::size_t cap =
                        engine.options().max_orders_per_batch;
                    for (std::size_t remaining = order_count; remaining != 0U;) {
                        const std::size_t scan_orders =
                            std::min(cap, remaining);
                        updated.successful_cache_scan_chunks =
                            checked_telemetry_add(
                                updated.successful_cache_scan_chunks, 1U);
                        const auto found = std::find_if(
                            updated.successful_scan_chunks_by_order_count
                                .begin(),
                            updated.successful_scan_chunks_by_order_count
                                .end(),
                            [&](const ExactSmoothScanChunkSizeCount& value) {
                                return value.order_count == scan_orders;
                            });
                        if (found ==
                            updated.successful_scan_chunks_by_order_count
                                .end()) {
                            updated.successful_scan_chunks_by_order_count
                                .push_back({scan_orders, 1U});
                        } else {
                            found->scan_chunks = checked_telemetry_add(
                                found->scan_chunks, 1U);
                        }
                        updated.max_orders_per_successful_scan_chunk =
                            std::max(
                                updated.max_orders_per_successful_scan_chunk,
                                scan_orders);
                        remaining -= scan_orders;
                    }
                    updated.completed_requests = checked_telemetry_add(
                        updated.completed_requests,
                        telemetry_count(batch.size()));
                    telemetry = std::move(updated);
                }
                for (std::size_t index = 0U; index < batch.size(); ++index) {
                    batch[index]->promise.set_value(std::move(results[index]));
                }
            } catch (...) {
                std::exception_ptr failure = std::current_exception();
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    try {
                        telemetry.failed_requests = checked_telemetry_add(
                            telemetry.failed_requests,
                            telemetry_count(batch.size()));
                    } catch (...) {
                        failure = std::current_exception();
                    }
                }
                for (const std::shared_ptr<Request>& request : batch) {
                    request->promise.set_exception(failure);
                }
            }
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (queue.empty()) {
                    scan_active = false;
                }
            }
        }
    }

    ExactSmoothEngine engine;
    ExactSmoothBatchCoordinatorOptions options;
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::shared_ptr<Request>> queue;
    bool accepting = true;
    bool scan_active = false;
    ExactSmoothBatchTelemetry telemetry;
};

ExactSmoothBatchCoordinator::ExactSmoothBatchCoordinator(
    ExactSmoothEngine engine, ExactSmoothBatchCoordinatorOptions options)
    : state_(std::make_shared<State>(std::move(engine), std::move(options))),
      worker_([state = state_] { state->run(); }) {}

ExactSmoothBatchCoordinator::~ExactSmoothBatchCoordinator() {
    try {
        cancel();
    } catch (...) {
        std::deque<std::shared_ptr<State::Request>> cancelled;
        {
            const std::lock_guard<std::mutex> lock(state_->mutex);
            state_->accepting = false;
            cancelled.swap(state_->queue);
        }
        const std::exception_ptr failure = std::current_exception();
        for (const std::shared_ptr<State::Request>& request : cancelled) {
            request->promise.set_exception(failure);
        }
        state_->ready.notify_one();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::vector<CurveTwistSmoothParts>
ExactSmoothBatchCoordinator::extract_curve_twist(
    std::span<const mpz_class> traces) const {
    if (traces.empty()) {
        return {};
    }
    if (traces.size() > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::length_error(
            "too many traces for coordinated curve/twist smoothness batch");
    }
    for (const mpz_class& trace : traces) {
        if (state_->engine.prime() + 1 - trace <= 1 ||
            state_->engine.prime() + 1 + trace <= 1) {
            throw std::invalid_argument(
                "trace produces a nonpositive curve or twist order");
        }
    }

    auto request = std::make_shared<State::Request>(traces);
    std::future<std::vector<CurveTwistSmoothParts>> result =
        request->promise.get_future();
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->accepting) {
            throw std::runtime_error(
                "exact-smooth batch coordinator is cancelled");
        }
        request->start_immediately =
            !state_->scan_active && state_->queue.empty();
        ExactSmoothBatchTelemetry updated = state_->telemetry;
        updated.submitted_requests = checked_telemetry_add(
            updated.submitted_requests, 1U);
        updated.submitted_orders = checked_telemetry_add(
            updated.submitted_orders,
            checked_telemetry_add(telemetry_count(traces.size()),
                                  telemetry_count(traces.size())));
        updated.max_queued_requests = std::max(
            updated.max_queued_requests, state_->queue.size() + 1U);
        state_->queue.push_back(request);
        state_->telemetry = std::move(updated);
        state_->scan_active = true;
    }
    state_->ready.notify_one();
    return result.get();
}

bool ExactSmoothBatchCoordinator::compatible_with(
    const ExactSmoothEngine& engine) const {
    return state_->engine.prime() == engine.prime() &&
           state_->engine.bound() == engine.bound();
}

void ExactSmoothBatchCoordinator::cancel() {
    std::deque<std::shared_ptr<State::Request>> cancelled;
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->accepting) {
            return;
        }
        const std::uint64_t cancelled_count = checked_telemetry_add(
            state_->telemetry.cancelled_requests,
            telemetry_count(state_->queue.size()));
        state_->accepting = false;
        cancelled.swap(state_->queue);
        state_->telemetry.cancelled_requests = cancelled_count;
    }
    const std::exception_ptr failure = std::make_exception_ptr(
        std::runtime_error("exact-smooth batch coordinator is cancelled"));
    for (const std::shared_ptr<State::Request>& request : cancelled) {
        request->promise.set_exception(failure);
    }
    state_->ready.notify_one();
}

ExactSmoothBatchTelemetry ExactSmoothBatchCoordinator::telemetry() const {
    const std::lock_guard<std::mutex> lock(state_->mutex);
    ExactSmoothBatchTelemetry result = state_->telemetry;
    std::sort(result.successful_scan_chunks_by_order_count.begin(),
              result.successful_scan_chunks_by_order_count.end(),
              [](const ExactSmoothScanChunkSizeCount& left,
                 const ExactSmoothScanChunkSizeCount& right) {
                  return left.order_count < right.order_count;
              });
    return result;
}

}  // namespace oneshotsea
