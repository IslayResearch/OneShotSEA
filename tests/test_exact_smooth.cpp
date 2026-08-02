#include "oneshotsea/exact_smooth.hpp"
#include "oneshotsea/integrity.hpp"
#include "oneshotsea/smooth_bounded.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

template <std::size_t Size>
class TestMpzArray {
public:
    TestMpzArray() {
        for (mpz_t& value : values_) {
            mpz_init(value);
        }
    }

    TestMpzArray(const TestMpzArray&) = delete;
    TestMpzArray& operator=(const TestMpzArray&) = delete;

    ~TestMpzArray() {
        for (mpz_t& value : values_) {
            mpz_clear(value);
        }
    }

    mpz_t* data() { return values_.data(); }
    mpz_ptr operator[](std::size_t index) { return values_[index]; }
    mpz_srcptr operator[](std::size_t index) const { return values_[index]; }

private:
    std::array<mpz_t, Size> values_{};
};

void test_bounded_reducer_differential() {
    smooth_base base{};
    smooth_base_build(&base, 100000, 2);

    std::vector<mpz_class> orders;
    orders.emplace_back(mpz_class(128) * 9 * 100003);
    orders.emplace_back(mpz_class(97) * 97 * 97 * 100003 * 100003);
    orders.emplace_back(mpz_class(99991) * 99991 * 100003);
    orders.emplace_back(mpz_class(1048576) * 65537 * 100019);
    orders.emplace_back(mpz_class(99991) * 99991 * 99991);

    mpz_class root = 1;
    TestMpzArray<5> input;
    TestMpzArray<5> upstream;
    for (std::size_t index = 0; index < orders.size(); ++index) {
        root *= orders[index];
        mpz_set(input[index], orders[index].get_mpz_t());
    }
    smooth_parts(&base, input.data(), orders.size(), upstream.data(), 2);

    const std::size_t root_limbs = mpz_size(root.get_mpz_t());
    const std::size_t one_item_cap =
        2U * root_limbs * sizeof(mp_limb_t);
    check(mpz_size(base.P) > root_limbs + 1U,
          "bounded reducer fixture spans more than one root chunk");
    const std::vector<mpz_class> bounded = oneshotsea::bounded_smooth_parts(
        base, orders, 2, one_item_cap);
    check(bounded.size() == orders.size(),
          "bounded reducer preserves non-power-of-two batch size");
    for (std::size_t index = 0; index < orders.size(); ++index) {
        const mpz_class trial =
            oneshotsea::trial_smooth_part(orders[index], base.y);
        check(bounded[index] == trial,
              "bounded reducer differs from trial division");
        check(mpz_cmp(upstream[index], bounded[index].get_mpz_t()) == 0,
              "bounded reducer differs from upstream remainder tree");
    }

    const std::array<mpz_class, 1> single = {orders.front()};
    const std::size_t single_cap =
        2U * mpz_size(single.front().get_mpz_t()) * sizeof(mp_limb_t);
    const auto single_result =
        oneshotsea::bounded_smooth_parts(base, single, 1, single_cap);
    check(single_result.size() == 1U &&
              single_result.front() ==
                  oneshotsea::trial_smooth_part(single.front(), base.y),
          "bounded reducer handles one order with one-item blocks");
    check(oneshotsea::bounded_smooth_parts(
              base, std::span<const mpz_class>{}, 1, 1U).empty(),
          "bounded reducer accepts an empty batch");
    expect_exception<std::invalid_argument>(
        [&] {
            (void)oneshotsea::bounded_smooth_parts(
                base, orders, 1, one_item_cap - 1U);
        },
        "bounded reducer rejects a cap smaller than one table item");
    expect_exception<std::invalid_argument>(
        [&] {
            const std::array<mpz_class, 1> invalid = {1};
            (void)oneshotsea::bounded_smooth_parts(base, invalid, 1, 1024U);
        },
        "bounded reducer rejects order one");
    smooth_base_clear(&base);
}

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
    const std::string cache_sha = oneshotsea::sha256_file(cache);
    const auto loaded = oneshotsea::ExactSmoothEngine::load(
        101, cache, cache_sha, options);
    check(loaded.extract_one(2 * 2 * 3 * 2503).value == 12,
          "portable full-product cache reload");
    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load(
                257, cache, cache_sha, options);
        },
        "cache bound must be tied to target bit length");
    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load(
                101, cache, cache_sha,
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
        101, generated_cache, std::nullopt, options);
    check(std::filesystem::is_regular_file(generated_cache) &&
              generated.extract_one(97 * 97 * 2503).value == 97 * 97,
          "missing cache is built, saved, and immediately usable");

    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load_or_build(
                101, temporary.path / "full.cache" / "child.cache",
                std::nullopt, options);
        },
        "load-or-build reports a parent path that is an existing file");

    smooth_base segment{};
    smooth_base_build_range(&segment, 100, 2401, 1);
    const auto segment_cache = temporary.path / "segment.cache";
    oneshotsea::save_portable_smooth_base(segment, segment_cache);
    smooth_base_clear(&segment);
    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load(
                101, segment_cache, oneshotsea::sha256_file(segment_cache),
                options);
        },
        "partial cache must never create exact full-bound evidence");

    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load(
                101, cache, std::string(64U, '0'), options);
        },
        "cache load requires the exact trusted digest");
    expect_exception<oneshotsea::SmoothCacheError>(
        [&] {
            (void)oneshotsea::ExactSmoothEngine::load_or_build(
                101, cache, std::nullopt, options);
        },
        "existing cache cannot mint exact evidence without a trust anchor");

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
                101,
                {.thread_count = 1,
                 .max_orders_per_batch = 1,
                 .max_root_auxiliary_bytes = 0});
        },
        "zero root auxiliary cap rejected");
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

    // Parallel curve search must share this immutable product rather than
    // loading one cache per curve. Exercise concurrent readers directly and
    // compare every result with the serial fixture above.
    std::array<std::future<std::vector<oneshotsea::CurveTwistSmoothParts>>, 4>
        concurrent;
    for (auto& future : concurrent) {
        future = std::async(std::launch::async, [&] {
            return engine.extract_curve_twist(traces);
        });
    }
    for (auto& future : concurrent) {
        const auto parallel = future.get();
        check(parallel.size() == pairs.size(),
              "concurrent shared-cache extraction output size");
        for (std::size_t index = 0; index < pairs.size(); ++index) {
            check(parallel[index].trace == pairs[index].trace &&
                      parallel[index].curve_order == pairs[index].curve_order &&
                      parallel[index].twist_order == pairs[index].twist_order &&
                      parallel[index].curve_smooth_part.value ==
                          pairs[index].curve_smooth_part.value &&
                      parallel[index].twist_smooth_part.value ==
                          pairs[index].twist_smooth_part.value,
                  "concurrent shared-cache extraction matches serial result");
        }
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

void test_grouped_curve_twist_oracle() {
    // The production opportunity is to combine trace sets from several
    // concurrently completed SEA curves before scanning the shared primorial.
    // Keep this oracle small: it proves that flattening and restoring request
    // boundaries is exactly equivalent to independent curve/twist extraction.
    const auto engine = oneshotsea::ExactSmoothEngine::build(
        101, {.thread_count = 2, .max_orders_per_batch = 64});
    const std::vector<mpz_class> first = {-10, 0};
    const std::vector<mpz_class> empty;
    const std::vector<mpz_class> third = {10, -2, 7};
    const std::array<std::span<const mpz_class>, 3> groups = {
        std::span<const mpz_class>(first),
        std::span<const mpz_class>(empty),
        std::span<const mpz_class>(third),
    };

    const auto grouped = engine.extract_curve_twist_groups(groups);
    check(grouped.size() == groups.size(),
          "grouped curve/twist extraction preserves request count");
    check(grouped[0].size() == first.size() && grouped[1].empty() &&
              grouped[2].size() == third.size(),
          "grouped curve/twist extraction preserves request boundaries");

    for (std::size_t group_index = 0; group_index < groups.size();
         ++group_index) {
        const auto independent =
            engine.extract_curve_twist(groups[group_index]);
        check(grouped[group_index].size() == independent.size(),
              "grouped request size matches independent extraction");
        for (std::size_t index = 0; index < independent.size(); ++index) {
            const auto& actual = grouped[group_index][index];
            const auto& expected = independent[index];
            check(actual.trace == expected.trace &&
                      actual.curve_order == expected.curve_order &&
                      actual.twist_order == expected.twist_order &&
                      actual.curve_smooth_part.value ==
                          expected.curve_smooth_part.value &&
                      actual.twist_smooth_part.value ==
                          expected.twist_smooth_part.value,
                  "grouped extraction matches independent exact oracle");
        }
    }

    check(engine.extract_curve_twist_groups(
              std::span<const std::span<const mpz_class>>{}).empty(),
          "grouped curve/twist extraction accepts no requests");
}

void check_curve_twist_results(
    const std::vector<oneshotsea::CurveTwistSmoothParts>& actual,
    const std::vector<oneshotsea::CurveTwistSmoothParts>& expected,
    const std::string& message) {
    check(actual.size() == expected.size(), message + " size");
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        check(actual[index].trace == expected[index].trace &&
                  actual[index].curve_order == expected[index].curve_order &&
                  actual[index].twist_order == expected[index].twist_order &&
                  actual[index].curve_smooth_part.value ==
                      expected[index].curve_smooth_part.value &&
                  actual[index].twist_smooth_part.value ==
                      expected[index].twist_smooth_part.value,
              message);
    }
}

