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
    config.seed = 17;
    config.table_directory = "data/modpoly/weber_f";
    config.max_level = 31;
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
        config, {0, 2}, 0, 1, smooth_sha, verifier_sha, "pipeline-test-v1");
    config.expected_schedule_sha256 = identity.schedule_sha256;
    config.expected_table_manifest_sha256 =
        identity.table_manifest_sha256;
    oneshotsea::SearchState state(identity);

    oneshotsea::SearchPipelineRunOptions first;
    first.max_curves = 1;
    first.checkpoint_path = checkpoint;
    first.progress_path = progress;
    first.certificate_path = temporary.path() / "certificate.txt";
    const auto first_result = oneshotsea::run_search_pipeline(
        config, smooth, state, first);
    check(first_result.curves_processed == 1U &&
              !first_result.verified.has_value() && state.next_index() == 1U,
          "capped first search chunk");
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
    const auto second_result = oneshotsea::run_search_pipeline(
        config, smooth, resumed, second, {}, canonical);
    check(second_result.curves_processed == 1U &&
              second_result.verified.has_value(),
          "resumed search finds deterministic certificate");
    check(canonical_calls == 1U,
          "pipeline invoked the unmodified canonical verifier before success");
    check(second_result.verified->certificate->line() == "101 5 8 28",
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
    check(event_count == 2U, "one NDJSON event per completed curve");
}

void test_worker_partition_is_identity_bound() {
    const oneshotsea::SearchPipelineConfig config = small_config();
    const std::string digest(64U, 'a');
    const auto identity = oneshotsea::make_search_identity(
        config, {10, 21}, 2, 3, digest, digest, "pipeline-test-v1");
    check(identity.range == oneshotsea::SearchRange{18, 21},
          "pipeline identity uses deterministic disjoint worker shard");
    check(identity.schedule_sha256.size() == 64U &&
              identity.table_manifest_sha256.size() == 64U,
          "pipeline identity binds schedule and table content");
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
        test_worker_partition_is_identity_bound();
        test_small_prime_resume_and_canonical_verification();
        std::cout << "search pipeline tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "search pipeline test failure: " << error.what() << '\n';
        return 1;
    }
}
