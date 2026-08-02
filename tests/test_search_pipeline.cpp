#include "oneshotsea/search_pipeline.hpp"
#include "oneshotsea/weber_table_trust.hpp"
#include "oneshotsea/x1_11_probe.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path() /
                ("oneshotsea-search-pipeline-" +
                 std::to_string(static_cast<long long>(::getpid())) + "-" +
                 std::to_string(stamp));
        std::filesystem::create_directory(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void test_sha256_fixtures() {
    TemporaryDirectory temporary;
    const std::filesystem::path empty = temporary.path() / "empty";
    const std::filesystem::path abc = temporary.path() / "abc";
    const std::filesystem::path fox = temporary.path() / "fox";
    std::ofstream(empty, std::ios::binary);
    {
        std::ofstream output(abc, std::ios::binary);
        output << "abc";
    }
    {
        std::ofstream output(fox, std::ios::binary);
        output << "The quick brown fox jumps over the lazy dog";
    }
    check(oneshotsea::sha256_file(empty) ==
              "e3b0c44298fc1c149afbf4c8996fb924"
              "27ae41e4649b934ca495991b7852b855",
          "SHA-256 empty fixture");
    check(oneshotsea::sha256_file(abc) ==
              "ba7816bf8f01cfea414140de5dae2223"
              "b00361a396177a9cb410ff61f20015ad",
          "SHA-256 abc fixture");
    check(oneshotsea::sha256_file(fox) ==
              "d7a8fbb307d7809469ca9abcb0082e4f"
              "8d5651e46d3cdb762d02d0bf37c9e592",
          "SHA-256 regular-file fixture");

    const std::filesystem::path source =
        "third_party/oneshot_primality_proofs/voneshot.py";
    const std::string first = oneshotsea::sha256_file(source);
    const std::string second = oneshotsea::sha256_file(source);
    check(first.size() == 64U && first == second,
          "SHA-256 regular-file fixture is stable");

    const std::vector<std::pair<std::size_t, std::string>> boundary_vectors = {
        {55U, "9f4390f8d30c2dd92ec9f095b65e2b9a"
              "e9b0a925a5258e241c9f1e910f734318"},
        {56U, "b35439a4ac6f0948b6d6f9e3c6af0f5"
              "f590ce20f1bde7090ef7970686ec6738a"},
        {63U, "7d3e74a05d7db15bce4ad9ec0658ea98"
              "e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64U, "ffe054fe7ae0cb6dc65c3af9b61d5209"
              "f439851db43d0ba5997337df154668eb"},
        {65U, "635361c48bb9eab14198e76ea8ab7f1a"
              "41685d6ad62aa9146d301d4f17eb0ae0"},
        {128U, "6836cf13bac400e9105071cd6af47084d"
               "facad4e5e302c94bfed24e013afb73e"},
    };
    for (const auto& [size, expected] : boundary_vectors) {
        const std::filesystem::path fixture =
            temporary.path() / ("a-" + std::to_string(size));
        {
            std::ofstream output(fixture, std::ios::binary);
            output << std::string(size, 'a');
        }
        check(oneshotsea::sha256_file(fixture) == expected,
              "SHA-256 block-boundary fixture " + std::to_string(size));
    }
}

void test_weber_table_authentication() {
    const std::filesystem::path source = "data/modpoly/weber_f";
    oneshotsea::authenticate_trusted_weber_table_set(source);

    TemporaryDirectory temporary;
    const std::filesystem::path copy = temporary.path() / "weber_f";
    std::filesystem::create_directory(copy);
    std::filesystem::copy_file(source / "MANIFEST.json",
                               copy / "MANIFEST.json");
    for (const auto& entry : std::filesystem::directory_iterator(source)) {
        if (entry.path().filename().string().starts_with("phi_") &&
            entry.path().extension() == ".txt") {
            std::filesystem::create_symlink(
                std::filesystem::absolute(entry.path()),
                copy / entry.path().filename());
        }
    }
    oneshotsea::authenticate_trusted_weber_table_set(copy);

    std::filesystem::remove(copy / "phi_5.txt");
    bool rejected_missing = false;
    try {
        oneshotsea::authenticate_trusted_weber_table_set(copy);
    } catch (const std::runtime_error&) {
        rejected_missing = true;
    }
    check(rejected_missing,
          "trusted Weber authentication rejects missing files");

    std::filesystem::create_symlink(
        std::filesystem::absolute(source / "phi_5.txt"), copy / "phi_5.txt");
    {
        std::ofstream extra(copy / "phi_409.txt");
        extra << "410 0 1\n";
    }
    bool rejected_extra = false;
    try {
        oneshotsea::authenticate_trusted_weber_table_set(copy);
    } catch (const std::runtime_error&) {
        rejected_extra = true;
    }
    check(rejected_extra,
          "trusted Weber authentication rejects extra files");

    std::filesystem::remove(copy / "phi_409.txt");
    std::filesystem::remove(copy / "phi_5.txt");
    std::filesystem::copy_file(source / "phi_5.txt", copy / "phi_5.txt");
    {
        std::ofstream altered(copy / "phi_5.txt", std::ios::app);
        altered << "\n";
    }
    bool rejected_altered = false;
    try {
        oneshotsea::authenticate_trusted_weber_table_set(copy);
    } catch (const std::runtime_error&) {
        rejected_altered = true;
    }
    check(rejected_altered,
          "trusted Weber authentication rejects altered files");
}

oneshotsea::SearchPipelineConfig small_config() {
    oneshotsea::SearchPipelineConfig config;
    config.prime = 101;
    // Under the certificate-compatible Montgomery prefilter, index one yields
    // the canonical fixture asserted below.
    config.seed = 4;
    config.table_directory = "data/modpoly/weber_f";
    config.max_level = 11;
    config.early_trace_cap = 16;
    config.assembly_attempts = 400;
    config.certificate_seed = 1;
    config.canonical_verifier =
        "third_party/oneshot_primality_proofs/voneshot.py";
    config.python_executable = oneshotsea::resolve_executable_path("python3");
    return config;
}

void test_small_prime_resume_and_canonical_verification() {
    TemporaryDirectory temporary;
    const std::filesystem::path smooth_cache =
        temporary.path() / "smooth.cache";
    const std::filesystem::path checkpoint =
        temporary.path() / "checkpoint.json";
    const std::filesystem::path progress = temporary.path() / "progress.ndjson";

    oneshotsea::SearchPipelineConfig config = small_config();
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime);
    smooth.save(smooth_cache);
    const std::string smooth_sha = oneshotsea::sha256_file(smooth_cache);
    const std::string verifier_sha =
        oneshotsea::sha256_file(config.canonical_verifier);
    const oneshotsea::SearchIdentity identity = oneshotsea::make_search_identity(
        config, {1, 2}, 0, 1, smooth_sha, verifier_sha, "pipeline-test-v1");
    config.expected_schedule_sha256 = identity.schedule_sha256;
    config.expected_table_manifest_sha256 =
        identity.table_manifest_sha256;
    oneshotsea::SearchState state(identity);

    oneshotsea::SearchPipelineRunOptions first;
    first.max_curves = 0;
    first.checkpoint_path = checkpoint;
    first.progress_path = progress;
    first.certificate_path = temporary.path() / "certificate.txt";
    const auto first_result = oneshotsea::run_search_pipeline(
        config, smooth, state, first);
    check(first_result.curves_processed == 0U &&
              !first_result.verified.has_value() && state.next_index() == 1U,
          "zero-curve first chunk leaves the initial resume cursor");
    oneshotsea::save_search_checkpoint(state, checkpoint);
    const std::filesystem::path precertificate_checkpoint =
        temporary.path() / "precertificate.json";
    std::filesystem::copy_file(checkpoint, precertificate_checkpoint);

    oneshotsea::SearchState resumed =
        oneshotsea::load_search_checkpoint(checkpoint, identity);
    std::size_t canonical_calls = 0;
    const auto canonical = [&](const oneshotsea::MontgomeryCertificate& cert) {
        ++canonical_calls;
        return oneshotsea::verify_with_canonical_voneshot(
            cert, config.canonical_verifier, config.python_executable);
    };
    oneshotsea::SearchPipelineRunOptions second = first;
    second.max_curves = 1;
    const auto second_result = oneshotsea::run_search_pipeline(
        config, smooth, resumed, second, {}, canonical);
    check(second_result.curves_processed == 1U &&
              second_result.verified.has_value(),
          "resumed search finds deterministic certificate");
    check(canonical_calls == 1U,
          "pipeline invoked the unmodified canonical verifier before success");
    check(second_result.verified->certificate->line() == "101 35 25 28",
          "small-prime deterministic certificate fixture");
    check(resumed.next_index() == 2U &&
              resumed.counters().certificates_found == 1U,
          "verified result is checkpointed exactly once");
    check(std::filesystem::is_regular_file(
              first.certificate_path.string() + ".meta.json"),
          "certificate identity/index metadata is durably published");

    oneshotsea::SearchState crash_window =
        oneshotsea::load_search_checkpoint(precertificate_checkpoint, identity);
    oneshotsea::SearchPipelineRunOptions crash_recovery = second;
    crash_recovery.checkpoint_path = precertificate_checkpoint;
    const auto crash_result = oneshotsea::run_search_pipeline(
        config, smooth, crash_window, crash_recovery, {}, canonical);
    check(crash_result.curves_processed == 0U &&
              crash_result.verified.has_value() &&
              crash_window.next_index() == 2U &&
              crash_window.counters().certificates_found == 1U,
          "pre-certificate checkpoint recovers the metadata-bound winning index");

    oneshotsea::SearchState completed =
        oneshotsea::load_search_checkpoint(checkpoint, identity);
    const auto recovered_result = oneshotsea::run_search_pipeline(
        config, smooth, completed, second, {}, canonical);
    check(recovered_result.curves_processed == 0U &&
              recovered_result.verified.has_value() && canonical_calls == 3U,
          "resume revalidates durable certificate without advancing search");

    std::ifstream events(progress);
    std::size_t event_count = 0;
    std::string line;
    while (std::getline(events, line)) {
        check(line.find("\"heuristic\":false") != std::string::npos,
              "event distinguishes disabled heuristic rejection");
        ++event_count;
    }
    check(event_count == 1U, "one NDJSON event per completed curve");
}

