#include "oneshotsea/exact_smooth.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void expect_exception(Function&& function, const std::string& message) {
    bool caught = false;
    try {
        function();
    } catch (const Exception&) {
        caught = true;
    }
    check(caught, message);
}

class TestDirectory {
public:
    TestDirectory() {
        const auto tick =
            std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            path = std::filesystem::temp_directory_path() /
                   ("oneshotsea-exact-smooth-" + std::to_string(tick) + "-" +
                    std::to_string(attempt));
            if (std::filesystem::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error("could not create exact-smooth test directory");
    }

    ~TestDirectory() {
        std::error_code error;
        if (path.filename().string().starts_with("oneshotsea-exact-smooth-")) {
            std::filesystem::remove_all(path, error);
        }
    }

    std::filesystem::path path;
};

void test_small_differential_and_batching(TestDirectory& temporary) {
    // 101 has seven bits, hence the engine must bind itself to B=7^4=2401.
    const oneshotsea::ExactSmoothOptions options{
        .thread_count = 2,
        .max_orders_per_batch = 3,
        .build_segment_span = 100,
        .cache_limits = {.max_product_bytes = 1024 * 1024},
    };
    const auto engine = oneshotsea::ExactSmoothEngine::build(101, options);
    check(engine.prime() == 101 && engine.bit_length() == 7 &&
              engine.bound() == 2401 && engine.prime_count() != 0,
          "engine target and full-bound metadata");

    std::vector<mpz_class> orders;
    for (unsigned value = 2; value <= 220; ++value) {
        mpz_class order = value;
        if (value % 5U == 0U) {
            order *= 2503;  // a prime strictly above the target bound
        }
        orders.push_back(std::move(order));
    }
    const auto parts = engine.extract(orders);
    check(parts.size() == orders.size(), "chunked batch preserves output size");
    for (std::size_t index = 0; index < orders.size(); ++index) {
        const mpz_class expected =
            oneshotsea::trial_smooth_part(orders[index], engine.bound());
        check(parts[index].value == expected,
              "small exact engine result differs from trial division");
        check(orders[index] % parts[index].value == 0,
              "small exact engine result divides input");
    }
    check(engine.extract(std::span<const mpz_class>{}).empty(),
          "empty batch is accepted");
    expect_exception<std::invalid_argument>(
        [&] { (void)engine.extract_one(1); },
        "order one must be rejected before entering pinned C engine");

    const auto cache = temporary.path / "full.cache";
    engine.save(cache);
    const auto loaded = oneshotsea::ExactSmoothEngine::load(101, cache, options);
    check(loaded.extract_one(2 * 2 * 3 * 2503).value == 12,
          "portable full-product cache reload");
    expect_exception<oneshotsea::SmoothCacheError>(
        [&] { (void)oneshotsea::ExactSmoothEngine::load(257, cache, options); },
        "cache bound must be tied to target bit length");
    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load(
                101, cache,
                {.thread_count = 1,
                 .max_orders_per_batch = 1,
                 .build_segment_span = 100,
                 .cache_limits = {.max_product_bytes = 8}});
        },
        "cache load must obey configured byte cap");
    expect_exception<oneshotsea::SmoothCacheError>(
        [] {
            (void)oneshotsea::ExactSmoothEngine::build(
                101,
                {.thread_count = 1,
                 .max_orders_per_batch = 1,
                 .cache_limits = {.max_product_bytes = 8}});
        },
        "built full product must obey configured byte cap");

    const auto generated_cache =
        temporary.path / "new-parent" / "nested" / "generated.cache";
    const auto generated = oneshotsea::ExactSmoothEngine::load_or_build(
        101, generated_cache, options);
    check(std::filesystem::is_regular_file(generated_cache) &&
              generated.extract_one(97 * 97 * 2503).value == 97 * 97,
          "missing cache is built, saved, and immediately usable");

    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load_or_build(
                101, temporary.path / "full.cache" / "child.cache", options);
        },
        "load-or-build reports a parent path that is an existing file");

    smooth_base segment{};
    smooth_base_build_range(&segment, 100, 2401, 1);
    const auto segment_cache = temporary.path / "segment.cache";
    oneshotsea::save_portable_smooth_base(segment, segment_cache);
    smooth_base_clear(&segment);
    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load(101, segment_cache,
                                                       options);
        },
        "partial cache must never create exact full-bound evidence");

    expect_exception<std::invalid_argument>(
        [] {
            (void)oneshotsea::ExactSmoothEngine::build(
                101, {.thread_count = -1, .max_orders_per_batch = 1});
        },
        "negative thread count rejected");
    expect_exception<std::invalid_argument>(
        [] {
            (void)oneshotsea::ExactSmoothEngine::build(
                101, {.thread_count = 1, .max_orders_per_batch = 0});
        },
        "zero batch cap rejected");
    expect_exception<std::invalid_argument>(
        [] {
            (void)oneshotsea::ExactSmoothEngine::build(
                101, {.thread_count = 1,
                      .max_orders_per_batch = 1,
                      .build_segment_span = 0});
        },
        "zero build segment span rejected");
}

