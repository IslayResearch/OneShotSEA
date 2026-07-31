#include "oneshotsea/search_checkpoint.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

class TestDirectory {
public:
    TestDirectory() {
        const auto tick =
            std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 128U; ++attempt) {
            path = std::filesystem::temp_directory_path() /
                   ("oneshotsea-search-checkpoint-" + std::to_string(tick) + "-" +
                    std::to_string(attempt));
            if (std::filesystem::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error("could not create checkpoint test directory");
    }

    ~TestDirectory() {
        std::error_code error;
        if (path.filename().string().starts_with("oneshotsea-search-checkpoint-")) {
            std::filesystem::remove_all(path, error);
        }
    }

    std::filesystem::path path;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(stream.good(), "open test file for reading");
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    check(stream.good(), "open test file for writing");
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(stream.good(), "write test file");
}

std::uint64_t crc64_ecma(std::string_view bytes) {
    constexpr std::uint64_t polynomial = UINT64_C(0x42f0e1eba9ea3693);
    std::uint64_t crc = 0;
    for (const char character : bytes) {
        const auto byte = static_cast<unsigned char>(character);
        crc ^= static_cast<std::uint64_t>(byte) << 56U;
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const bool high = (crc & UINT64_C(0x8000000000000000)) != 0;
            crc <<= 1U;
            if (high) {
                crc ^= polynomial;
            }
        }
    }
    return crc;
}

std::string hex_crc(std::uint64_t value) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string encoded(16U, '0');
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        const unsigned shift = static_cast<unsigned>((15U - index) * 4U);
        encoded[index] = digits[static_cast<std::size_t>((value >> shift) & 0xfU)];
    }
    return encoded;
}

std::string encode_payload_with_crc(const std::string& payload) {
    check(!payload.empty() && payload.back() == '}', "test payload shape");
    return payload.substr(0, payload.size() - 1U) + ",\"crc64_ecma\":\"" +
           hex_crc(crc64_ecma(payload)) + "\"}\n";
}

std::string checkpoint_payload(const std::string& encoded) {
    constexpr std::string_view marker = ",\"crc64_ecma\":\"";
    const std::size_t position = encoded.rfind(marker);
    check(position != std::string::npos, "test checkpoint has CRC marker");
    return encoded.substr(0, position) + "}";
}

oneshotsea::SearchIdentity identity(oneshotsea::SearchRange range = {100, 107}) {
    return {
        mpz_class(101),
        UINT64_C(0x123456789abcdef0),
        1,
        3,
        range,
        std::string(64U, 'a'),
        std::string(64U, 'b'),
        "f5234e0-test",
    };
}

void test_partitioning() {
    using oneshotsea::SearchRange;
    const std::array<SearchRange, 3> expected = {
        SearchRange{100, 104}, SearchRange{104, 107}, SearchRange{107, 110}};
    for (std::uint64_t worker = 0; worker < expected.size(); ++worker) {
        check(oneshotsea::partition_search_range({100, 110}, worker, 3) ==
                  expected[static_cast<std::size_t>(worker)],
              "remainder-first contiguous partition");
    }

    std::uint64_t cursor = 97;
    std::uint64_t total = 0;
    for (std::uint64_t worker = 0; worker < 17; ++worker) {
        const SearchRange shard =
            oneshotsea::partition_search_range({97, 10003}, worker, 17);
        check(shard.first == cursor && shard.first <= shard.end,
              "partition gap, overlap, or reversal");
        cursor = shard.end;
        total += shard.size();
    }
    check(cursor == 10003 && total == 10003U - 97U,
          "partitions exactly cover the global range");

    check(oneshotsea::partition_search_range({7, 9}, 0, 4) == SearchRange{7, 8} &&
              oneshotsea::partition_search_range({7, 9}, 1, 4) ==
                  SearchRange{8, 9} &&
              oneshotsea::partition_search_range({7, 9}, 2, 4).empty() &&
              oneshotsea::partition_search_range({7, 9}, 3, 4).empty(),
          "more workers than indices remain disjoint");

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const SearchRange high0 =
        oneshotsea::partition_search_range({maximum - 9U, maximum}, 0, 2);
    const SearchRange high1 =
        oneshotsea::partition_search_range({maximum - 9U, maximum}, 1, 2);
    check(high0 == SearchRange{maximum - 9U, maximum - 4U} &&
              high1 == SearchRange{maximum - 4U, maximum},
          "partition arithmetic near uint64 limit");

    expect_failure(
        [] { (void)oneshotsea::partition_search_range({2, 1}, 0, 1); },
        "reversed global range accepted");
    expect_failure(
        [] { (void)oneshotsea::partition_search_range({0, 1}, 0, 0); },
        "zero worker count accepted");
    expect_failure(
        [] { (void)oneshotsea::partition_search_range({0, 1}, 2, 2); },
        "out-of-range worker id accepted");
}

