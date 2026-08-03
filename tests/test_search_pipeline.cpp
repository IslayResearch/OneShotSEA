#include "oneshotsea/search_pipeline.hpp"
#include "oneshotsea/direct_context_cache.hpp"
#include "oneshotsea/weber_table_trust.hpp"
#include "oneshotsea/x1_11_probe.hpp"
#include "oneshotsea/x1_27_probe.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

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
    oneshotsea::Sha256Hasher incremental;
    incremental.update("The quick brown ");
    incremental.update("fox jumps over the lazy dog");
    check(incremental.hex_digest() == oneshotsea::sha256_file(fox),
          "incremental SHA-256 matches the regular-file digest");

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
    std::filesystem::copy_file(source / "SOURCE_CATALOG.txt",
                               copy / "SOURCE_CATALOG.txt");
    for (const auto& entry : std::filesystem::directory_iterator(source)) {
        if (entry.path().filename().string().starts_with("phi_") &&
            entry.path().extension() == ".txt") {
            std::filesystem::create_symlink(
                std::filesystem::absolute(entry.path()),
                copy / entry.path().filename());
        }
    }
    oneshotsea::authenticate_trusted_weber_table_set(copy);

    {
        std::ofstream altered_catalog(copy / "SOURCE_CATALOG.txt",
                                      std::ios::app);
        altered_catalog << '\n';
    }
    bool rejected_altered_catalog = false;
    try {
        oneshotsea::authenticate_trusted_weber_table_set(copy);
    } catch (const std::runtime_error&) {
        rejected_altered_catalog = true;
    }
    check(rejected_altered_catalog,
          "trusted Weber authentication rejects an altered source catalog");
    std::filesystem::remove(copy / "SOURCE_CATALOG.txt");
    std::filesystem::copy_file(source / "SOURCE_CATALOG.txt",
                               copy / "SOURCE_CATALOG.txt");

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

    const std::filesystem::path subset = temporary.path() / "subset";
    std::filesystem::create_directory(subset);
    std::filesystem::copy_file(source / "SOURCE_CATALOG.txt",
                               subset / "SOURCE_CATALOG.txt");
    std::filesystem::create_symlink(
        std::filesystem::absolute(source / "phi_5.txt"),
        subset / "phi_5.txt");
    {
        std::ofstream manifest(subset / "MANIFEST.json");
        manifest
            << "{\n  \"files\": {\n    \"phi_5.txt\": {\n"
            << "      \"bytes\": 80,\n      \"level\": 5,\n"
            << "      \"sha256\": "
               "\"d41cf91849e40aa1a76a8145af66500fa4e925477413eab12ea89f4dba0783fc\"\n"
            << "    }\n  },\n"
            << "  \"source_archive_sha256\": "
               "\"4ecc78a3163ba7232d67e3b2f5e678a2dbc038c7ee4a9d2e8c00c9e0b5a58176\",\n"
            << "  \"source_catalog_sha256\": "
               "\"031c35989f12d8f93c3a992014d6275edb93a21a3a9c70b4b78ce317e7db5dd5\"\n"
            << "}\n";
    }
    oneshotsea::authenticate_trusted_weber_table_set(subset);

    const std::filesystem::path forged = temporary.path() / "forged";
    std::filesystem::create_directory(forged);
    std::filesystem::copy_file(source / "SOURCE_CATALOG.txt",
                               forged / "SOURCE_CATALOG.txt");
    {
        std::ofstream table(forged / "phi_409.txt");
        table << "410 0 1\n";
    }
    const std::string forged_digest =
        oneshotsea::sha256_file(forged / "phi_409.txt");
    {
        std::ofstream manifest(forged / "MANIFEST.json");
        manifest
            << "{\n  \"files\": {\n    \"phi_409.txt\": {\n"
            << "      \"bytes\": 8,\n      \"level\": 409,\n"
            << "      \"sha256\": \"" << forged_digest << "\"\n"
            << "    }\n  },\n"
            << "  \"source_archive_sha256\": "
               "\"4ecc78a3163ba7232d67e3b2f5e678a2dbc038c7ee4a9d2e8c00c9e0b5a58176\",\n"
            << "  \"source_catalog_sha256\": "
               "\"031c35989f12d8f93c3a992014d6275edb93a21a3a9c70b4b78ce317e7db5dd5\"\n"
            << "}\n";
    }
    bool rejected_forged_manifest = false;
    try {
        oneshotsea::authenticate_trusted_weber_table_set(forged);
    } catch (const std::runtime_error&) {
        rejected_forged_manifest = true;
    }
    check(rejected_forged_manifest,
          "trusted Weber authentication rejects a self-consistent table absent from the source catalog");
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

struct CachedDirectRun {
    oneshotsea::SearchCurveReport report;
    oneshotsea::SearchIdentity identity;
    std::string direct_cache_sha256;
};

