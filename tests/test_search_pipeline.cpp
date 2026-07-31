#include "oneshotsea/search_pipeline.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
        test_non_python_success_program_is_rejected();
        test_verifier_runtime_failure_is_not_a_rejection();
        test_bounded_early_screen_default();
        test_worker_partition_is_identity_bound();
        test_sea_level_limit_does_not_advance_cursor();
        test_small_prime_resume_and_canonical_verification();
        std::cout << "search pipeline tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "search pipeline test failure: " << error.what() << '\n';
        return 1;
    }
}