void test_parallel_curve_ordering_and_shared_checkpoint() {
    TemporaryDirectory temporary;
    oneshotsea::SearchPipelineConfig config = small_config();
    // This fixture has four consecutive terminal curves at level eleven when
    // the injected canonical verifier rejects each valid candidate.
    config.seed = 11;
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime,
                                             {.thread_count = 1});
    const std::filesystem::path smooth_cache =
        temporary.path() / "parallel-smooth.cache";
    smooth.save(smooth_cache);
    const oneshotsea::SearchIdentity identity = oneshotsea::make_search_identity(
        config, {0, 4}, 0, 1, oneshotsea::sha256_file(smooth_cache),
        oneshotsea::sha256_file(config.canonical_verifier),
        "parallel-pipeline-test-v1");
    config.expected_schedule_sha256 = identity.schedule_sha256;
    config.expected_table_manifest_sha256 = identity.table_manifest_sha256;

    struct Observation {
        std::vector<std::uint64_t> indices;
        std::vector<oneshotsea::SearchCurveStatus> statuses;
        oneshotsea::SearchCounters counters;
        std::uint64_t next_index = 0;
    };
    const auto run = [&](std::size_t curve_threads,
                         const std::string& name,
                         bool audit_live_levels) {
        oneshotsea::SearchState state(identity);
        oneshotsea::SearchPipelineRunOptions options;
        options.max_curves = 4;
        options.curve_threads = curve_threads;
        options.checkpoint_every = 2;
        options.checkpoint_path = temporary.path() / (name + ".checkpoint");
        options.progress_path = temporary.path() / (name + ".ndjson");
        options.certificate_path = temporary.path() / (name + ".cert");

        std::atomic<unsigned> active_callbacks{0};
        std::atomic<unsigned> maximum_active_callbacks{0};
        std::map<std::uint64_t,
                 std::vector<std::pair<std::size_t, std::uint64_t>>>
            live_levels;
        std::map<std::uint64_t,
                 std::vector<std::pair<std::size_t, std::uint64_t>>>
            report_levels;
        if (audit_live_levels) {
            options.sea_level_callback =
                [&](std::uint64_t index,
                    const oneshotsea::SearchSeaLevelTiming& level) {
                    const unsigned active =
                        active_callbacks.fetch_add(1U) + 1U;
                    unsigned maximum = maximum_active_callbacks.load();
                    while (maximum < active &&
                           !maximum_active_callbacks.compare_exchange_weak(
                               maximum, active)) {
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    live_levels[index].emplace_back(level.pass, level.ell);
                    active_callbacks.fetch_sub(1U);
                };
        }

        Observation observation;
        const auto result = oneshotsea::run_search_pipeline(
            config, smooth, state, options,
            [&](const oneshotsea::SearchCurveReport& report,
                const oneshotsea::SearchState&) {
                observation.indices.push_back(report.global_index);
                observation.statuses.push_back(report.status);
                auto& levels = report_levels[report.global_index];
                for (const auto& level : report.sea_level_timings) {
                    levels.emplace_back(level.pass, level.ell);
                }
            },
            [](const oneshotsea::MontgomeryCertificate&) { return false; });
        check(result.curves_processed == 4U &&
                  result.exhausted_assigned_range &&
                  !result.verified.has_value(),
              "four-curve fixture exhausts under rejecting verifier");
        observation.counters = state.counters();
        observation.next_index = state.next_index();
        const oneshotsea::SearchState saved =
            oneshotsea::load_search_checkpoint(options.checkpoint_path,
                                               identity);
        check(saved.next_index() == state.next_index() &&
                  saved.counters() == state.counters(),
              "parallel coordinator publishes the complete ordered checkpoint");
        if (audit_live_levels) {
            check(maximum_active_callbacks.load() == 1U,
                  "parallel live SEA callbacks are serialized atomically");
            check(live_levels == report_levels,
                  "each interleaved live index subsequence stays in SEA order");
        }
        return observation;
    };

    const Observation serial = run(1U, "serial", false);
    const Observation parallel = run(2U, "parallel", true);
    check(parallel.indices == std::vector<std::uint64_t>({0, 1, 2, 3}) &&
              parallel.indices == serial.indices &&
              parallel.statuses == serial.statuses &&
              parallel.counters == serial.counters &&
              parallel.next_index == serial.next_index,
          "K=2 preserves K=1 report order, outcomes, counters, and cursor");

    oneshotsea::SearchState invalid(identity);
    oneshotsea::SearchPipelineRunOptions invalid_options;
    invalid_options.curve_threads = 0;
    bool rejected_zero = false;
    try {
        (void)oneshotsea::run_search_pipeline(
            config, smooth, invalid, invalid_options);
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    check(rejected_zero, "zero curve concurrency is rejected");
}

void test_parallel_stop_discards_later_reports() {
    TemporaryDirectory temporary;
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(101, {.thread_count = 1});
    const std::filesystem::path smooth_cache =
        temporary.path() / "stop-smooth.cache";
    smooth.save(smooth_cache);
    const std::string smooth_sha = oneshotsea::sha256_file(smooth_cache);

    // Index zero is an implementation limit while the already-launched index
    // one finds a canonical certificate. The coordinator must drain but never
    // publish or checkpoint the later success across the unchanged cursor.
    oneshotsea::SearchPipelineConfig limited = small_config();
    const oneshotsea::SearchIdentity limited_identity =
        oneshotsea::make_search_identity(
            limited, {0, 2}, 0, 1, smooth_sha,
            oneshotsea::sha256_file(limited.canonical_verifier),
            "parallel-stop-limit-v1");
    limited.expected_schedule_sha256 = limited_identity.schedule_sha256;
    limited.expected_table_manifest_sha256 =
        limited_identity.table_manifest_sha256;
    oneshotsea::SearchState limited_state(limited_identity);
    oneshotsea::SearchPipelineRunOptions limited_options;
    limited_options.max_curves = 2;
    limited_options.curve_threads = 2;
    limited_options.checkpoint_path = temporary.path() / "limit.checkpoint";
    limited_options.progress_path = temporary.path() / "limit.ndjson";
    limited_options.certificate_path = temporary.path() / "limit.cert";
    std::atomic<unsigned> later_verifications{0};
    std::atomic<bool> saw_later_level{false};
    limited_options.sea_level_callback =
        [&](std::uint64_t index,
            const oneshotsea::SearchSeaLevelTiming&) {
            if (index == 1U) {
                saw_later_level = true;
            }
        };
    std::vector<std::uint64_t> limited_reports;
    const auto limited_result = oneshotsea::run_search_pipeline(
        limited, smooth, limited_state, limited_options,
        [&](const oneshotsea::SearchCurveReport& report,
            const oneshotsea::SearchState&) {
            limited_reports.push_back(report.global_index);
        },
        [&](const oneshotsea::MontgomeryCertificate&) {
            ++later_verifications;
            return true;
        });
    check(limited_result.curves_processed == 0U &&
              !limited_result.verified.has_value() &&
              limited_state.next_index() == 0U &&
              limited_reports == std::vector<std::uint64_t>{0U} &&
              saw_later_level.load() && later_verifications.load() == 1U &&
              !std::filesystem::exists(limited_options.certificate_path),
          "earlier implementation limit drains and discards later certificate");

    // Conversely, index one wins while the launched index two reaches an
    // implementation limit. Only the lowest winning index may advance and
    // publish, regardless of the later report's completion order.
    oneshotsea::SearchIdentity winning_identity =
        oneshotsea::make_search_identity(
            limited, {1, 3}, 0, 1, smooth_sha,
            oneshotsea::sha256_file(limited.canonical_verifier),
            "parallel-stop-certificate-v1");
    limited.expected_schedule_sha256 = winning_identity.schedule_sha256;
    limited.expected_table_manifest_sha256 =
        winning_identity.table_manifest_sha256;
    oneshotsea::SearchState winning_state(winning_identity);
    oneshotsea::SearchPipelineRunOptions winning_options;
    winning_options.max_curves = 2;
    winning_options.curve_threads = 2;
    winning_options.checkpoint_path = temporary.path() / "winning.checkpoint";
    winning_options.progress_path = temporary.path() / "winning.ndjson";
    winning_options.certificate_path = temporary.path() / "winning.cert";
    std::atomic<bool> saw_discarded_level{false};
    winning_options.sea_level_callback =
        [&](std::uint64_t index,
            const oneshotsea::SearchSeaLevelTiming&) {
            if (index == 2U) {
                saw_discarded_level = true;
            }
        };
    std::vector<std::uint64_t> winning_reports;
    const auto winning_result = oneshotsea::run_search_pipeline(
        limited, smooth, winning_state, winning_options,
        [&](const oneshotsea::SearchCurveReport& report,
            const oneshotsea::SearchState&) {
            winning_reports.push_back(report.global_index);
        },
        [](const oneshotsea::MontgomeryCertificate&) { return true; });
    check(winning_result.curves_processed == 1U &&
              winning_result.verified.has_value() &&
              winning_result.verified->global_index == 1U &&
              winning_state.next_index() == 2U &&
              winning_reports == std::vector<std::uint64_t>{1U} &&
              saw_discarded_level.load() &&
              std::filesystem::is_regular_file(
                  winning_options.certificate_path),
          "earlier certificate drains and discards later implementation report");
}

void test_sea_level_limit_does_not_advance_cursor() {
    TemporaryDirectory temporary;
    oneshotsea::SearchPipelineConfig limited = small_config();
    limited.max_level = 5;
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(limited.prime);
    const std::filesystem::path smooth_cache =
        temporary.path() / "smooth.cache";
    smooth.save(smooth_cache);
    const std::string smooth_sha = oneshotsea::sha256_file(smooth_cache);
    const std::string verifier_sha =
        oneshotsea::sha256_file(limited.canonical_verifier);
    const oneshotsea::SearchIdentity limited_identity =
        oneshotsea::make_search_identity(
            limited, {1, 2}, 0, 1, smooth_sha, verifier_sha,
            "level-limit-test-v1");
    limited.expected_schedule_sha256 = limited_identity.schedule_sha256;
    limited.expected_table_manifest_sha256 =
        limited_identity.table_manifest_sha256;
    oneshotsea::SearchState limited_state(limited_identity);
    oneshotsea::SearchPipelineRunOptions limited_options;
    limited_options.max_curves = 1;
    limited_options.checkpoint_path = temporary.path() / "limited.json";
    limited_options.progress_path = temporary.path() / "limited.ndjson";
    limited_options.certificate_path = temporary.path() / "limited.cert";
    std::size_t reports = 0;
    oneshotsea::SearchCurveStatus observed =
        oneshotsea::SearchCurveStatus::verified_certificate;
    oneshotsea::SearchPipelineRunResult limited_result;
    try {
        limited_result = oneshotsea::run_search_pipeline(
            limited, smooth, limited_state, limited_options,
            [&](const oneshotsea::SearchCurveReport& report,
                const oneshotsea::SearchState& current) {
                ++reports;
                observed = report.status;
                check(current.next_index() == 1U,
                      "level-limit report observes unchanged cursor");
            });
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("limited SEA regression: ") +
                                 error.what());
    }
    check(limited_result.curves_processed == 0U && reports == 1U &&
              observed == oneshotsea::SearchCurveStatus::sea_level_limit,
          "level-five SEA limit is reported but not completed");
    check(limited_state.next_index() == 1U &&
              limited_state.counters().curves_attempted == 0U,
          "implementation limit does not mutate search counters/cursor");
    const oneshotsea::SearchState checkpointed =
        oneshotsea::load_search_checkpoint(
            limited_options.checkpoint_path, limited_identity);
    check(checkpointed.next_index() == 1U &&
              checkpointed.counters().curves_attempted == 0U,
          "unchanged cursor is atomically checkpointed");
    {
        std::ifstream progress(limited_options.progress_path);
        std::string event;
        std::getline(progress, event);
        check(event.find("\"status\":\"sea_level_limit\"") !=
                  std::string::npos &&
                  event.find("\"next_index\":\"1\"") !=
                  std::string::npos,
              "progress preserves implementation-limit evidence and cursor");
    }

    oneshotsea::SearchPipelineConfig sufficient = small_config();
    const oneshotsea::SearchIdentity sufficient_identity =
        oneshotsea::make_search_identity(
            sufficient, {1, 2}, 0, 1, smooth_sha, verifier_sha,
            "level-limit-test-v1");
    sufficient.expected_schedule_sha256 = sufficient_identity.schedule_sha256;
    sufficient.expected_table_manifest_sha256 =
        sufficient_identity.table_manifest_sha256;
    oneshotsea::SearchState sufficient_state(sufficient_identity);
    oneshotsea::SearchPipelineRunOptions sufficient_options;
    sufficient_options.max_curves = 1;
    sufficient_options.checkpoint_path = temporary.path() / "sufficient.json";
    sufficient_options.progress_path = temporary.path() / "sufficient.ndjson";
    sufficient_options.certificate_path = temporary.path() / "sufficient.cert";
    oneshotsea::SearchPipelineRunResult sufficient_result;
    oneshotsea::SearchCurveStatus sufficient_status =
        oneshotsea::SearchCurveStatus::sea_level_limit;
    try {
        sufficient_result = oneshotsea::run_search_pipeline(
            sufficient, smooth, sufficient_state, sufficient_options,
            [&](const oneshotsea::SearchCurveReport& report,
                const oneshotsea::SearchState&) {
                sufficient_status = report.status;
            });
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("sufficient SEA regression: ") +
                                 error.what());
    }
    check(sufficient_result.verified.has_value() &&
              sufficient_result.verified->global_index == 1U &&
              sufficient_result.verified->certificate->line() ==
                  "101 35 25 28",
          std::string("same curve succeeds when sufficient SEA levels are available; status=") +
              oneshotsea::search_curve_status_name(sufficient_status));
}