CachedDirectRun run_one_cached_direct_search(
    oneshotsea::SearchPipelineConfig config, std::uint64_t index,
    const std::filesystem::path& directory, const std::string& label) {
    config.sea_strategy = oneshotsea::SearchSeaStrategy::direct_first;
    config.sea_threads = 1U;
    const std::filesystem::path direct_cache =
        directory / (label + ".direct.ctx");
    auto prepared = oneshotsea::make_classical_direct_sea_context(
        oneshotsea::Field(config.prime), config.classical_direct_levels,
        config.classical_direct_maximum_prime_candidates,
        config.classical_direct_maximum_x_candidates_per_surface,
        config.sea_threads);
    const std::string direct_sha =
        oneshotsea::save_classical_direct_context_cache(
            prepared, direct_cache);
    auto cached = oneshotsea::load_classical_direct_context_cache(
        oneshotsea::Field(config.prime), config.classical_direct_levels,
        config.classical_direct_maximum_prime_candidates,
        config.classical_direct_maximum_x_candidates_per_surface,
        config.sea_threads, direct_cache, direct_sha);
    config.expected_classical_direct_context_sha256 = direct_sha;

    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime,
                                             {.thread_count = 1U});
    const std::filesystem::path smooth_cache =
        directory / (label + ".smooth.cache");
    smooth.save(smooth_cache);
    const std::string smooth_sha = oneshotsea::sha256_file(smooth_cache);
    const std::string verifier_sha =
        oneshotsea::sha256_file(config.canonical_verifier);
    const oneshotsea::SearchIdentity identity =
        oneshotsea::make_search_identity(
            config, {index, index + 1U}, 0U, 1U, smooth_sha, verifier_sha,
            "direct-first-search-test-v1");
    config.expected_schedule_sha256 = identity.schedule_sha256;
    config.expected_smooth_cache_sha256 = smooth_sha;
    config.expected_table_manifest_sha256 = identity.table_manifest_sha256;
    config.expected_verifier_sha256 = verifier_sha;

    oneshotsea::SearchState state(identity);
    oneshotsea::SearchPipelineRunOptions options;
    options.max_curves = 1U;
    options.checkpoint_path = directory / (label + ".checkpoint");
    options.progress_path = directory / (label + ".ndjson");
    options.certificate_path = directory / (label + ".certificate");
    options.classical_direct_context = &cached;
    std::optional<oneshotsea::SearchCurveReport> report;
    const auto result = oneshotsea::run_search_pipeline(
        config, smooth, state, options,
        [&](const oneshotsea::SearchCurveReport& current,
            const oneshotsea::SearchState&) { report = current; },
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    check(result.curves_processed == 1U && report.has_value(),
          "cached direct-first fixture completes exactly one curve");
    return {*report, identity, direct_sha};
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
    config.expected_smooth_cache_sha256 = smooth_sha;
    config.expected_verifier_sha256 = verifier_sha;
    config.expected_table_manifest_sha256 =
        identity.table_manifest_sha256;
    oneshotsea::SearchState state(identity);

    oneshotsea::SearchPipelineRunOptions first;
    first.max_curves = 0;
    first.curve_threads = 2;
    first.smooth_coordinator_count = 1;
    first.checkpoint_path = checkpoint;
    first.progress_path = progress;
    first.certificate_path = temporary.path() / "certificate.txt";
    const auto first_result = oneshotsea::run_search_pipeline(
        config, smooth, state, first);
    check(first_result.curves_processed == 0U &&
              !first_result.verified.has_value() &&
              !first_result.smooth_batch_coordinator_enabled &&
              first_result.smooth_batch_coordinator_count == 0U &&
              first_result.smooth_batch_cohort_telemetry.empty() &&
              state.next_index() == 1U,
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
              second_result.verified.has_value() &&
              second_result.smooth_batch_coordinator_enabled &&
              second_result.smooth_batch_coordinator_count == 1U &&
              second_result.smooth_batch_cohort_telemetry.size() == 1U,
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
              !crash_result.smooth_batch_coordinator_enabled &&
              crash_result.smooth_batch_coordinator_count == 0U &&
              crash_result.smooth_batch_cohort_telemetry.empty() &&
              crash_window.next_index() == 2U &&
              crash_window.counters().certificates_found == 1U,
          "pre-certificate checkpoint recovers the metadata-bound winning index");

    oneshotsea::SearchState completed =
        oneshotsea::load_search_checkpoint(checkpoint, identity);
    const auto recovered_result = oneshotsea::run_search_pipeline(
        config, smooth, completed, second, {}, canonical);
    check(recovered_result.curves_processed == 0U &&
              recovered_result.verified.has_value() &&
              !recovered_result.smooth_batch_coordinator_enabled &&
              recovered_result.smooth_batch_coordinator_count == 0U &&
              recovered_result.smooth_batch_cohort_telemetry.empty() &&
              canonical_calls == 3U,
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

void test_mismatched_smooth_coordinator_fails_before_sea() {
    const oneshotsea::SearchPipelineConfig config = small_config();
    const auto search_engine =
        oneshotsea::ExactSmoothEngine::build(config.prime);
    const auto wrong_engine = oneshotsea::ExactSmoothEngine::build(103);
    oneshotsea::ExactSmoothBatchCoordinator wrong_coordinator(wrong_engine);
    std::size_t sea_callbacks = 0U;
    bool rejected = false;
    try {
        (void)oneshotsea::process_search_curve(
            config, search_engine, 1U, {},
            [&](std::uint64_t, const oneshotsea::SearchSeaLevelTiming&) {
                ++sea_callbacks;
            },
            &wrong_coordinator);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected,
          "search rejects an exact-smooth coordinator for another prime");
    check(sea_callbacks == 0U &&
              wrong_coordinator.telemetry().submitted_requests == 0U,
          "coordinator mismatch fails before SEA or smoothness work");
}

void test_pool_telemetry_merge_checks_overflow_transactionally() {
    using PoolMember =
        std::uint64_t oneshotsea::ExactSmoothBatchPoolTelemetry::*;
    using CohortMember =
        std::uint64_t oneshotsea::ExactSmoothBatchTelemetry::*;
    const std::array<std::pair<PoolMember, CohortMember>, 7> counters = {{
        {&oneshotsea::ExactSmoothBatchPoolTelemetry::submitted_requests,
         &oneshotsea::ExactSmoothBatchTelemetry::submitted_requests},
        {&oneshotsea::ExactSmoothBatchPoolTelemetry::completed_requests,
         &oneshotsea::ExactSmoothBatchTelemetry::completed_requests},
        {&oneshotsea::ExactSmoothBatchPoolTelemetry::failed_requests,
         &oneshotsea::ExactSmoothBatchTelemetry::failed_requests},
        {&oneshotsea::ExactSmoothBatchPoolTelemetry::cancelled_requests,
         &oneshotsea::ExactSmoothBatchTelemetry::cancelled_requests},
        {&oneshotsea::ExactSmoothBatchPoolTelemetry::coordinator_batches,
         &oneshotsea::ExactSmoothBatchTelemetry::coordinator_batches},
        {&oneshotsea::ExactSmoothBatchPoolTelemetry::
             successful_cache_scan_chunks,
         &oneshotsea::ExactSmoothBatchTelemetry::
             successful_cache_scan_chunks},
        {&oneshotsea::ExactSmoothBatchPoolTelemetry::submitted_orders,
         &oneshotsea::ExactSmoothBatchTelemetry::submitted_orders},
    }};
    for (const auto& [pool_member, cohort_member] : counters) {
        oneshotsea::ExactSmoothBatchPoolTelemetry aggregate;
        aggregate.*pool_member = std::numeric_limits<std::uint64_t>::max();
        aggregate.max_queued_requests_in_any_cohort = 7U;
        oneshotsea::ExactSmoothBatchTelemetry cohort;
        cohort.*cohort_member = 1U;
        bool rejected = false;
        try {
            oneshotsea::merge_exact_smooth_batch_pool_telemetry(
                aggregate, cohort);
        } catch (const std::overflow_error&) {
            rejected = true;
        }
        check(rejected &&
                  aggregate.*pool_member ==
                      std::numeric_limits<std::uint64_t>::max() &&
                  aggregate.max_queued_requests_in_any_cohort == 7U,
              "pool telemetry scalar overflow is transactional");
    }

    oneshotsea::ExactSmoothBatchPoolTelemetry histogram_aggregate;
    histogram_aggregate.successful_scan_chunks_by_order_count.push_back(
        {4U, std::numeric_limits<std::uint64_t>::max()});
    oneshotsea::ExactSmoothBatchTelemetry histogram_cohort;
    histogram_cohort.successful_scan_chunks_by_order_count.push_back(
        {4U, 1U});
    bool histogram_rejected = false;
    try {
        oneshotsea::merge_exact_smooth_batch_pool_telemetry(
            histogram_aggregate, histogram_cohort);
    } catch (const std::overflow_error&) {
        histogram_rejected = true;
    }
    check(histogram_rejected &&
              histogram_aggregate.successful_scan_chunks_by_order_count
                      .front()
                      .scan_chunks ==
                  std::numeric_limits<std::uint64_t>::max(),
          "pool telemetry histogram overflow is transactional");
}

void test_search_uses_retained_schoof_fallback_without_second_sea_pass() {
    oneshotsea::SearchPipelineConfig config = small_config();
    config.max_level = 5U;
    config.early_trace_cap = 1U;
    config.enable_schoof_fallback = true;
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime);

    const auto report = oneshotsea::process_search_curve(
        config, smooth, 1U,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    const auto generated = oneshotsea::deterministic_weber_curve_pair(
        config.prime, config.seed, 1U);
    const mpz_class expected_trace =
        config.prime + 1 -
        oneshotsea::count_points_bruteforce(generated.curve);
    check(report.status != oneshotsea::SearchCurveStatus::sea_level_limit &&
              report.exact_trace ==
                  std::optional<mpz_class>(expected_trace) &&
              report.sea_passes == 1U &&
              !report.schoof_fallback_levels.empty(),
          "search completes from retained SEA state without a second table pass");
    for (const auto& level : report.schoof_fallback_levels) {
        check(level.trace_residue ==
                  mpz_fdiv_ui(expected_trace.get_mpz_t(), level.ell),
              "search fallback residue matches the brute-force trace");
    }
    const std::string digest(64U, 'd');
    const auto identity = oneshotsea::make_search_identity(
        config, {1, 2}, 0, 1, digest, digest, "fallback-pipeline-test-v1");
    const std::string report_json = oneshotsea::search_curve_report_json(
        report, oneshotsea::SearchState(identity), false);
    check(report_json.find("\"schoof_fallback_level_count\":\"") !=
                  std::string::npos &&
              report_json.find("\"schoof_fallback_levels\":[{") !=
                  std::string::npos &&
              report_json.find("classical_direct") == std::string::npos &&
              report_json.find("sea_strategy") == std::string::npos &&
              report_json.find("direct_first") == std::string::npos,
          "search telemetry retains byte-compatible default fields and compact exact-Schoof evidence");
}

void test_search_uses_classical_direct_tail_after_weber_completion() {
    oneshotsea::SearchPipelineConfig config = small_config();
    config.max_level = 5U;
    config.early_trace_cap = 16U;
    config.classical_direct_levels = {7U, 11U};
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime);

    std::vector<oneshotsea::SearchClassicalDirectLevelTiming> live_levels;
    const auto report = oneshotsea::process_search_curve(
        config, smooth, 1U,
        [](const oneshotsea::MontgomeryCertificate&) { return false; },
        {}, nullptr,
        [&](std::uint64_t index,
            const oneshotsea::SearchClassicalDirectLevelTiming& level) {
            check(index == 1U,
                  "live direct SEA telemetry retains the curve index");
            live_levels.push_back(level);
        });
    const auto generated = oneshotsea::deterministic_weber_curve_pair(
        config.prime, config.seed, 1U);
    const mpz_class expected_trace =
        config.prime + 1 -
        oneshotsea::count_points_bruteforce(generated.curve);

    check(report.status != oneshotsea::SearchCurveStatus::sea_level_limit &&
              report.initial_trace_count == 10U &&
              report.exact_trace ==
                  std::optional<mpz_class>(expected_trace) &&
              report.sea_passes == 2U && report.sea_levels == 1U &&
              report.classical_direct_passes == 1U &&
              report.classical_direct_levels.size() == 1U &&
              report.schoof_fallback_levels.empty(),
          "search exhausts the authenticated Weber schedule before using the minimal direct tail");
    check(live_levels.size() == report.classical_direct_levels.size(),
          "live direct SEA telemetry covers every retained direct level");
    for (std::size_t index = 0U;
         index < report.classical_direct_levels.size(); ++index) {
        const auto& level = report.classical_direct_levels[index];
        check(level.pass == 1U && level.exact && level.ell == 7U &&
                  level.trace_residue == mpz_fdiv_ui(
                      expected_trace.get_mpz_t(), level.ell) &&
                  level.order_discriminant < 0 &&
                  level.class_number != 0U &&
                  level.auxiliary_prime_count != 0U &&
                  level.elkies_kernel_count == 2U &&
                  live_levels[index].ell == level.ell &&
                  live_levels[index].trace_residue == level.trace_residue,
              "direct search level retains authenticated CM/CRT and exact-trace evidence");
        const std::string live_json =
            oneshotsea::search_classical_direct_level_json(1U, level);
        check(live_json.find(
                  "\"schema\":\"oneshotsea.search-classical-direct-level.v1\"") !=
                      std::string::npos &&
                  live_json.find("\"order_discriminant\":\"") !=
                      std::string::npos &&
                  live_json.find("\"auxiliary_prime_count\":\"") !=
                      std::string::npos,
              "live direct SEA JSON exposes the mathematical evidence");
    }

    const std::string digest(64U, 'c');
    const auto direct_identity = oneshotsea::make_search_identity(
        config, {1, 2}, 0, 1, digest, digest,
        "direct-tail-pipeline-test-v1");
    const std::string report_json = oneshotsea::search_curve_report_json(
        report, oneshotsea::SearchState(direct_identity));
    const std::string compact_json = oneshotsea::search_curve_report_json(
        report, oneshotsea::SearchState(direct_identity), false);
    check(report_json.find("\"classical_direct_level_count\":\"1\"") !=
                  std::string::npos &&
              report_json.find("\"classical_direct_levels\":[{") !=
                  std::string::npos &&
              compact_json.find("\"classical_direct_level_count\":\"1\"") !=
                  std::string::npos &&
              compact_json.find("\"classical_direct_levels\":[]") !=
                  std::string::npos,
          "curve JSON retains direct counts while honoring compact level telemetry");

    oneshotsea::SearchPipelineConfig disabled = config;
    disabled.classical_direct_levels.clear();
    const auto disabled_identity = oneshotsea::make_search_identity(
        disabled, {1, 2}, 0, 1, digest, digest,
        "direct-tail-pipeline-test-v1");
    oneshotsea::SearchPipelineConfig tighter_cap = config;
    --tighter_cap.classical_direct_maximum_prime_candidates;
    const auto tighter_cap_identity = oneshotsea::make_search_identity(
        tighter_cap, {1, 2}, 0, 1, digest, digest,
        "direct-tail-pipeline-test-v1");
    check(direct_identity.schedule_sha256 !=
                  disabled_identity.schedule_sha256 &&
              direct_identity.schedule_sha256 !=
                  tighter_cap_identity.schedule_sha256,
          "direct policy, level list, and execution caps are schedule-bound");

    for (const std::vector<std::uint64_t>& invalid_levels :
         {std::vector<std::uint64_t>{11U, 7U},
          std::vector<std::uint64_t>{7U, 7U},
          std::vector<std::uint64_t>{9U}}) {
        oneshotsea::SearchPipelineConfig invalid = config;
        invalid.classical_direct_levels = invalid_levels;
        bool rejected = false;
        try {
            (void)oneshotsea::make_search_identity(
                invalid, {1, 2}, 0, 1, digest, digest,
                "invalid-direct-tail-test-v1");
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected,
              "search identity rejects an invalid direct SEA schedule");
    }
}

void test_direct_tail_preserves_remaining_weber_levels() {
    oneshotsea::SearchPipelineConfig config = small_config();
    config.max_level = 11U;
    config.early_trace_cap = 16U;
    config.classical_direct_levels = {7U};
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime);

    const auto report = oneshotsea::process_search_curve(
        config, smooth, 1U,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    const auto generated = oneshotsea::deterministic_weber_curve_pair(
        config.prime, config.seed, 1U);
    const mpz_class expected_trace =
        config.prime + 1 -
        oneshotsea::count_points_bruteforce(generated.curve);
    check(report.status != oneshotsea::SearchCurveStatus::sea_level_limit &&
              report.sea_passes == 2U &&
              report.exact_trace ==
                  std::optional<mpz_class>(expected_trace) &&
              expected_trace == -10,
          "direct-enabled survivors exhaust the authenticated Weber schedule before the optional tail");
}

void test_pipeline_prepares_direct_contexts_once_per_run() {
    TemporaryDirectory temporary;
    oneshotsea::SearchPipelineConfig config = small_config();
    config.max_level = 5U;
    config.early_trace_cap = 16U;
    config.classical_direct_levels = {7U, 11U};
    config.skip_incomplete_curves = true;
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime,
                                             {.thread_count = 1});
    const std::filesystem::path smooth_cache =
        temporary.path() / "prepared-smooth.cache";
    smooth.save(smooth_cache);
    const std::string cache_sha = oneshotsea::sha256_file(smooth_cache);
    const std::string verifier_sha =
        oneshotsea::sha256_file(config.canonical_verifier);
    const oneshotsea::SearchIdentity identity =
        oneshotsea::make_search_identity(
            config, {1, 3}, 0, 1, cache_sha, verifier_sha,
            "prepared-direct-pipeline-test-v1");
    config.expected_schedule_sha256 = identity.schedule_sha256;
    config.expected_smooth_cache_sha256 = cache_sha;
    config.expected_table_manifest_sha256 = identity.table_manifest_sha256;
    config.expected_verifier_sha256 = verifier_sha;

    oneshotsea::SearchState state(identity);
    oneshotsea::SearchPipelineRunOptions options;
    options.max_curves = 2U;
    options.curve_threads = 2U;
    options.checkpoint_path = temporary.path() / "prepared.checkpoint";
    options.progress_path = temporary.path() / "prepared.ndjson";
    options.certificate_path = temporary.path() / "prepared.cert";
    std::vector<oneshotsea::SearchCurveReport> reports;
    const oneshotsea::SearchPipelineRunResult result =
        oneshotsea::run_search_pipeline(
            config, smooth, state, options,
            [&](const oneshotsea::SearchCurveReport& report,
                const oneshotsea::SearchState&) {
                reports.push_back(report);
            },
            [](const oneshotsea::MontgomeryCertificate&) { return false; });
    std::size_t direct_records = 0U;
    std::vector<std::uint64_t> prepared_levels;
    for (const auto& report : reports) {
        direct_records += report.classical_direct_levels.size();
        for (const auto& level : report.classical_direct_levels) {
            if (std::find(prepared_levels.begin(), prepared_levels.end(),
                          level.ell) == prepared_levels.end()) {
                prepared_levels.push_back(level.ell);
            }
        }
    }
    check(result.curves_processed == 2U &&
              result.classical_direct_context_count ==
                  prepared_levels.size() &&
              result.classical_direct_preparation_us != 0U &&
              result.classical_direct_interpolation_coefficient_count != 0U &&
              result.classical_direct_interpolation_storage_bytes ==
                  result.classical_direct_interpolation_coefficient_count *
                      sizeof(std::uint64_t) &&
              reports.size() == 2U && direct_records != 0U,
          "multi-curve search lazily prepares each used direct level once and shares it across workers");

    oneshotsea::SearchState no_work_state(identity);
    oneshotsea::SearchPipelineRunOptions no_work;
    no_work.max_curves = 0U;
    const oneshotsea::SearchPipelineRunResult no_work_result =
        oneshotsea::run_search_pipeline(
            config, smooth, no_work_state, no_work, {},
            [](const oneshotsea::MontgomeryCertificate&) { return false; });
    check(no_work_result.curves_processed == 0U &&
              no_work_result.classical_direct_context_count == 0U &&
              no_work_result.classical_direct_preparation_us == 0U &&
              no_work_result
                      .classical_direct_interpolation_coefficient_count ==
                  0U &&
              no_work_result.classical_direct_interpolation_storage_bytes ==
                  0U,
          "zero-work search does not pay direct-context preparation cost");

    oneshotsea::SearchPipelineConfig capped = config;
    capped.classical_direct_maximum_prime_candidates = 1U;
    const oneshotsea::SearchIdentity capped_identity =
        oneshotsea::make_search_identity(
            capped, {1, 2}, 0, 1, cache_sha, verifier_sha,
            "prepared-direct-cap-test-v1");
    capped.expected_schedule_sha256 = capped_identity.schedule_sha256;
    capped.expected_smooth_cache_sha256 = cache_sha;
    capped.expected_table_manifest_sha256 =
        capped_identity.table_manifest_sha256;
    capped.expected_verifier_sha256 = verifier_sha;
    oneshotsea::SearchState capped_state(capped_identity);
    oneshotsea::SearchPipelineRunOptions capped_options;
    capped_options.max_curves = 1U;
    capped_options.checkpoint_path = temporary.path() / "capped.checkpoint";
    capped_options.progress_path = temporary.path() / "capped.ndjson";
    capped_options.certificate_path = temporary.path() / "capped.cert";
    bool preparation_failed = false;
    try {
        static_cast<void>(oneshotsea::run_search_pipeline(
            capped, smooth, capped_state, capped_options, {},
            [](const oneshotsea::MontgomeryCertificate&) { return false; }));
    } catch (const std::runtime_error&) {
        preparation_failed = true;
    }
    check(preparation_failed && capped_state.next_index() == 1U &&
              !std::filesystem::exists(capped_options.checkpoint_path) &&
              !std::filesystem::exists(capped_options.progress_path),
          "direct-context preparation cap failure leaves the durable curve cursor untouched");
}