bool wait_for_submitted_requests(
    const oneshotsea::ExactSmoothBatchCoordinator& coordinator,
    std::uint64_t target) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (coordinator.telemetry().submitted_requests < target) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

void test_no_delay_coordinator_forced_queue_and_cap() {
    const auto engine = oneshotsea::ExactSmoothEngine::build(
        101, {.thread_count = 1, .max_orders_per_batch = 4});
    const std::array<std::array<mpz_class, 1>, 4> traces = {{
        {-10},
        {-2},
        {7},
        {10},
    }};
    std::array<std::vector<oneshotsea::CurveTwistSmoothParts>, 4> expected;
    for (std::size_t index = 0U; index < traces.size(); ++index) {
        expected[index] = engine.extract_curve_twist(traces[index]);
    }

    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool first_started = false;
    bool release_first = false;
    std::vector<std::pair<std::size_t, std::size_t>> observed_batches;
    oneshotsea::ExactSmoothBatchCoordinator coordinator(
        engine,
        {.batch_start_callback =
             [&](std::size_t requests, std::size_t orders) {
                 std::unique_lock<std::mutex> lock(gate_mutex);
                 observed_batches.emplace_back(requests, orders);
                 if (!first_started) {
                     first_started = true;
                     gate_changed.notify_all();
                     gate_changed.wait(lock, [&] { return release_first; });
                 }
             }});

    std::array<std::future<std::vector<oneshotsea::CurveTwistSmoothParts>>, 4>
        futures;
    futures[0] = std::async(std::launch::async, [&] {
        return coordinator.extract_curve_twist(traces[0]);
    });
    bool scan_started = false;
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        scan_started = gate_changed.wait_for(
            lock, std::chrono::seconds(5), [&] { return first_started; });
    }

    bool all_queued = scan_started;
    for (std::size_t index = 1U; index < futures.size(); ++index) {
        futures[index] = std::async(std::launch::async, [&, index] {
            return coordinator.extract_curve_twist(traces[index]);
        });
        all_queued = all_queued &&
                     wait_for_submitted_requests(coordinator, index + 1U);
    }
    {
        const std::lock_guard<std::mutex> lock(gate_mutex);
        release_first = true;
    }
    gate_changed.notify_all();

    check(scan_started, "coordinator starts the first request without delay");
    check(all_queued, "requests queue while one exact scan is active");
    for (std::size_t index = 0U; index < futures.size(); ++index) {
        check(futures[index].wait_for(std::chrono::seconds(5)) ==
                  std::future_status::ready,
              "coordinator completes every queued request without starvation");
        check_curve_twist_results(futures[index].get(), expected[index],
                                  "coordinated exact result matches oracle");
    }

    const auto telemetry = coordinator.telemetry();
    check(telemetry.submitted_requests == 4U &&
              telemetry.completed_requests == 4U &&
              telemetry.failed_requests == 0U &&
              telemetry.cancelled_requests == 0U,
          "coordinator request telemetry");
    check(telemetry.coordinator_batches == 3U &&
              telemetry.successful_cache_scan_chunks == 3U &&
              telemetry.max_queued_requests == 3U &&
              telemetry.max_requests_per_batch == 2U &&
              telemetry.max_orders_per_successful_scan_chunk == 4U,
          "coordinator batches queued requests up to the exact scan cap");
    check(telemetry.successful_scan_chunks_by_order_count.size() == 2U &&
              telemetry.successful_scan_chunks_by_order_count[0].order_count ==
                  2U &&
              telemetry.successful_scan_chunks_by_order_count[0].scan_chunks ==
                  2U &&
              telemetry.successful_scan_chunks_by_order_count[1].order_count ==
                  4U &&
              telemetry.successful_scan_chunks_by_order_count[1].scan_chunks ==
                  1U,
          "coordinator exposes exact scan-size telemetry");
    {
        const std::lock_guard<std::mutex> lock(gate_mutex);
        check(observed_batches ==
                  std::vector<std::pair<std::size_t, std::size_t>>{
                      {1U, 2U}, {2U, 4U}, {1U, 2U}},
              "coordinator removes queued request groups in FIFO cap batches");
    }
}