void test_worker_partition_is_identity_bound() {
    TemporaryDirectory temporary;
    oneshotsea::SearchPipelineConfig config = small_config();
    config.sea_threads = 1;
    const std::string digest(64U, 'a');
    const auto identity = oneshotsea::make_search_identity(
        config, {10, 21}, 2, 3, digest, digest, "pipeline-test-v1");
    config.sea_threads = 8;
    const auto differently_threaded = oneshotsea::make_search_identity(
        config, {10, 21}, 2, 3, digest, digest, "pipeline-test-v1");
    check(identity.range == oneshotsea::SearchRange{18, 21},
          "pipeline identity uses deterministic disjoint worker shard");
    check(identity.schedule_sha256.size() == 64U &&
              identity.table_manifest_sha256.size() == 64U,
          "pipeline identity binds schedule and table content");
    check(differently_threaded == identity,
          "SEA thread limit is a resumable resource setting, not an identity");

    const std::filesystem::path canonical_schedule =
        temporary.path() / "canonical-default-schedule.txt";
    {
        std::ostringstream canonical;
        canonical << "oneshotsea.search-schedule.v1\n"
                  << "curve_generator=weber-f-montgomery-filtered-v2\n"
                  << "trace_prior_policy="
                  << "weber-full-e2-mod4-if-validated-"
                     "x1-selected-group-divisor-point4-p5mod8-176-v2\n"
                  << "weber_source_lift_policy="
                     "generator-retained-unramified-singleton-v1\n"
                  << "sea=weber-reference-two-pass-classical-atkin-v2\n"
                  << "heuristic_rejection=disabled\n"
                  << "prime=" << config.prime << '\n'
                  << "max_level=" << config.max_level << '\n'
                  << "early_trace_cap=" << config.early_trace_cap << '\n'
                  << "assembly_attempts=" << config.assembly_attempts << '\n'
                  << "certificate_seed=" << config.certificate_seed << '\n'
                  << "python_executable_path=" << config.python_executable
                  << '\n'
                  << "python_executable_sha256="
                  << oneshotsea::sha256_file(config.python_executable) << '\n'
                  << "smooth_cache_sha256=" << digest << '\n'
                  << "canonical_verifier_sha256=" << digest << '\n';
        std::ofstream output(canonical_schedule, std::ios::binary);
        output << canonical.str();
    }
    check(identity.schedule_sha256 ==
              oneshotsea::sha256_file(canonical_schedule),
          "search schedule identity binds trace-prior and known-lift policy versions");

    const std::filesystem::path pre_source_lift_schedule =
        temporary.path() / "pre-source-lift-schedule.txt";
    {
        std::ostringstream canonical;
        canonical << "oneshotsea.search-schedule.v1\n"
                  << "curve_generator=weber-f-montgomery-filtered-v2\n"
                  << "trace_prior_policy="
                  << "weber-full-e2-mod4-if-validated-"
                     "x1-selected-group-divisor-point4-p5mod8-176-v2\n"
                  << "sea=weber-reference-two-pass-classical-atkin-v2\n"
                  << "heuristic_rejection=disabled\n"
                  << "prime=" << config.prime << '\n'
                  << "max_level=" << config.max_level << '\n'
                  << "early_trace_cap=" << config.early_trace_cap << '\n'
                  << "assembly_attempts=" << config.assembly_attempts << '\n'
                  << "certificate_seed=" << config.certificate_seed << '\n'
                  << "python_executable_path=" << config.python_executable
                  << '\n'
                  << "python_executable_sha256="
                  << oneshotsea::sha256_file(config.python_executable) << '\n'
                  << "smooth_cache_sha256=" << digest << '\n'
                  << "canonical_verifier_sha256=" << digest << '\n';
        std::ofstream output(pre_source_lift_schedule, std::ios::binary);
        output << canonical.str();
    }
    oneshotsea::SearchIdentity pre_source_lift_identity = identity;
    pre_source_lift_identity.schedule_sha256 =
        oneshotsea::sha256_file(pre_source_lift_schedule);
    check(pre_source_lift_identity.schedule_sha256 != identity.schedule_sha256,
          "known-source policy changes the production schedule hash");
    const std::filesystem::path pre_source_lift_checkpoint =
        temporary.path() / "pre-source-lift.json";
    oneshotsea::save_search_checkpoint(
        oneshotsea::SearchState(pre_source_lift_identity),
        pre_source_lift_checkpoint);
    bool pre_source_lift_rejected = false;
    try {
        (void)oneshotsea::load_search_checkpoint(
            pre_source_lift_checkpoint, identity);
    } catch (const oneshotsea::SearchCheckpointError&) {
        pre_source_lift_rejected = true;
    }
    check(pre_source_lift_rejected,
          "checkpoint rejects the pre-known-source schedule identity");

    const std::filesystem::path pre_point_four_176_schedule =
        temporary.path() / "pre-point-four-176-schedule.txt";
    {
        std::ostringstream canonical;
        canonical << "oneshotsea.search-schedule.v1\n"
                  << "curve_generator=weber-f-montgomery-filtered-v2\n"
                  << "trace_prior_policy="
                  << "weber-full-e2-mod4-if-validated-"
                     "x1-selected-group-divisor-v1\n"
                  << "weber_source_lift_policy="
                     "generator-retained-unramified-singleton-v1\n"
                  << "sea=weber-reference-two-pass-classical-atkin-v2\n"
                  << "heuristic_rejection=disabled\n"
                  << "prime=" << config.prime << '\n'
                  << "max_level=" << config.max_level << '\n'
                  << "early_trace_cap=" << config.early_trace_cap << '\n'
                  << "assembly_attempts=" << config.assembly_attempts << '\n'
                  << "certificate_seed=" << config.certificate_seed << '\n'
                  << "python_executable_path=" << config.python_executable
                  << '\n'
                  << "python_executable_sha256="
                  << oneshotsea::sha256_file(config.python_executable) << '\n'
                  << "smooth_cache_sha256=" << digest << '\n'
                  << "canonical_verifier_sha256=" << digest << '\n';
        std::ofstream output(pre_point_four_176_schedule, std::ios::binary);
        output << canonical.str();
    }
    oneshotsea::SearchIdentity pre_point_four_176_identity = identity;
    pre_point_four_176_identity.schedule_sha256 =
        oneshotsea::sha256_file(pre_point_four_176_schedule);
    check(pre_point_four_176_identity.schedule_sha256 !=
              identity.schedule_sha256,
          "point-four 176 prior changes the production schedule hash");
    const std::filesystem::path pre_point_four_176_checkpoint =
        temporary.path() / "pre-point-four-176.json";
    oneshotsea::save_search_checkpoint(
        oneshotsea::SearchState(pre_point_four_176_identity),
        pre_point_four_176_checkpoint);
    bool pre_point_four_176_rejected = false;
    try {
        (void)oneshotsea::load_search_checkpoint(
            pre_point_four_176_checkpoint, identity);
    } catch (const oneshotsea::SearchCheckpointError&) {
        pre_point_four_176_rejected = true;
    }
    check(pre_point_four_176_rejected,
          "checkpoint rejects the pre-point-four-176 schedule identity");

    const std::filesystem::path pre_policy_schedule =
        temporary.path() / "pre-trace-prior-schedule.txt";
    {
        std::ostringstream canonical;
        canonical << "oneshotsea.search-schedule.v1\n"
                  << "curve_generator=weber-f-montgomery-filtered-v2\n"
                  << "sea=weber-reference-two-pass-classical-atkin-v2\n"
                  << "heuristic_rejection=disabled\n"
                  << "prime=" << config.prime << '\n'
                  << "max_level=" << config.max_level << '\n'
                  << "early_trace_cap=" << config.early_trace_cap << '\n'
                  << "assembly_attempts=" << config.assembly_attempts << '\n'
                  << "certificate_seed=" << config.certificate_seed << '\n'
                  << "python_executable_path=" << config.python_executable
                  << '\n'
                  << "python_executable_sha256="
                  << oneshotsea::sha256_file(config.python_executable) << '\n'
                  << "smooth_cache_sha256=" << digest << '\n'
                  << "canonical_verifier_sha256=" << digest << '\n';
        std::ofstream output(pre_policy_schedule, std::ios::binary);
        output << canonical.str();
    }
    oneshotsea::SearchIdentity pre_policy_identity = identity;
    pre_policy_identity.schedule_sha256 =
        oneshotsea::sha256_file(pre_policy_schedule);
    check(pre_policy_identity.schedule_sha256 != identity.schedule_sha256,
          "trace-prior policy changes the production schedule hash");
    const std::filesystem::path pre_policy_checkpoint =
        temporary.path() / "pre-trace-prior.json";
    oneshotsea::save_search_checkpoint(
        oneshotsea::SearchState(pre_policy_identity),
        pre_policy_checkpoint);
    bool pre_policy_rejected = false;
    try {
        (void)oneshotsea::load_search_checkpoint(
            pre_policy_checkpoint, identity);
    } catch (const oneshotsea::SearchCheckpointError&) {
        pre_policy_rejected = true;
    }
    check(pre_policy_rejected,
          "checkpoint rejects the pre-trace-prior schedule identity");

    oneshotsea::SearchPipelineConfig x1_config = small_config();
    x1_config.curve_family = oneshotsea::SearchCurveFamily::x1_11;
    const auto x1_identity = oneshotsea::make_search_identity(
        x1_config, {10, 21}, 2, 3, digest, digest, "pipeline-test-v1");
    check(x1_identity.range == identity.range &&
              x1_identity.schedule_sha256 != identity.schedule_sha256,
          "curve family changes schedule identity without changing partition");

    x1_config.x1_require_point_four = true;
    const auto x1_point_four_identity = oneshotsea::make_search_identity(
        x1_config, {10, 21}, 2, 3, digest, digest, "pipeline-test-v1");
    check(x1_point_four_identity.range == identity.range &&
              x1_point_four_identity.schedule_sha256 !=
                  x1_identity.schedule_sha256,
          "X1 point-four requirement changes schedule identity only");

    const std::filesystem::path checkpoint = temporary.path() / "weber.json";
    oneshotsea::save_search_checkpoint(
        oneshotsea::SearchState(identity), checkpoint);
    bool mismatch_rejected = false;
    try {
        (void)oneshotsea::load_search_checkpoint(checkpoint, x1_identity);
    } catch (const oneshotsea::SearchCheckpointError&) {
        mismatch_rejected = true;
    }
    check(mismatch_rejected,
          "checkpoint rejects a mismatched curve-family schedule");
}