void test_state_and_counters() {
    using oneshotsea::CurveSearchOutcome;
    using oneshotsea::CurveTerminalStage;
    oneshotsea::SearchState state(identity());
    const std::array<CurveSearchOutcome, 7> outcomes = {{
        {CurveTerminalStage::rejected_invalid_curve, false, false},
        {CurveTerminalStage::rejected_sea, false, false},
        {CurveTerminalStage::rejected_sound_early_abort, false, true},
        {CurveTerminalStage::rejected_heuristic, false, false},
        {CurveTerminalStage::rejected_certificate_assembly, true, true},
        {CurveTerminalStage::completed_without_certificate, true, true},
        {CurveTerminalStage::verified_certificate_found, true, true},
    }};
    std::size_t callback_index = 0;
    const std::uint64_t first = oneshotsea::run_search_chunk(
        state, 3, [&](std::uint64_t index) {
            check(index == 100U + callback_index, "deterministic callback index");
            return outcomes.at(callback_index++);
        });
    check(first == 3 && state.next_index() == 103 &&
              state.counters().curves_attempted == 3,
          "bounded first search chunk");
    check(oneshotsea::run_search_chunk(
              state, 99, [&](std::uint64_t index) {
                  check(index == 100U + callback_index,
                        "deterministic resumed callback index");
                  return outcomes.at(callback_index++);
              }) == 4,
          "chunk stops at range end");
    const auto& counters = state.counters();
    check(state.complete() && state.next_index() == 107 && callback_index == 7 &&
              counters.curves_attempted == 7 &&
              counters.rejected_invalid_curve == 1 && counters.rejected_sea == 1 &&
              counters.rejected_sound_early_abort == 1 &&
              counters.rejected_heuristic == 1 &&
              counters.rejected_certificate_assembly == 1 &&
              counters.completed_without_certificate == 1 &&
              counters.full_point_counts_completed == 3 &&
              counters.candidates_reaching_smoothness == 4 &&
              counters.certificates_found == 1,
          "exact terminal and pipeline counters");
    check(oneshotsea::run_search_chunk(
              state, 1, [](std::uint64_t) -> CurveSearchOutcome {
                  throw std::runtime_error("completed state invoked callback");
              }) == 0,
          "completed range does no further work");
    expect_failure(
        [&] {
            state.record_completed(
                107, {CurveTerminalStage::rejected_sea, false, false});
        },
        "completion beyond range accepted");

    oneshotsea::SearchState strict(identity({400, 402}));
    expect_failure(
        [&] {
            strict.record_completed(
                401, {CurveTerminalStage::rejected_sea, false, false});
        },
        "skipped curve index accepted");
    expect_failure(
        [&] {
            strict.record_completed(
                400,
                {CurveTerminalStage::verified_certificate_found, false, true});
        },
        "unchecked certificate outcome accepted");
    expect_failure(
        [&] {
            strict.record_completed(
                400,
                {CurveTerminalStage::rejected_certificate_assembly, true, false});
        },
        "premature certificate-assembly outcome accepted");
    expect_failure(
        [&] {
            strict.record_completed(
                400, {CurveTerminalStage::rejected_invalid_curve, true, false});
        },
        "invalid curve with point count accepted");
    check(strict.next_index() == 400 && strict.counters().curves_attempted == 0,
          "rejected state transitions are transactional");

    oneshotsea::SearchState empty(identity({9, 9}));
    check(empty.complete() && empty.counters().curves_attempted == 0,
          "empty assigned range is complete");
}