void test_pipeline_reuses_authenticated_direct_context_cache() {
    TemporaryDirectory temporary;
    oneshotsea::SearchPipelineConfig config = small_config();
    config.max_level = 5U;
    config.early_trace_cap = 16U;
    config.classical_direct_levels = {7U};
    config.skip_incomplete_curves = true;
    config.sea_threads = 3U;

    const std::filesystem::path direct_cache =
        temporary.path() / "classical-direct.ctx";
    auto prepared = oneshotsea::make_classical_direct_sea_context(
        oneshotsea::Field(config.prime), config.classical_direct_levels,
        config.classical_direct_maximum_prime_candidates,
        config.classical_direct_maximum_x_candidates_per_surface,
        config.sea_threads);
    const std::string direct_sha =
        oneshotsea::save_classical_direct_context_cache(
            prepared, direct_cache);
    oneshotsea::ClassicalDirectContextCacheLimits cache_limits;
    cache_limits.max_file_bytes =
        oneshotsea::kDefaultMaxClassicalDirectContextCacheBytes + 1U;
    auto cached = oneshotsea::load_classical_direct_context_cache(
        oneshotsea::Field(config.prime), config.classical_direct_levels,
        config.classical_direct_maximum_prime_candidates,
        config.classical_direct_maximum_x_candidates_per_surface,
        config.sea_threads, direct_cache, direct_sha, cache_limits);
    config.expected_classical_direct_context_sha256 = direct_sha;
    config.classical_direct_context_max_file_bytes =
        cache_limits.max_file_bytes;

    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime,
                                             {.thread_count = 1});
    const std::filesystem::path smooth_cache =
        temporary.path() / "cached-direct-smooth.cache";
    smooth.save(smooth_cache);
    const std::string smooth_sha = oneshotsea::sha256_file(smooth_cache);
    const std::string verifier_sha =
        oneshotsea::sha256_file(config.canonical_verifier);
    oneshotsea::SearchPipelineConfig default_limit_config = config;
    default_limit_config.classical_direct_context_max_file_bytes =
        oneshotsea::kDefaultMaxClassicalDirectContextCacheBytes;
    const oneshotsea::SearchIdentity default_limit_identity =
        oneshotsea::make_search_identity(
            default_limit_config, {1U, 2U}, 0U, 1U, smooth_sha,
            verifier_sha, "cached-direct-pipeline-test-v1");
    const oneshotsea::SearchIdentity identity =
        oneshotsea::make_search_identity(
            config, {1U, 2U}, 0U, 1U, smooth_sha, verifier_sha,
            "cached-direct-pipeline-test-v1");
    config.expected_schedule_sha256 = identity.schedule_sha256;
    config.expected_smooth_cache_sha256 = smooth_sha;
    config.expected_table_manifest_sha256 =
        identity.table_manifest_sha256;
    config.expected_verifier_sha256 = verifier_sha;

    const std::filesystem::path limit_checkpoint =
        temporary.path() / "limit.checkpoint";
    oneshotsea::save_search_checkpoint(
        oneshotsea::SearchState(identity), limit_checkpoint);
    bool rejected_limit_checkpoint = false;
    try {
        static_cast<void>(oneshotsea::load_search_checkpoint(
            limit_checkpoint, default_limit_identity));
    } catch (const oneshotsea::SearchCheckpointError&) {
        rejected_limit_checkpoint = true;
    }

    oneshotsea::SearchState state(identity);
    oneshotsea::SearchPipelineRunOptions options;
    options.max_curves = 1U;
    options.checkpoint_path = temporary.path() / "cached.checkpoint";
    options.progress_path = temporary.path() / "cached.ndjson";
    options.certificate_path = temporary.path() / "cached.cert";
    options.classical_direct_context = &cached;
    const oneshotsea::SearchPipelineRunResult result =
        oneshotsea::run_search_pipeline(
            config, smooth, state, options, {},
            [](const oneshotsea::MontgomeryCertificate&) { return false; });
    check(result.curves_processed == 1U &&
              result.classical_direct_context_cache_loaded &&
              result.classical_direct_context_cache_load_us ==
                  cached.cache_load_us() &&
              result.classical_direct_context_count == 1U &&
              result.classical_direct_preparation_us == 0U &&
              result.classical_direct_interpolation_coefficient_count != 0U &&
              result.classical_direct_cached_level_load_count != 0U &&
              result.classical_direct_peak_cached_resident_context_count ==
                  1U &&
              result.classical_direct_final_cached_resident_context_count ==
                  0U &&
              result.classical_direct_cache_residency_budget_bytes == 0U &&
              result
                      .classical_direct_final_cached_retained_context_count ==
                  0U &&
              result
                      .classical_direct_final_cached_retained_payload_bytes ==
                  0U &&
              result.classical_direct_cached_context_eviction_count == 0U,
          "search streams an authenticated direct cache without rebuilding or retaining its level context");

    oneshotsea::SearchPipelineRunOptions no_cache_options;
    oneshotsea::SearchState missing_cache_state(identity);
    bool rejected_missing_cache = false;
    try {
        static_cast<void>(oneshotsea::run_search_pipeline(
            config, smooth, missing_cache_state, no_cache_options, {},
            [](const oneshotsea::MontgomeryCertificate&) { return false; }));
    } catch (const std::invalid_argument&) {
        rejected_missing_cache = true;
    }

    oneshotsea::SearchPipelineConfig uncached = config;
    uncached.expected_schedule_sha256.clear();
    uncached.expected_smooth_cache_sha256.clear();
    uncached.expected_table_manifest_sha256.clear();
    uncached.expected_verifier_sha256.clear();
    uncached.expected_classical_direct_context_sha256.clear();
    uncached.classical_direct_context_max_file_bytes =
        oneshotsea::kDefaultMaxClassicalDirectContextCacheBytes;
    const oneshotsea::SearchIdentity uncached_identity =
        oneshotsea::make_search_identity(
            uncached, {1U, 2U}, 0U, 1U, smooth_sha, verifier_sha,
            "cached-direct-pipeline-test-v1");
    uncached.expected_schedule_sha256 = uncached_identity.schedule_sha256;
    uncached.expected_smooth_cache_sha256 = smooth_sha;
    uncached.expected_table_manifest_sha256 =
        uncached_identity.table_manifest_sha256;
    uncached.expected_verifier_sha256 = verifier_sha;
    oneshotsea::SearchState unexpected_cache_state(uncached_identity);
    oneshotsea::SearchPipelineRunOptions unexpected_cache_options;
    unexpected_cache_options.classical_direct_context = &cached;
    bool rejected_unbound_cache = false;
    try {
        static_cast<void>(oneshotsea::run_search_pipeline(
            uncached, smooth, unexpected_cache_state,
            unexpected_cache_options, {},
            [](const oneshotsea::MontgomeryCertificate&) { return false; }));
    } catch (const std::invalid_argument&) {
        rejected_unbound_cache = true;
    }

    auto wrong_resource = oneshotsea::load_classical_direct_context_cache(
        oneshotsea::Field(config.prime), config.classical_direct_levels,
        config.classical_direct_maximum_prime_candidates,
        config.classical_direct_maximum_x_candidates_per_surface,
        config.sea_threads, direct_cache, direct_sha);
    oneshotsea::SearchState wrong_resource_state(identity);
    oneshotsea::SearchPipelineRunOptions wrong_resource_options;
    wrong_resource_options.classical_direct_context = &wrong_resource;
    bool rejected_wrong_resource = false;
    try {
        static_cast<void>(oneshotsea::run_search_pipeline(
            config, smooth, wrong_resource_state, wrong_resource_options, {},
            [](const oneshotsea::MontgomeryCertificate&) { return false; }));
    } catch (const std::invalid_argument&) {
        rejected_wrong_resource = true;
    }
    check(rejected_missing_cache && rejected_unbound_cache &&
              rejected_wrong_resource &&
              rejected_limit_checkpoint &&
              cached.cache_max_file_bytes() ==
                  cache_limits.max_file_bytes &&
              identity.schedule_sha256 !=
                  default_limit_identity.schedule_sha256 &&
              identity.schedule_sha256 != uncached_identity.schedule_sha256 &&
              missing_cache_state.next_index() == 1U &&
              unexpected_cache_state.next_index() == 1U &&
              wrong_resource_state.next_index() == 1U,
          "direct-cache injection is digest-bound, resource-checked, and fails before cursor mutation");
}