void test_coordinator_oversized_request_scan_splits() {
    const auto engine = oneshotsea::ExactSmoothEngine::build(
        101, {.thread_count = 1, .max_orders_per_batch = 4});
    const std::array<mpz_class, 3> traces = {-10, 0, 10};
    const auto expected = engine.extract_curve_twist(traces);
    oneshotsea::ExactSmoothBatchCoordinator coordinator(engine);
    check_curve_twist_results(coordinator.extract_curve_twist(traces), expected,
                              "oversized coordinated request exact oracle");
    const auto telemetry = coordinator.telemetry();
    check(telemetry.coordinator_batches == 1U &&
              telemetry.successful_cache_scan_chunks == 2U &&
              telemetry.successful_scan_chunks_by_order_count.size() == 2U &&
              telemetry.successful_scan_chunks_by_order_count[0].order_count ==
                  2U &&
              telemetry.successful_scan_chunks_by_order_count[0].scan_chunks ==
                  1U &&
              telemetry.successful_scan_chunks_by_order_count[1].order_count ==
                  4U &&
              telemetry.successful_scan_chunks_by_order_count[1].scan_chunks ==
                  1U,
          "oversized request reports cap-respecting exact scan splits");
}

void test_coordinator_exception_recovery_and_cancellation() {
    const auto engine = oneshotsea::ExactSmoothEngine::build(
        101, {.thread_count = 1, .max_orders_per_batch = 4});
    const std::array<mpz_class, 1> trace = {-10};
    const auto expected = engine.extract_curve_twist(trace);

    const auto failing_engine = oneshotsea::ExactSmoothEngine::build(
        101,
        {.thread_count = 1,
         .max_orders_per_batch = 4,
         .max_root_auxiliary_bytes = 1U});
    oneshotsea::ExactSmoothBatchCoordinator failing(failing_engine);
    expect_exception<std::invalid_argument>(
        [&] { (void)failing.extract_curve_twist(trace); },
        "coordinator propagates an underlying exact extraction failure");
    const auto failed_extraction = failing.telemetry();
    check(failed_extraction.failed_requests == 1U &&
              failed_extraction.successful_cache_scan_chunks == 0U &&
              failed_extraction.successful_scan_chunks_by_order_count.empty() &&
              failed_extraction.max_orders_per_successful_scan_chunk == 0U,
          "failed exact extraction records no successful scan chunks");

    std::atomic<unsigned> starts = 0U;
    oneshotsea::ExactSmoothBatchCoordinator recovering(
        engine,
        {.batch_start_callback =
             [&](std::size_t, std::size_t) {
                 if (starts.fetch_add(1U) == 0U) {
                     throw std::runtime_error("injected batch failure");
                 }
             }});
    expect_exception<std::runtime_error>(
        [&] { (void)recovering.extract_curve_twist(trace); },
        "coordinator propagates a batch exception to its request");
    const auto failed = recovering.telemetry();
    check(failed.failed_requests == 1U &&
              failed.successful_cache_scan_chunks == 0U &&
              failed.successful_scan_chunks_by_order_count.empty() &&
              failed.max_orders_per_successful_scan_chunk == 0U,
          "failed extraction does not claim a successful cache scan");
    check_curve_twist_results(recovering.extract_curve_twist(trace), expected,
                              "coordinator continues after batch exception");
    const auto recovered = recovering.telemetry();
    check(recovered.failed_requests == 1U &&
              recovered.completed_requests == 1U &&
              recovered.coordinator_batches == 2U &&
              recovered.successful_cache_scan_chunks == 1U,
          "coordinator exception telemetry distinguishes failed scans");

    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool active = false;
    bool release = false;
    oneshotsea::ExactSmoothBatchCoordinator cancellable(
        engine,
        {.batch_start_callback =
             [&](std::size_t, std::size_t) {
                 std::unique_lock<std::mutex> lock(gate_mutex);
                 if (!active) {
                     active = true;
                     gate_changed.notify_all();
                     gate_changed.wait(lock, [&] { return release; });
                 }
             }});
    auto active_future = std::async(std::launch::async, [&] {
        return cancellable.extract_curve_twist(trace);
    });
    bool active_started = false;
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        active_started = gate_changed.wait_for(
            lock, std::chrono::seconds(5), [&] { return active; });
    }
    auto queued_future = std::async(std::launch::async, [&] {
        return cancellable.extract_curve_twist(trace);
    });
    const bool queued =
        active_started && wait_for_submitted_requests(cancellable, 2U);
    cancellable.cancel();
    const bool queued_cancelled =
        queued_future.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    {
        const std::lock_guard<std::mutex> lock(gate_mutex);
        release = true;
    }
    gate_changed.notify_all();

    check(active_started && queued,
          "cancellation fixture has one active and one queued request");
    check(queued_cancelled,
          "cancellation releases queued callers while active GMP work finishes");
    expect_exception<std::runtime_error>(
        [&] { (void)queued_future.get(); },
        "queued request receives cancellation exception");
    check_curve_twist_results(active_future.get(), expected,
                              "active request finishes during cancellation");
    expect_exception<std::runtime_error>(
        [&] { (void)cancellable.extract_curve_twist(trace); },
        "cancelled coordinator rejects new requests");
    const auto cancelled = cancellable.telemetry();
    check(cancelled.submitted_requests == 2U &&
              cancelled.completed_requests == 1U &&
              cancelled.cancelled_requests == 1U,
          "coordinator cancellation telemetry");
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
        test_bounded_reducer_differential();
        test_small_differential_and_batching(temporary);
        test_curve_twist_and_evidence_lifetime();
        test_grouped_curve_twist_oracle();
        test_no_delay_coordinator_forced_queue_and_cap();
        test_coordinator_oversized_request_scan_splits();
        test_coordinator_exception_recovery_and_cancellation();
        test_p125_factored_segment_fixture();
        std::cout << "all exact-smooth tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "exact-smooth test failed: " << error.what() << '\n';
        return 1;
    }
}