void test_checkpoint_resume_and_progress() {
    using oneshotsea::CurveSearchOutcome;
    using oneshotsea::CurveTerminalStage;
    TestDirectory temporary;
    const auto checkpoint = temporary.path / "checkpoint.json";
    const auto progress = temporary.path / "progress.jsonl";
    const auto expected = identity({500, 503});
    oneshotsea::SearchState state(expected);

    bool interrupted = false;
    try {
        (void)oneshotsea::run_search_chunk(
            state, 3, [](std::uint64_t index) -> CurveSearchOutcome {
                if (index == 501) {
                    throw std::runtime_error("simulated worker interruption");
                }
                return {CurveTerminalStage::rejected_sea, false, false};
            });
    } catch (const std::runtime_error&) {
        interrupted = true;
    }
    check(interrupted && state.next_index() == 501 &&
              state.counters().curves_attempted == 1,
          "interrupted curve remains uncommitted for replay");

    oneshotsea::save_search_checkpoint(state, checkpoint);
    const std::string first_encoding = read_file(checkpoint);
    check(first_encoding.starts_with("{\"schema_version\":1,") &&
              first_encoding.ends_with("\"}\n") &&
              first_encoding.find("\"crc64_ecma\":\"") != std::string::npos,
          "canonical checksummed checkpoint encoding");

    oneshotsea::SearchState resumed =
        oneshotsea::load_search_checkpoint(checkpoint, expected);
    check(resumed.next_index() == 501 && resumed.counters() == state.counters(),
          "checkpoint roundtrip preserves cursor and counters");
    std::vector<std::uint64_t> replayed;
    check(oneshotsea::run_search_chunk(
              resumed, 9, [&](std::uint64_t index) {
                  replayed.push_back(index);
                  return CurveSearchOutcome{
                      CurveTerminalStage::completed_without_certificate, true, true};
              }) == 2 &&
              replayed == std::vector<std::uint64_t>({501, 502}) &&
              resumed.complete(),
          "resume replays exactly the first unfinished curve");

    const std::string json = oneshotsea::search_progress_json(resumed);
    check(!json.empty() && json.front() == '{' && json.back() == '}' &&
              json.find('\n') == std::string::npos &&
              json.find("\"complete\":true") != std::string::npos &&
              json.find("\"curves_attempted\":\"3\"") != std::string::npos &&
              json.find("\"full_point_counts_completed\":\"2\"") !=
                  std::string::npos &&
              json.find("\"certificates_found\":\"0\"") != std::string::npos,
          "precision-safe machine-readable progress counters");
    oneshotsea::append_search_progress_jsonl(state, progress);
    oneshotsea::append_search_progress_jsonl(resumed, progress);
    const std::string jsonl = read_file(progress);
    check(std::count(jsonl.begin(), jsonl.end(), '\n') == 2 &&
              jsonl == oneshotsea::search_progress_json(state) + "\n" + json + "\n",
          "append-only NDJSON progress records");

    oneshotsea::save_search_checkpoint(resumed, checkpoint);
    check(read_file(checkpoint) != first_encoding,
          "atomic checkpoint replacement publishes new state");
    for (const auto& entry : std::filesystem::directory_iterator(temporary.path)) {
        check(entry.path().filename().string().find(".tmp.") == std::string::npos,
              "checkpoint save left temporary file");
    }

    auto mismatched = expected;
    ++mismatched.seed;
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(checkpoint, mismatched); },
        "checkpoint accepted for another seed");
    mismatched = expected;
    mismatched.schedule_sha256 = std::string(64U, 'c');
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(checkpoint, mismatched); },
        "checkpoint accepted for another SEA schedule");
    mismatched = expected;
    mismatched.table_manifest_sha256 = std::string(64U, 'd');
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(checkpoint, mismatched); },
        "checkpoint accepted for another table manifest");
    mismatched = expected;
    mismatched.build_id = "another-build";
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(checkpoint, mismatched); },
        "checkpoint accepted for another build");

    const std::string valid = read_file(checkpoint);
    std::string corrupt = valid;
    const std::size_t prime_position = corrupt.find("\"prime\":\"101\"");
    check(prime_position != std::string::npos, "locate target for corruption test");
    corrupt[prime_position + 10U] = '3';
    const auto corrupt_path = temporary.path / "corrupt.json";
    write_file(corrupt_path, corrupt);
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(corrupt_path, expected); },
        "checksum corruption accepted");

    const auto truncated_path = temporary.path / "truncated.json";
    write_file(truncated_path, valid.substr(0, valid.size() - 1U));
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(truncated_path, expected); },
        "truncated checkpoint accepted");

    const auto trailing_path = temporary.path / "trailing.json";
    write_file(trailing_path, valid + "x");
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(trailing_path, expected); },
        "checkpoint with trailing bytes accepted");

    std::string invalid_payload = checkpoint_payload(valid);
    const std::string old_cursor = "\"next_index\":\"503\"";
    const std::size_t cursor_position = invalid_payload.find(old_cursor);
    check(cursor_position != std::string::npos, "locate cursor for invariant test");
    invalid_payload.replace(cursor_position, old_cursor.size(),
                            "\"next_index\":\"502\"");
    const auto invariant_path = temporary.path / "invalid-invariants.json";
    write_file(invariant_path, encode_payload_with_crc(invalid_payload));
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(invariant_path, expected); },
        "valid-checksum checkpoint with inconsistent cursor accepted");

    const auto excessive_path = temporary.path / "excessive.json";
    write_file(excessive_path, std::string(2U * 1024U * 1024U + 1U, 'x'));
    expect_failure(
        [&] { (void)oneshotsea::load_search_checkpoint(excessive_path, expected); },
        "excessive checkpoint file accepted");
}

void test_identity_validation() {
    auto invalid = identity();
    invalid.prime = 100;
    expect_failure([&] { (void)oneshotsea::SearchState(invalid); },
                   "even target accepted");
    invalid = identity();
    invalid.worker_count = 0;
    expect_failure([&] { (void)oneshotsea::SearchState(invalid); },
                   "zero workers accepted");
    invalid = identity();
    invalid.schedule_sha256 = "not-a-digest";
    expect_failure([&] { (void)oneshotsea::SearchState(invalid); },
                   "unbound schedule accepted");
    invalid = identity();
    invalid.build_id = "bad build id";
    expect_failure([&] { (void)oneshotsea::SearchState(invalid); },
                   "unsafe build id accepted");
}

}  // namespace

int main() {
    test_partitioning();
    test_state_and_counters();
    test_checkpoint_resume_and_progress();
    test_identity_validation();
    std::cout << "all search-checkpoint tests passed\n";
    return 0;
}