void test_curve_twist_and_evidence_lifetime() {
    const auto engine = oneshotsea::ExactSmoothEngine::build(
        101, {.thread_count = 2, .max_orders_per_batch = 2});
    const std::array<mpz_class, 3> traces = {-10, 0, 10};
    const auto pairs = engine.extract_curve_twist(traces);
    check(pairs.size() == traces.size(), "curve/twist batch output size");
    for (std::size_t index = 0; index < traces.size(); ++index) {
        check(pairs[index].trace == traces[index] &&
                  pairs[index].curve_order == 102 - traces[index] &&
                  pairs[index].twist_order == 102 + traces[index],
              "curve/twist order convention");
        check(pairs[index].curve_smooth_part.value ==
                      oneshotsea::trial_smooth_part(
                          pairs[index].curve_order, engine.bound()) &&
                  pairs[index].twist_smooth_part.value ==
                      oneshotsea::trial_smooth_part(
                          pairs[index].twist_order, engine.bound()),
              "curve/twist exact smooth parts");
    }

    oneshotsea::SmoothPartExtractor retained_extractor;
    {
        const auto temporary = engine;
        retained_extractor = temporary.extractor();
    }
    const oneshotsea::SmoothPartEvidence evidence = retained_extractor(2503 * 12);
    check(std::holds_alternative<oneshotsea::ExactN4SmoothPart>(evidence) &&
              std::get<oneshotsea::ExactN4SmoothPart>(evidence).value == 12,
          "extractor retains engine and returns exact evidence type");
}

void test_p125_factored_segment_fixture() {
    // This fixture avoids constructing the ~3.7 GB primorial.  The independent
    // exhaustive segmented run recorded in docs/benchmark_20260730.md proved
    // that these are all prime divisors <=416^4 on the two orders.  Supplying
    // those squarefree support primes as disjoint miniature segments still
    // exercises the pinned multi-segment remainder tree and its recovery of
    // the complete prime powers from 416-bit inputs.
    constexpr std::uint64_t bound = UINT64_C(29948379136);
    std::array<smooth_base, 3> segments{};
    for (smooth_base& segment : segments) {
        mpz_init_set_ui(segment.P, 1);
        segment.y = bound;
    }
    segments[0].lo = 0;
    segments[0].nprimes = 3;
    mpz_set_ui(segments[0].P, 2 * 7 * 19);
    segments[1].lo = 19;
    segments[1].nprimes = 3;
    mpz_set_ui(segments[1].P, 23 * 109);
    mpz_mul_ui(segments[1].P, segments[1].P, 1486574729UL);
    segments[2].lo = 1486574729;
    segments[2].nprimes = 0;
    // P=1 is an intentionally empty third rung, checking that factored
    // fixtures and production ladders share the same neutral-element path.

    std::array<mpz_t, 2> orders;
    std::array<mpz_t, 2> parts;
    for (std::size_t index = 0; index < orders.size(); ++index) {
        mpz_init(orders[index]);
        mpz_init(parts[index]);
    }
    check(mpz_set_str(
              orders[0],
              "99999999999999999999999999999999999999999999999999999999999999421412458233122978783123953175222821780006676235765491044695104",
              10) == 0,
          "parse p125 curve order fixture");
    check(mpz_set_str(
              orders[1],
              "100000000000000000000000000000000000000000000000000000000000000578587541766877021216876046824777178219993323764234508955305372",
              10) == 0,
          "parse p125 twist order fixture");

    smooth_parts_multi(segments.data(), static_cast<int>(segments.size()),
                       orders.data(), orders.size(), parts.data(), 2);
    check(mpz_cmp_ui(parts[0], 332287808UL) == 0,
          "known p125 curve n^4-smooth part factored fixture");
    check(mpz_cmp_ui(parts[1], 41624092412UL) == 0,
          "known p125 twist n^4-smooth part factored fixture");

    for (std::size_t index = 0; index < orders.size(); ++index) {
        check(mpz_divisible_p(orders[index], parts[index]) != 0,
              "p125 fixture smooth part divides order");
        mpz_clear(parts[index]);
        mpz_clear(orders[index]);
    }
    for (smooth_base& segment : segments) {
        smooth_base_clear(&segment);
    }
}

}  // namespace

int main() {
    try {
        TestDirectory temporary;
        test_small_differential_and_batching(temporary);
        test_curve_twist_and_evidence_lifetime();
        test_p125_factored_segment_fixture();
        std::cout << "all exact-smooth tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "exact-smooth test failed: " << error.what() << '\n';
        return 1;
    }
}
