#include "oneshotsea/exact_smooth.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace oneshotsea {
namespace {

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

class MpzArray {
public:
    explicit MpzArray(std::size_t size) : size_(size) {
        if (size_ == 0) {
            return;
        }
        values_ = new mpz_t[size_];
        std::size_t initialized = 0;
        try {
            for (; initialized < size_; ++initialized) {
                mpz_init(values_[initialized]);
            }
        } catch (...) {
            while (initialized != 0) {
                mpz_clear(values_[--initialized]);
            }
            delete[] values_;
            values_ = nullptr;
            throw;
        }
    }

    MpzArray(const MpzArray&) = delete;
    MpzArray& operator=(const MpzArray&) = delete;

    ~MpzArray() {
        for (std::size_t index = 0; index < size_; ++index) {
            mpz_clear(values_[index]);
        }
        delete[] values_;
    }

    mpz_t* data() { return values_; }
    const mpz_t* data() const { return values_; }
    mpz_ptr operator[](std::size_t index) { return values_[index]; }
    mpz_srcptr operator[](std::size_t index) const { return values_[index]; }

private:
    mpz_t* values_ = nullptr;
    std::size_t size_ = 0;
};

void validate_options(const ExactSmoothOptions& options) {
    if (options.thread_count < 0) {
        throw std::invalid_argument("exact smooth thread count must be nonnegative");
    }
    if (options.max_orders_per_batch == 0) {
        throw std::invalid_argument("exact smooth batch size must be positive");
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

    smooth_base built{};
    smooth_base_build(&built, bound, options.thread_count);

    auto data = std::make_shared<Data>();
    data->prime = prime;
    data->bit_length = bits;
    data->bound = bound;
    data->base.adopt(built);
    smooth_base_clear(&built);
    validate_full_base(data->base.get(), bound);
    validate_product_size(data->base.get(), options.cache_limits);
    return ExactSmoothEngine(std::move(data), options);
}

ExactSmoothEngine ExactSmoothEngine::load(
    const mpz_class& prime, const std::filesystem::path& cache_path,
    ExactSmoothOptions options) {
    validate_options(options);
    const auto [bits, bound] = target_parameters(prime);
    auto data = std::make_shared<Data>();
    load_portable_smooth_base(data->base.get(), cache_path,
                              options.cache_limits);
    validate_full_base(data->base.get(), bound);
    validate_product_size(data->base.get(), options.cache_limits);
    data->prime = prime;
    data->bit_length = bits;
    data->bound = bound;
    return ExactSmoothEngine(std::move(data), options);
}

ExactSmoothEngine ExactSmoothEngine::load_or_build(
    const mpz_class& prime, const std::filesystem::path& cache_path,
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
        return load(prime, cache_path, options);
    }
    ExactSmoothEngine engine = build(prime, options);
    engine.save(cache_path);
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
        MpzArray input(count);
        MpzArray output(count);
        for (std::size_t index = 0; index < count; ++index) {
            const mpz_class& order = orders[begin + index];
            if (order <= 1) {
                throw std::invalid_argument("smooth-part order must exceed one");
            }
            mpz_set(input[index], order.get_mpz_t());
        }

        smooth_parts(&data_->base.get(), input.data(), count, output.data(),
                     options_.thread_count);
        for (std::size_t index = 0; index < count; ++index) {
            mpz_class value(output[index]);
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
    if (traces.size() > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::length_error("too many traces for curve/twist smoothness batch");
    }
    std::vector<mpz_class> orders;
    orders.reserve(2U * traces.size());
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

    std::vector<ExactN4SmoothPart> parts = extract(orders);
    std::vector<CurveTwistSmoothParts> result;
    result.reserve(traces.size());
    for (std::size_t index = 0; index < traces.size(); ++index) {
        result.push_back({traces[index], std::move(orders[2U * index]),
                          std::move(orders[2U * index + 1U]),
                          std::move(parts[2U * index]),
                          std::move(parts[2U * index + 1U])});
    }
    return result;
}

SmoothPartExtractor ExactSmoothEngine::extractor() const {
    const ExactSmoothEngine retained = *this;
    return [retained](const mpz_class& order) -> SmoothPartEvidence {
        return retained.extract_one(order);
    };
}

}  // namespace oneshotsea