void test_checkpoint_bound_direct_first_strategy() {
    TemporaryDirectory temporary;
    oneshotsea::SearchPipelineConfig completion_config = small_config();
    completion_config.max_level = 5U;
    completion_config.early_trace_cap = 1U;
    completion_config.classical_direct_levels = {7U, 11U};
    const CachedDirectRun completion = run_one_cached_direct_search(
        completion_config, 1U, temporary.path(), "completion");
    const oneshotsea::ExactSmoothEngine completion_smooth =
        oneshotsea::ExactSmoothEngine::build(completion_config.prime,
                                             {.thread_count = 1U});
    const auto generated_completion = oneshotsea::process_search_curve(
        completion_config, completion_smooth, 1U,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    check(completion.report.direct_first_attempts == 1U &&
              completion.report.direct_first_completions == 1U &&
              completion.report.direct_first_fallbacks == 0U &&
              completion.report.sea_passes == 0U &&
              !completion.report.classical_direct_levels.empty() &&
              !generated_completion.classical_direct_levels.empty() &&
              completion.report.classical_direct_levels.front().ell ==
                  generated_completion.classical_direct_levels.front().ell &&
              completion.report.classical_direct_levels.front()
                      .trace_residue ==
                  generated_completion.classical_direct_levels.front()
                      .trace_residue &&
              completion.report.status == generated_completion.status &&
              completion.report.exact_trace ==
                  generated_completion.exact_trace,
          "cached direct-first completion matches freshly generated direct evidence and terminal semantics");

    oneshotsea::SearchPipelineConfig fallback_config = small_config();
    fallback_config.early_trace_cap = 1U;
    fallback_config.classical_direct_levels = {5U};
    const CachedDirectRun fallback = run_one_cached_direct_search(
        fallback_config, 1U, temporary.path(), "fallback");
    const oneshotsea::ExactSmoothEngine fallback_smooth =
        oneshotsea::ExactSmoothEngine::build(fallback_config.prime,
                                             {.thread_count = 1U});
    const auto generated_weber = oneshotsea::process_search_curve(
        fallback_config, fallback_smooth, 1U,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    check(fallback.report.sea_strategy ==
                  oneshotsea::SearchSeaStrategy::direct_first &&
              fallback.report.direct_first_attempts == 1U &&
              fallback.report.direct_first_completions == 0U &&
              fallback.report.direct_first_fallbacks == 1U &&
              fallback.report.timings.direct_first_us != 0U &&
              fallback.report.timings.direct_first_fallback_us != 0U &&
              fallback.report.sea_passes != 0U &&
              fallback.report.status == generated_weber.status &&
              fallback.report.outcome.terminal_stage ==
                  generated_weber.outcome.terminal_stage &&
              fallback.report.outcome.full_point_count_completed ==
                  generated_weber.outcome.full_point_count_completed &&
              fallback.report.outcome.reached_smoothness_testing ==
                  generated_weber.outcome.reached_smoothness_testing &&
              fallback.report.exact_trace == generated_weber.exact_trace,
          "inconclusive cached direct state is discarded before a semantically equivalent Weber-first restart");

    oneshotsea::SearchPipelineConfig direct_identity_config = fallback_config;
    direct_identity_config.sea_threads = 1U;
    direct_identity_config.sea_strategy =
        oneshotsea::SearchSeaStrategy::direct_first;
    direct_identity_config.expected_classical_direct_context_sha256 =
        fallback.direct_cache_sha256;
    const std::string digest(64U, 'e');
    const auto direct_identity = oneshotsea::make_search_identity(
        direct_identity_config, {1U, 2U}, 0U, 1U, digest, digest,
        "direct-first-identity-test-v1");
    direct_identity_config.sea_strategy =
        oneshotsea::SearchSeaStrategy::weber_first;
    const auto weber_identity = oneshotsea::make_search_identity(
        direct_identity_config, {1U, 2U}, 0U, 1U, digest, digest,
        "direct-first-identity-test-v1");
    const std::filesystem::path checkpoint =
        temporary.path() / "direct-identity.checkpoint";
    oneshotsea::save_search_checkpoint(
        oneshotsea::SearchState(direct_identity), checkpoint);
    bool rejected_cross_resume = false;
    try {
        (void)oneshotsea::load_search_checkpoint(checkpoint, weber_identity);
    } catch (const oneshotsea::SearchCheckpointError&) {
        rejected_cross_resume = true;
    }
    oneshotsea::SearchPipelineConfig empty_direct = fallback_config;
    empty_direct.sea_strategy = oneshotsea::SearchSeaStrategy::direct_first;
    bool rejected_empty_direct = false;
    try {
        (void)oneshotsea::make_search_identity(
            empty_direct, {1U, 2U}, 0U, 1U, digest, digest,
            "direct-first-empty-test-v1");
    } catch (const std::invalid_argument&) {
        rejected_empty_direct = true;
    }
    check(direct_identity.schedule_sha256 !=
                  weber_identity.schedule_sha256 &&
              rejected_cross_resume && rejected_empty_direct,
          "direct-first is nondefault, checkpoint-bound, and requires an authenticated nonempty direct schedule");

    const std::string fallback_json = oneshotsea::search_curve_report_json(
        fallback.report, oneshotsea::SearchState(fallback.identity), false);
    check(fallback_json.find("\"sea_strategy\":\"direct-first\"") !=
                  std::string::npos &&
              fallback_json.find(
                  "\"direct_first\":{\"attempts\":\"1\",\"completions\":\"0\",\"fallbacks\":\"1\"}") !=
                  std::string::npos &&
              fallback_json.find("\"direct_first_fallback\":\"") !=
                  std::string::npos,
          "curve telemetry distinguishes direct-first exhaustion and fallback timing");

    for (const auto& fixture : {
             std::pair<std::uint64_t, std::uint64_t>{157U, 7U},
             std::pair<std::uint64_t, std::uint64_t>{397U, 0U}}) {
        oneshotsea::SearchPipelineConfig config = small_config();
        config.prime = mpz_class(std::to_string(fixture.first));
        config.seed = UINT64_C(0x7821058d55e0f265);
        config.curve_family = oneshotsea::SearchCurveFamily::x1_11;
        config.x1_require_point_four = fixture.first == 157U;
        config.early_trace_cap = 1U;
        config.classical_direct_levels = {5U};
        const CachedDirectRun direct = run_one_cached_direct_search(
            config, fixture.second, temporary.path(),
            "x1-" + std::to_string(fixture.first));
        const auto generated = oneshotsea::deterministic_x1_11_search_curve(
            config.prime, config.seed, fixture.second,
            config.x1_require_point_four);
        const mpz_class curve_trace =
            config.prime + 1 - oneshotsea::count_points_bruteforce(
                                   generated.sample->pair.curve);
        check(direct.report.direct_first_attempts == 1U &&
                  direct.report.direct_first_completions == 1U &&
                  direct.report.direct_first_fallbacks == 0U &&
                  direct.report.sea_passes == 0U &&
                  direct.report.exact_trace ==
                      std::optional<mpz_class>(curve_trace),
              "direct-first applies the selected X1 family prior to the counted curve, not to its selection side");
    }

    oneshotsea::SearchPipelineConfig tamper_config = small_config();
    tamper_config.early_trace_cap = 1U;
    tamper_config.classical_direct_levels = {5U};
    tamper_config.sea_strategy = oneshotsea::SearchSeaStrategy::direct_first;
    tamper_config.sea_threads = 1U;
    const std::filesystem::path tamper_cache =
        temporary.path() / "tampered.direct.ctx";
    auto tamper_prepared = oneshotsea::make_classical_direct_sea_context(
        oneshotsea::Field(tamper_config.prime),
        tamper_config.classical_direct_levels,
        tamper_config.classical_direct_maximum_prime_candidates,
        tamper_config.classical_direct_maximum_x_candidates_per_surface,
        tamper_config.sea_threads);
    const std::string tamper_sha =
        oneshotsea::save_classical_direct_context_cache(
            tamper_prepared, tamper_cache);
    auto tampered = oneshotsea::load_classical_direct_context_cache(
        oneshotsea::Field(tamper_config.prime),
        tamper_config.classical_direct_levels,
        tamper_config.classical_direct_maximum_prime_candidates,
        tamper_config.classical_direct_maximum_x_candidates_per_surface,
        tamper_config.sea_threads, tamper_cache, tamper_sha);
    tamper_config.expected_classical_direct_context_sha256 = tamper_sha;
    {
        std::fstream file(tamper_cache,
                          std::ios::in | std::ios::out | std::ios::binary);
        check(static_cast<bool>(file), "open direct cache for deferred tamper");
        file.seekg(-1, std::ios::end);
        char byte = 0;
        file.read(&byte, 1);
        file.seekp(-1, std::ios::end);
        byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 1U);
        file.write(&byte, 1);
    }
    const oneshotsea::ExactSmoothEngine tamper_smooth =
        oneshotsea::ExactSmoothEngine::build(tamper_config.prime,
                                             {.thread_count = 1U});
    const std::filesystem::path tamper_smooth_cache =
        temporary.path() / "tampered.smooth.cache";
    tamper_smooth.save(tamper_smooth_cache);
    const std::string smooth_sha =
        oneshotsea::sha256_file(tamper_smooth_cache);
    const std::string verifier_sha =
        oneshotsea::sha256_file(tamper_config.canonical_verifier);
    const auto tamper_identity = oneshotsea::make_search_identity(
        tamper_config, {1U, 2U}, 0U, 1U, smooth_sha, verifier_sha,
        "direct-first-tamper-test-v1");
    tamper_config.expected_schedule_sha256 =
        tamper_identity.schedule_sha256;
    tamper_config.expected_smooth_cache_sha256 = smooth_sha;
    tamper_config.expected_table_manifest_sha256 =
        tamper_identity.table_manifest_sha256;
    tamper_config.expected_verifier_sha256 = verifier_sha;
    oneshotsea::SearchState tamper_state(tamper_identity);
    oneshotsea::SearchPipelineRunOptions tamper_options;
    tamper_options.max_curves = 1U;
    tamper_options.checkpoint_path = temporary.path() / "tampered.checkpoint";
    tamper_options.progress_path = temporary.path() / "tampered.ndjson";
    tamper_options.certificate_path = temporary.path() / "tampered.cert";
    tamper_options.classical_direct_context = &tampered;
    bool propagated_tamper = false;
    try {
        (void)oneshotsea::run_search_pipeline(
            tamper_config, tamper_smooth, tamper_state, tamper_options, {},
            [](const oneshotsea::MontgomeryCertificate&) { return false; });
    } catch (const std::runtime_error&) {
        propagated_tamper = true;
    }
    check(propagated_tamper && tamper_state.next_index() == 1U &&
              !std::filesystem::exists(tamper_options.checkpoint_path) &&
              !std::filesystem::exists(tamper_options.progress_path),
          "direct-first propagates deferred cache tamper without fallback or durable cursor mutation");
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
    config.expected_smooth_cache_sha256 =
        oneshotsea::sha256_file(smooth_cache);
    config.expected_verifier_sha256 =
        oneshotsea::sha256_file(config.canonical_verifier);
    config.expected_table_manifest_sha256 = identity.table_manifest_sha256;

    struct Observation {
        std::vector<std::uint64_t> indices;
        std::vector<oneshotsea::SearchCurveStatus> statuses;
        oneshotsea::SearchCounters counters;
        std::uint64_t next_index = 0;
    };
    const auto run = [&](std::size_t curve_threads,
                         std::size_t smooth_coordinators,
                         const std::string& name,
                         bool audit_live_levels) {
        oneshotsea::SearchState state(identity);
        oneshotsea::SearchPipelineRunOptions options;
        options.max_curves = 4;
        options.curve_threads = curve_threads;
        options.smooth_coordinator_count = smooth_coordinators;
        options.checkpoint_every = 2;
        options.checkpoint_path = temporary.path() / (name + ".checkpoint");
        options.progress_path = temporary.path() / (name + ".ndjson");
        options.certificate_path = temporary.path() / (name + ".cert");

        std::atomic<unsigned> active_callbacks{0};
        std::atomic<unsigned> maximum_active_callbacks{0};
        const auto enter_callback = [&] {
            const unsigned active = active_callbacks.fetch_add(1U) + 1U;
            unsigned maximum = maximum_active_callbacks.load();
            while (maximum < active &&
                   !maximum_active_callbacks.compare_exchange_weak(
                       maximum, active)) {
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        };
        const auto leave_callback = [&] {
            active_callbacks.fetch_sub(1U);
        };
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
                    enter_callback();
                    live_levels[index].emplace_back(level.pass, level.ell);
                    leave_callback();
                };
        }

        Observation observation;
        const auto result = oneshotsea::run_search_pipeline(
            config, smooth, state, options,
            [&](const oneshotsea::SearchCurveReport& report,
                const oneshotsea::SearchState&) {
                if (audit_live_levels) {
                    enter_callback();
                }
                observation.indices.push_back(report.global_index);
                observation.statuses.push_back(report.status);
                auto& levels = report_levels[report.global_index];
                for (const auto& level : report.sea_level_timings) {
                    levels.emplace_back(level.pass, level.ell);
                }
                if (audit_live_levels) {
                    leave_callback();
                }
            },
            [](const oneshotsea::MontgomeryCertificate&) { return false; });
        check(result.curves_processed == 4U &&
                  result.exhausted_assigned_range &&
                  !result.verified.has_value(),
              "four-curve fixture exhausts under rejecting verifier");
        if (smooth_coordinators == 0U) {
            check(!result.smooth_batch_coordinator_enabled &&
                      result.smooth_batch_coordinator_count == 0U &&
                      result.smooth_batch_cohort_telemetry.empty() &&
                      result.smooth_batch_telemetry.submitted_requests == 0U,
                  "zero-coordinator pipeline keeps direct exact-smooth extraction");
        } else {
            bool routing_matches =
                result.smooth_batch_cohort_telemetry.size() ==
                smooth_coordinators;
            std::uint64_t submitted_requests = 0U;
            std::uint64_t completed_requests = 0U;
            std::uint64_t failed_requests = 0U;
            std::uint64_t cancelled_requests = 0U;
            std::uint64_t coordinator_batches = 0U;
            std::uint64_t successful_scan_chunks = 0U;
            std::uint64_t submitted_orders = 0U;
            std::size_t max_queued_requests = 0U;
            std::size_t max_requests_per_batch = 0U;
            std::size_t max_orders_per_scan_chunk = 0U;
            for (std::size_t cohort = 0U;
                 routing_matches && cohort < smooth_coordinators; ++cohort) {
                std::uint64_t expected_requests = 0U;
                for (std::uint64_t index = 0U; index < 4U; ++index) {
                    expected_requests +=
                        static_cast<std::uint64_t>(
                            index % smooth_coordinators == cohort);
                }
                routing_matches =
                    result.smooth_batch_cohort_telemetry[cohort]
                        .submitted_requests == expected_requests;
                const auto& telemetry =
                    result.smooth_batch_cohort_telemetry[cohort];
                submitted_requests += telemetry.submitted_requests;
                completed_requests += telemetry.completed_requests;
                failed_requests += telemetry.failed_requests;
                cancelled_requests += telemetry.cancelled_requests;
                coordinator_batches += telemetry.coordinator_batches;
                successful_scan_chunks +=
                    telemetry.successful_cache_scan_chunks;
                submitted_orders += telemetry.submitted_orders;
                max_queued_requests = std::max(
                    max_queued_requests, telemetry.max_queued_requests);
                max_requests_per_batch = std::max(
                    max_requests_per_batch,
                    telemetry.max_requests_per_batch);
                max_orders_per_scan_chunk = std::max(
                    max_orders_per_scan_chunk,
                    telemetry.max_orders_per_successful_scan_chunk);
            }
            check(result.smooth_batch_coordinator_enabled &&
                      result.smooth_batch_coordinator_count ==
                          smooth_coordinators &&
                      routing_matches &&
                      result.smooth_batch_telemetry.submitted_requests == 4U &&
                      result.smooth_batch_telemetry.completed_requests ==
                          result.smooth_batch_telemetry.submitted_requests &&
                      result.smooth_batch_telemetry.failed_requests == 0U &&
                      result.smooth_batch_telemetry.cancelled_requests == 0U &&
                      result.smooth_batch_telemetry
                              .successful_cache_scan_chunks != 0U &&
                      result.smooth_batch_telemetry.submitted_requests ==
                          submitted_requests &&
                      result.smooth_batch_telemetry.completed_requests ==
                          completed_requests &&
                      result.smooth_batch_telemetry.failed_requests ==
                          failed_requests &&
                      result.smooth_batch_telemetry.cancelled_requests ==
                          cancelled_requests &&
                      result.smooth_batch_telemetry.coordinator_batches ==
                          coordinator_batches &&
                      result.smooth_batch_telemetry
                              .successful_cache_scan_chunks ==
                          successful_scan_chunks &&
                      result.smooth_batch_telemetry.submitted_orders ==
                          submitted_orders &&
                      result.smooth_batch_telemetry
                              .max_queued_requests_in_any_cohort ==
                          max_queued_requests &&
                      result.smooth_batch_telemetry
                              .max_requests_per_batch_in_any_cohort ==
                          max_requests_per_batch &&
                      result.smooth_batch_telemetry
                              .max_orders_per_successful_scan_chunk_in_any_cohort ==
                          max_orders_per_scan_chunk,
                  "parallel pipeline reports coordinated exact-smooth scans");
        }
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
                  "parallel live SEA and curve callbacks are serialized atomically");
            check(live_levels == report_levels,
                  "each interleaved live index subsequence stays in SEA order");
        }
        return observation;
    };

    const Observation serial = run(1U, 0U, "serial", false);
    const Observation direct2 = run(2U, 0U, "direct2", false);
    const Observation cohort2 = run(2U, 2U, "cohort2", true);
    const Observation cohort3 = run(3U, 3U, "cohort3", false);
    const Observation cohort5 = run(5U, 5U, "cohort5", false);
    for (const Observation* parallel :
         {&direct2, &cohort2, &cohort3, &cohort5}) {
        check(parallel->indices ==
                      std::vector<std::uint64_t>({0, 1, 2, 3}) &&
                  parallel->indices == serial.indices &&
                  parallel->statuses == serial.statuses &&
                  parallel->counters == serial.counters &&
                  parallel->next_index == serial.next_index,
              "coordinator cohorts preserve serial report order and outcomes");
    }

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

    oneshotsea::SearchPipelineRunOptions too_many_coordinators;
    too_many_coordinators.curve_threads = 2U;
    too_many_coordinators.smooth_coordinator_count = 3U;
    bool rejected_coordinator_count = false;
    try {
        (void)oneshotsea::run_search_pipeline(
            config, smooth, invalid, too_many_coordinators);
    } catch (const std::invalid_argument&) {
        rejected_coordinator_count = true;
    }
    check(rejected_coordinator_count,
          "smooth coordinator pool cannot exceed curve concurrency");
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
    limited.expected_smooth_cache_sha256 = smooth_sha;
    limited.expected_verifier_sha256 =
        oneshotsea::sha256_file(limited.canonical_verifier);
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
    limited.expected_smooth_cache_sha256 = smooth_sha;
    limited.expected_verifier_sha256 =
        oneshotsea::sha256_file(limited.canonical_verifier);
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
    limited.expected_smooth_cache_sha256 = smooth_sha;
    limited.expected_verifier_sha256 = verifier_sha;
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

    oneshotsea::SearchPipelineConfig skipping = limited;
    skipping.skip_incomplete_curves = true;
    const oneshotsea::SearchIdentity skipping_identity =
        oneshotsea::make_search_identity(
            skipping, {1, 2}, 0, 1, smooth_sha, verifier_sha,
            "level-limit-skip-test-v1");
    check(skipping_identity.schedule_sha256 !=
              limited_identity.schedule_sha256,
          "incomplete-skip policy is bound into schedule identity");
    skipping.expected_schedule_sha256 = skipping_identity.schedule_sha256;
    skipping.expected_smooth_cache_sha256 = smooth_sha;
    skipping.expected_verifier_sha256 = verifier_sha;
    skipping.expected_table_manifest_sha256 =
        skipping_identity.table_manifest_sha256;
    oneshotsea::SearchState skipping_state(skipping_identity);
    oneshotsea::SearchPipelineRunOptions skipping_options;
    skipping_options.max_curves = 1;
    skipping_options.checkpoint_path = temporary.path() / "skipping.json";
    skipping_options.progress_path = temporary.path() / "skipping.ndjson";
    skipping_options.certificate_path = temporary.path() / "skipping.cert";
    oneshotsea::SearchCurveStatus skipping_status =
        oneshotsea::SearchCurveStatus::sea_level_limit;
    const oneshotsea::SearchPipelineRunResult skipping_result =
        oneshotsea::run_search_pipeline(
            skipping, smooth, skipping_state, skipping_options,
            [&](const oneshotsea::SearchCurveReport& report,
                const oneshotsea::SearchState& current) {
                skipping_status = report.status;
                check(current.next_index() == 2U,
                      "heuristic incomplete skip advances cursor");
            });
    check(skipping_result.curves_processed == 1U &&
              skipping_status ==
                  oneshotsea::SearchCurveStatus::heuristic_level_limit_skip &&
              skipping_state.next_index() == 2U &&
              skipping_state.counters().curves_attempted == 1U &&
              skipping_state.counters().rejected_heuristic == 1U &&
              skipping_state.counters().rejected_sound_early_abort == 0U,
          "opt-in level-limit skip is counted only as heuristic rejection");
    {
        std::ifstream progress(skipping_options.progress_path);
        std::string event;
        std::getline(progress, event);
        check(event.find("\"status\":\"heuristic_level_limit_skip\"") !=
                      std::string::npos &&
                  event.find("\"heuristic\":true") !=
                      std::string::npos &&
                  event.find("\"outcome_class\":\"heuristic_rejection\"") !=
                      std::string::npos &&
                  event.find("\"next_index\":\"2\"") !=
                      std::string::npos,
              "progress explicitly labels incomplete heuristic skip");
    }

    oneshotsea::SearchPipelineConfig second_pass = small_config();
    second_pass.skip_incomplete_curves = true;
    const oneshotsea::SearchIdentity second_pass_identity =
        oneshotsea::make_search_identity(
            second_pass, {0, 1}, 0, 1, smooth_sha, verifier_sha,
            "second-pass-skip-test-v1");
    second_pass.expected_schedule_sha256 =
        second_pass_identity.schedule_sha256;
    second_pass.expected_smooth_cache_sha256 = smooth_sha;
    second_pass.expected_verifier_sha256 = verifier_sha;
    second_pass.expected_table_manifest_sha256 =
        second_pass_identity.table_manifest_sha256;
    oneshotsea::SearchState second_pass_state(second_pass_identity);
    oneshotsea::SearchPipelineRunOptions second_pass_options;
    second_pass_options.max_curves = 1;
    second_pass_options.checkpoint_path = temporary.path() / "second-pass.json";
    second_pass_options.progress_path = temporary.path() / "second-pass.ndjson";
    second_pass_options.certificate_path = temporary.path() / "second-pass.cert";
    bool second_pass_reached_smoothness = false;
    const auto second_pass_result = oneshotsea::run_search_pipeline(
        second_pass, smooth, second_pass_state, second_pass_options,
        [&](const oneshotsea::SearchCurveReport& report,
            const oneshotsea::SearchState&) {
            second_pass_reached_smoothness =
                report.outcome.reached_smoothness_testing;
        });
    check(second_pass_result.curves_processed == 1U &&
              second_pass_state.counters().rejected_heuristic == 1U &&
              second_pass_state.counters().candidates_reaching_smoothness ==
                  1U &&
              second_pass_reached_smoothness,
          "second-pass heuristic skip preserves completed work milestones");

    oneshotsea::SearchPipelineConfig mutated = small_config();
    const oneshotsea::SearchIdentity sound_identity =
        oneshotsea::make_search_identity(
            mutated, {0, 1}, 0, 1, smooth_sha, verifier_sha,
            "mutated-schedule-test-v1");
    mutated.expected_schedule_sha256 = sound_identity.schedule_sha256;
    mutated.expected_smooth_cache_sha256 = smooth_sha;
    mutated.expected_verifier_sha256 = verifier_sha;
    mutated.expected_table_manifest_sha256 =
        sound_identity.table_manifest_sha256;
    mutated.skip_incomplete_curves = true;
    oneshotsea::SearchState mutated_state(sound_identity);
    oneshotsea::SearchPipelineRunOptions mutated_options;
    mutated_options.max_curves = 1;
    mutated_options.checkpoint_path = temporary.path() / "mutated.json";
    mutated_options.progress_path = temporary.path() / "mutated.ndjson";
    mutated_options.certificate_path = temporary.path() / "mutated.cert";
    bool mutation_rejected = false;
    try {
        (void)oneshotsea::run_search_pipeline(
            mutated, smooth, mutated_state, mutated_options);
    } catch (const std::invalid_argument&) {
        mutation_rejected = true;
    }
    check(mutation_rejected && mutated_state.next_index() == 0U,
          "post-identity semantic mutation is rejected before processing");

    oneshotsea::SearchPipelineConfig fallback_mutated = small_config();
    fallback_mutated.expected_schedule_sha256 =
        sound_identity.schedule_sha256;
    fallback_mutated.expected_smooth_cache_sha256 = smooth_sha;
    fallback_mutated.expected_verifier_sha256 = verifier_sha;
    fallback_mutated.expected_table_manifest_sha256 =
        sound_identity.table_manifest_sha256;
    fallback_mutated.enable_schoof_fallback = true;
    oneshotsea::SearchState fallback_mutated_state(sound_identity);
    bool fallback_mutation_rejected = false;
    try {
        (void)oneshotsea::run_search_pipeline(
            fallback_mutated, smooth, fallback_mutated_state,
            mutated_options);
    } catch (const std::invalid_argument&) {
        fallback_mutation_rejected = true;
    }
    check(fallback_mutation_rejected &&
              fallback_mutated_state.next_index() == 0U,
          "post-identity Schoof-fallback mutation is rejected before processing");

    oneshotsea::SearchPipelineConfig direct_mutated = small_config();
    direct_mutated.expected_schedule_sha256 = sound_identity.schedule_sha256;
    direct_mutated.expected_smooth_cache_sha256 = smooth_sha;
    direct_mutated.expected_verifier_sha256 = verifier_sha;
    direct_mutated.expected_table_manifest_sha256 =
        sound_identity.table_manifest_sha256;
    direct_mutated.classical_direct_levels = {13U};
    oneshotsea::SearchState direct_mutated_state(sound_identity);
    bool direct_mutation_rejected = false;
    try {
        (void)oneshotsea::run_search_pipeline(
            direct_mutated, smooth, direct_mutated_state,
            mutated_options);
    } catch (const std::invalid_argument&) {
        direct_mutation_rejected = true;
    }
    check(direct_mutation_rejected &&
              direct_mutated_state.next_index() == 0U,
          "post-identity direct-SEA mutation is rejected before processing");

    oneshotsea::SearchPipelineConfig unbound = small_config();
    const oneshotsea::SearchIdentity unbound_identity =
        oneshotsea::make_search_identity(
            unbound, {0, 1}, 0, 1, smooth_sha, verifier_sha,
            "unbound-schedule-test-v1");
    oneshotsea::SearchState unbound_state(unbound_identity);
    bool unbound_rejected = false;
    try {
        (void)oneshotsea::run_search_pipeline(
            unbound, smooth, unbound_state, mutated_options);
    } catch (const std::invalid_argument&) {
        unbound_rejected = true;
    }
    check(unbound_rejected && unbound_state.next_index() == 0U,
          "search execution rejects missing authenticated identities");

    oneshotsea::SearchPipelineConfig sufficient = small_config();
    const oneshotsea::SearchIdentity sufficient_identity =
        oneshotsea::make_search_identity(
            sufficient, {1, 2}, 0, 1, smooth_sha, verifier_sha,
            "level-limit-test-v1");
    sufficient.expected_schedule_sha256 = sufficient_identity.schedule_sha256;
    sufficient.expected_smooth_cache_sha256 = smooth_sha;
    sufficient.expected_verifier_sha256 = verifier_sha;
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
                  << "rare_schoof_fallback=disabled\n"
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

    oneshotsea::SearchPipelineConfig fallback_config = config;
    fallback_config.enable_schoof_fallback = true;
    const auto fallback_identity = oneshotsea::make_search_identity(
        fallback_config, {10, 21}, 2, 3, digest, digest,
        "pipeline-test-v1");
    check(fallback_identity.schedule_sha256 != identity.schedule_sha256,
          "fixed Schoof fallback policy is bound into schedule identity");

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

    oneshotsea::SearchPipelineConfig x127_config = small_config();
    x127_config.curve_family = oneshotsea::SearchCurveFamily::x1_27;
    x127_config.x1_require_point_four = true;
    const auto x127_identity = oneshotsea::make_search_identity(
        x127_config, {10, 21}, 2, 3, digest, digest, "pipeline-test-v1");
    check(x127_identity.range == identity.range &&
              x127_identity.schedule_sha256 !=
                  x1_point_four_identity.schedule_sha256,
          "X1(27) generator and formula identity bind the schedule");

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

    oneshotsea::save_search_checkpoint(
        oneshotsea::SearchState(x1_point_four_identity), checkpoint);
    bool x127_mismatch_rejected = false;
    try {
        (void)oneshotsea::load_search_checkpoint(checkpoint, x127_identity);
    } catch (const oneshotsea::SearchCheckpointError&) {
        x127_mismatch_rejected = true;
    }
    check(x127_mismatch_rejected,
          "checkpoint rejects X1(11)/X1(27) schedule substitution");
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