void test_x1_curve_family_enters_search_pipeline() {
    oneshotsea::SearchPipelineConfig config = small_config();
    config.prime = 157;
    config.seed = UINT64_C(0x7821058d55e0f265);
    config.curve_family = oneshotsea::SearchCurveFamily::x1_11;
    config.x1_require_point_four = true;
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime);

    const auto first = oneshotsea::process_search_curve(
        config, smooth, 7,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    const auto repeat = oneshotsea::process_search_curve(
        config, smooth, 7,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    check(first.global_index == 7U &&
              first.rejected_generator_samples ==
                  repeat.rejected_generator_samples &&
              first.status == repeat.status &&
              first.sea_levels == repeat.sea_levels &&
              first.initial_trace_count == repeat.initial_trace_count,
          "X1 family feeds the common pipeline deterministically");
    const auto twist_sample = oneshotsea::deterministic_x1_11_search_curve(
        config.prime, config.seed, 7, true);
    check(twist_sample.sample->selected_side ==
              oneshotsea::X111CanonicalSide::twist,
          "pinned X1 fixture exercises selected-twist semantics");
    const mpz_class twist_fixture_trace =
        config.prime + 1 - oneshotsea::count_points_bruteforce(
                               twist_sample.sample->pair.curve);
    check(first.trace_prior_modulus ==
              std::optional<std::uint64_t>(176U) &&
              first.trace_prior_residue ==
                  std::optional<std::uint64_t>(18U) &&
              first.exact_trace ==
                  std::optional<mpz_class>(twist_fixture_trace) &&
              twist_fixture_trace == 18 && first.sea_levels == 0U,
          "selected-twist divisor becomes the negated curve-trace prior");

    const std::string digest(64U, 'b');
    const auto identity = oneshotsea::make_search_identity(
        config, {7, 8}, 0, 1, digest, digest, "pipeline-test-v1");
    const std::string report_json = oneshotsea::search_curve_report_json(
        first, oneshotsea::SearchState(identity));
    check(report_json.find(
              "\"trace_prior\":{\"modulus\":\"176\",\"residue\":\"18\"}") !=
              std::string::npos,
          "production curve telemetry exposes the applied exact prior");

    oneshotsea::SearchPipelineConfig curve_side_config = small_config();
    curve_side_config.prime = 397;
    curve_side_config.seed = UINT64_C(0x7821058d55e0f265);
    curve_side_config.curve_family = oneshotsea::SearchCurveFamily::x1_11;
    const oneshotsea::ExactSmoothEngine curve_side_smooth =
        oneshotsea::ExactSmoothEngine::build(curve_side_config.prime);
    const auto curve_side_report = oneshotsea::process_search_curve(
        curve_side_config, curve_side_smooth, 0,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    const auto curve_sample = oneshotsea::deterministic_x1_11_search_curve(
        curve_side_config.prime, curve_side_config.seed, 0, false);
    const mpz_class curve_fixture_trace =
        curve_side_config.prime + 1 - oneshotsea::count_points_bruteforce(
                                          curve_sample.sample->pair.curve);
    check(curve_sample.sample->selected_side ==
              oneshotsea::X111CanonicalSide::curve &&
              curve_side_report.trace_prior_modulus ==
                  std::optional<std::uint64_t>(44U) &&
              curve_side_report.trace_prior_residue ==
                  std::optional<std::uint64_t>(2U) &&
              curve_side_report.exact_trace ==
                  std::optional<mpz_class>(curve_fixture_trace) &&
              curve_side_report.sea_levels == 0U,
          "selected-curve divisor keeps the positive curve-trace prior");
    check(std::none_of(
              curve_side_report.sea_level_timings.begin(),
              curve_side_report.sea_level_timings.end(),
              [](const oneshotsea::SearchSeaLevelTiming& level) {
                  return level.ell == 11U;
              }),
          "X1 group-divisor prior skips SEA level eleven");
}

void test_known_weber_source_lift_pipeline_determinism() {
    oneshotsea::SearchPipelineConfig config = small_config();
    config.early_trace_cap = 1;
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime);

    const auto first = oneshotsea::process_search_curve(
        config, smooth, 1,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    const auto repeat = oneshotsea::process_search_curve(
        config, smooth, 1,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    check(first.status == repeat.status &&
              first.sea_passes == repeat.sea_passes &&
              first.sea_levels == repeat.sea_levels &&
              first.exact_sea_levels == repeat.exact_sea_levels &&
              first.initial_trace_count == repeat.initial_trace_count &&
              first.exact_trace == repeat.exact_trace &&
              first.sea_level_timings.size() ==
                  repeat.sea_level_timings.size(),
          "known Weber source production path is deterministic");
    check(!first.sea_level_timings.empty(),
          "known Weber source pipeline fixture exercises SEA levels");
    for (std::size_t index = 0; index < first.sea_level_timings.size();
         ++index) {
        const auto& left = first.sea_level_timings[index];
        const auto& right = repeat.sea_level_timings[index];
        check(left.pass == right.pass && left.ell == right.ell &&
                  left.exact == right.exact &&
                  left.trace_residue == right.trace_residue &&
                  left.exact_modulus == right.exact_modulus &&
                  left.constraint_modulus == right.constraint_modulus &&
                  left.compatible_source_lifts == 1U &&
                  right.compatible_source_lifts == 1U,
              "known Weber singleton preserves deterministic per-level state");
    }
}

void test_bounded_early_screen_default() {
    const oneshotsea::SearchPipelineConfig defaults;
    check(defaults.early_trace_cap == 64U,
          "default early screen is capped at 64 traces / 128 orders");
}

void test_non_python_success_program_is_rejected() {
    const std::string true_executable =
        oneshotsea::resolve_executable_path("/usr/bin/true");
    check(!oneshotsea::authenticate_python3_interpreter(true_executable),
          "exit-zero non-Python executable cannot impersonate verifier runtime");
}

void test_verifier_runtime_failure_is_not_a_rejection() {
    TemporaryDirectory temporary;
    const std::filesystem::path failing = temporary.path() / "failing.py";
    {
        std::ofstream output(failing, std::ios::binary);
        output << "raise RuntimeError('operational failure')\n";
    }
    const oneshotsea::MontgomeryCertificate fixture{101, 3, 24, 24, {}};
    bool threw = false;
    try {
        (void)oneshotsea::verify_with_canonical_voneshot(
            fixture, failing, oneshotsea::resolve_executable_path("python3"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw,
          "Python verifier exception is operational, not certificate rejection");

    const oneshotsea::MontgomeryCertificate invalid{101, 3, 24, 25, {}};
    check(!oneshotsea::verify_with_canonical_voneshot(
              invalid, "third_party/oneshot_primality_proofs/voneshot.py",
              oneshotsea::resolve_executable_path("python3")),
          "canonical False output remains an ordinary certificate rejection");
}

}  // namespace

int main() {
    try {
        test_sha256_fixtures();
        test_weber_table_authentication();
        test_non_python_success_program_is_rejected();
        test_verifier_runtime_failure_is_not_a_rejection();
        test_bounded_early_screen_default();
        test_worker_partition_is_identity_bound();
        test_x1_curve_family_enters_search_pipeline();
        test_known_weber_source_lift_pipeline_determinism();
        test_sea_level_limit_does_not_advance_cursor();
        test_parallel_stop_discards_later_reports();
        test_parallel_curve_ordering_and_shared_checkpoint();
        test_small_prime_resume_and_canonical_verification();
        std::cout << "search pipeline tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "search pipeline test failure: " << error.what() << '\n';
        return 1;
    }
}