void test_x1_27_curve_family_enters_search_pipeline() {
    oneshotsea::SearchPipelineConfig config = small_config();
    config.prime = 461;
    config.seed = UINT64_C(202607300000);
    config.curve_family = oneshotsea::SearchCurveFamily::x1_27;
    config.x1_require_point_four = true;
    const oneshotsea::ExactSmoothEngine smooth =
        oneshotsea::ExactSmoothEngine::build(config.prime);

    const auto report = oneshotsea::process_search_curve(
        config, smooth, 0,
        [](const oneshotsea::MontgomeryCertificate&) { return false; });
    const auto generated = oneshotsea::deterministic_x1_27_search_curve(
        config.prime, config.seed, 0, true);
    const auto& sample = *generated.sample;
    const mpz_class curve_trace =
        config.prime + 1 -
        oneshotsea::count_points_bruteforce(sample.pair.curve);
    const std::uint64_t p_plus_one =
        (mpz_fdiv_ui(config.prime.get_mpz_t(), 432U) + 1U) % 432U;
    const std::uint64_t expected_residue =
        sample.selected_side == oneshotsea::X127CanonicalSide::curve
            ? p_plus_one
            : (432U - p_plus_one) % 432U;

    check(sample.group_divisor == 432U &&
              report.trace_prior_modulus ==
                  std::optional<std::uint64_t>(432U) &&
              report.trace_prior_residue ==
                  std::optional<std::uint64_t>(expected_residue) &&
              report.exact_trace == std::optional<mpz_class>(curve_trace) &&
              report.sea_levels == 0U,
          "X1(27) point-four p=5 mod 8 divisor enters production trace prior");
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
        test_x1_27_curve_family_enters_search_pipeline();
        test_known_weber_source_lift_pipeline_determinism();
        test_sea_level_limit_does_not_advance_cursor();
        test_parallel_stop_discards_later_reports();
        test_parallel_curve_ordering_and_shared_checkpoint();
        test_small_prime_resume_and_canonical_verification();
        test_mismatched_smooth_coordinator_fails_before_sea();
        test_pool_telemetry_merge_checks_overflow_transactionally();
        test_search_uses_retained_schoof_fallback_without_second_sea_pass();
        test_search_uses_classical_direct_tail_after_weber_completion();
        test_direct_tail_preserves_remaining_weber_levels();
        test_pipeline_prepares_direct_contexts_once_per_run();
        test_pipeline_reuses_authenticated_direct_context_cache();
        test_checkpoint_bound_direct_first_strategy();
        std::cout << "search pipeline tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "search pipeline test failure: " << error.what() << '\n';
        return 1;
    }
}
