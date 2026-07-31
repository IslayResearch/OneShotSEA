#include "oneshotsea/search_checkpoint.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace oneshotsea {
namespace {

constexpr std::uint64_t kCrc64Polynomial = UINT64_C(0x42f0e1eba9ea3693);
constexpr std::uint64_t kMaximumCheckpointBytes = UINT64_C(2) * 1024U * 1024U;
constexpr std::size_t kMaximumPrimeDigits = 1024U * 1024U;
constexpr std::size_t kMaximumBuildIdBytes = 256U;

std::string errno_text(const std::string& operation,
                       const std::filesystem::path& path) {
    return operation + " " + path.string() + ": " + std::strerror(errno);
}

bool is_lower_hex_digest(const std::string& value) {
    if (value.size() != 64U) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

bool is_safe_build_id(const std::string& value) {
    if (value.empty() || value.size() > kMaximumBuildIdBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '.' ||
               character == '_' || character == '/' || character == '+' ||
               character == ':' || character == '-';
    });
}

void validate_identity(const SearchIdentity& identity) {
    if (identity.prime <= 2 || mpz_even_p(identity.prime.get_mpz_t()) != 0) {
        throw SearchCheckpointError("search target must be an odd integer greater than two");
    }
    const std::string prime = identity.prime.get_str();
    if (prime.size() > kMaximumPrimeDigits) {
        throw SearchCheckpointError("search target exceeds checkpoint digit limit");
    }
    if (identity.worker_count == 0 || identity.worker_id >= identity.worker_count) {
        throw SearchCheckpointError("invalid search worker identity");
    }
    if (identity.range.first > identity.range.end) {
        throw SearchCheckpointError("search range is reversed");
    }
    if (!is_lower_hex_digest(identity.schedule_sha256)) {
        throw SearchCheckpointError("schedule SHA-256 must be 64 lowercase hex digits");
    }
    if (!is_lower_hex_digest(identity.table_manifest_sha256)) {
        throw SearchCheckpointError(
            "table-manifest SHA-256 must be 64 lowercase hex digits");
    }
    if (!is_safe_build_id(identity.build_id)) {
        throw SearchCheckpointError("build id contains unsupported characters or length");
    }
}

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right,
                          const char* label) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw SearchCheckpointError(std::string("search counter overflow: ") + label);
    }
    return left + right;
}

std::uint64_t terminal_count(const SearchCounters& counters) {
    std::uint64_t total = 0;
    total = checked_add(total, counters.rejected_invalid_curve, "terminal total");
    total = checked_add(total, counters.rejected_sea, "terminal total");
    total = checked_add(total, counters.rejected_sound_early_abort, "terminal total");
    total = checked_add(total, counters.rejected_heuristic, "terminal total");
    total = checked_add(total, counters.rejected_certificate_assembly,
                        "terminal total");
    total = checked_add(total, counters.completed_without_certificate,
                        "terminal total");
    total = checked_add(total, counters.certificates_found, "terminal total");
    return total;
}

void validate_counters(const SearchIdentity& identity, std::uint64_t next_index,
                       const SearchCounters& counters) {
    if (next_index < identity.range.first || next_index > identity.range.end) {
        throw SearchCheckpointError("checkpoint cursor lies outside assigned range");
    }
    if (counters.curves_attempted != next_index - identity.range.first) {
        throw SearchCheckpointError("checkpoint cursor and attempted count disagree");
    }
    if (terminal_count(counters) != counters.curves_attempted) {
        throw SearchCheckpointError("terminal-stage counts do not cover attempted curves");
    }
    if (counters.full_point_counts_completed > counters.curves_attempted ||
        counters.candidates_reaching_smoothness > counters.curves_attempted ||
        counters.certificates_found > counters.full_point_counts_completed ||
        counters.certificates_found > counters.candidates_reaching_smoothness ||
        counters.rejected_certificate_assembly >
            counters.full_point_counts_completed ||
        counters.rejected_certificate_assembly >
            counters.candidates_reaching_smoothness) {
        throw SearchCheckpointError("search counters violate pipeline invariants");
    }
}

void validate_outcome(const CurveSearchOutcome& outcome) {
    if (outcome.terminal_stage ==
            CurveTerminalStage::verified_certificate_found &&
        (!outcome.full_point_count_completed ||
         !outcome.reached_smoothness_testing)) {
        throw SearchCheckpointError(
            "certificate outcome requires point count and smoothness testing");
    }
    if (outcome.terminal_stage ==
            CurveTerminalStage::rejected_certificate_assembly &&
        (!outcome.full_point_count_completed ||
         !outcome.reached_smoothness_testing)) {
        throw SearchCheckpointError(
            "certificate-assembly rejection requires point count and smoothness testing");
    }
    if (outcome.terminal_stage == CurveTerminalStage::rejected_invalid_curve &&
        (outcome.full_point_count_completed || outcome.reached_smoothness_testing)) {
        throw SearchCheckpointError(
            "invalid-curve rejection cannot report downstream work");
    }
}

std::uint64_t incremented(std::uint64_t value, const char* label) {
    return checked_add(value, 1U, label);
}

std::uint64_t crc64_ecma(std::string_view bytes) {
    std::uint64_t crc = 0;
    for (const char character : bytes) {
        const auto byte = static_cast<unsigned char>(character);
        crc ^= static_cast<std::uint64_t>(byte) << 56U;
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const bool high = (crc & UINT64_C(0x8000000000000000)) != 0;
            crc <<= 1U;
            if (high) {
                crc ^= kCrc64Polynomial;
            }
        }
    }
    return crc;
}

std::string checkpoint_payload(const SearchState& state) {
    const SearchIdentity& identity = state.identity();
    const SearchCounters& counters = state.counters();
    std::ostringstream output;
    output << "{\"schema_version\":" << kSearchCheckpointVersion
           << ",\"prime\":\"" << identity.prime
           << "\",\"seed\":\"" << identity.seed
           << "\",\"worker_id\":\"" << identity.worker_id
           << "\",\"worker_count\":\"" << identity.worker_count
           << "\",\"range_start\":\"" << identity.range.first
           << "\",\"range_end\":\"" << identity.range.end
           << "\",\"schedule_sha256\":\"" << identity.schedule_sha256
           << "\",\"table_manifest_sha256\":\""
           << identity.table_manifest_sha256 << "\",\"build_id\":\""
           << identity.build_id << "\",\"next_index\":\"" << state.next_index()
           << "\",\"counters\":{\"curves_attempted\":\""
           << counters.curves_attempted << "\",\"rejected_invalid_curve\":\""
           << counters.rejected_invalid_curve << "\",\"rejected_sea\":\""
           << counters.rejected_sea
           << "\",\"rejected_sound_early_abort\":\""
           << counters.rejected_sound_early_abort
           << "\",\"rejected_heuristic\":\"" << counters.rejected_heuristic
           << "\",\"rejected_certificate_assembly\":\""
           << counters.rejected_certificate_assembly
           << "\",\"completed_without_certificate\":\""
           << counters.completed_without_certificate
           << "\",\"full_point_counts_completed\":\""
           << counters.full_point_counts_completed
           << "\",\"candidates_reaching_smoothness\":\""
           << counters.candidates_reaching_smoothness
           << "\",\"certificates_found\":\"" << counters.certificates_found
           << "\"}}";
    return output.str();
}

std::string hex_crc(std::uint64_t value) {
    static constexpr std::array<char, 16> digits = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result(16, '0');
    for (std::size_t index = 0; index < result.size(); ++index) {
        const unsigned shift = static_cast<unsigned>((15U - index) * 4U);
        result[index] = digits[static_cast<std::size_t>((value >> shift) & 0xfU)];
    }
    return result;
}

std::uint64_t parse_hex_crc(std::string_view value) {
    if (value.size() != 16U) {
        throw SearchCheckpointError("invalid checkpoint checksum width");
    }
    std::uint64_t result = 0;
    for (char character : value) {
        result <<= 4U;
        if (character >= '0' && character <= '9') {
            result |= static_cast<std::uint64_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            result |= static_cast<std::uint64_t>(character - 'a' + 10);
        } else {
            throw SearchCheckpointError("invalid checkpoint checksum encoding");
        }
    }
    return result;
}

class CanonicalParser {
public:
    explicit CanonicalParser(std::string_view input) : input_(input) {}

    void expect(std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            throw SearchCheckpointError("noncanonical or malformed checkpoint JSON");
        }
        position_ += literal.size();
    }

    std::string quoted() {
        const std::size_t end = input_.find('"', position_);
        if (end == std::string_view::npos) {
            throw SearchCheckpointError("unterminated checkpoint string");
        }
        std::string result(input_.substr(position_, end - position_));
        position_ = end + 1U;
        return result;
    }

    std::uint64_t quoted_u64() {
        const std::string value = quoted();
        if (value.empty() || (value.size() > 1U && value.front() == '0')) {
            throw SearchCheckpointError("noncanonical checkpoint integer");
        }
        std::uint64_t parsed = 0;
        const auto conversion =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (conversion.ec != std::errc() ||
            conversion.ptr != value.data() + value.size()) {
            throw SearchCheckpointError("invalid or excessive checkpoint integer");
        }
        return parsed;
    }

    void finish() const {
        if (position_ != input_.size()) {
            throw SearchCheckpointError("trailing checkpoint JSON data");
        }
    }

private:
    std::string_view input_;
    std::size_t position_ = 0;
};

struct ParsedCheckpoint {
    SearchIdentity identity;
    std::uint64_t next_index = 0;
    SearchCounters counters;
};

ParsedCheckpoint parse_payload(std::string_view payload) {
    CanonicalParser parser(payload);
    ParsedCheckpoint parsed;
    parser.expect("{\"schema_version\":1,\"prime\":\"");
    const std::string prime = parser.quoted();
    if (prime.empty() || prime.size() > kMaximumPrimeDigits ||
        (prime.size() > 1U && prime.front() == '0') ||
        !std::all_of(prime.begin(), prime.end(), [](char character) {
            return character >= '0' && character <= '9';
        }) ||
        parsed.identity.prime.set_str(prime, 10) != 0) {
        throw SearchCheckpointError("invalid checkpoint target integer");
    }
    parser.expect(",\"seed\":\"");
    parsed.identity.seed = parser.quoted_u64();
    parser.expect(",\"worker_id\":\"");
    parsed.identity.worker_id = parser.quoted_u64();
    parser.expect(",\"worker_count\":\"");
    parsed.identity.worker_count = parser.quoted_u64();
    parser.expect(",\"range_start\":\"");
    parsed.identity.range.first = parser.quoted_u64();
    parser.expect(",\"range_end\":\"");
    parsed.identity.range.end = parser.quoted_u64();
    parser.expect(",\"schedule_sha256\":\"");
    parsed.identity.schedule_sha256 = parser.quoted();
    parser.expect(",\"table_manifest_sha256\":\"");
    parsed.identity.table_manifest_sha256 = parser.quoted();
    parser.expect(",\"build_id\":\"");
    parsed.identity.build_id = parser.quoted();
    parser.expect(",\"next_index\":\"");
    parsed.next_index = parser.quoted_u64();
    parser.expect(",\"counters\":{\"curves_attempted\":\"");
    parsed.counters.curves_attempted = parser.quoted_u64();
    parser.expect(",\"rejected_invalid_curve\":\"");
    parsed.counters.rejected_invalid_curve = parser.quoted_u64();
    parser.expect(",\"rejected_sea\":\"");
    parsed.counters.rejected_sea = parser.quoted_u64();
    parser.expect(",\"rejected_sound_early_abort\":\"");
    parsed.counters.rejected_sound_early_abort = parser.quoted_u64();
    parser.expect(",\"rejected_heuristic\":\"");
    parsed.counters.rejected_heuristic = parser.quoted_u64();
    parser.expect(",\"rejected_certificate_assembly\":\"");
    parsed.counters.rejected_certificate_assembly = parser.quoted_u64();
    parser.expect(",\"completed_without_certificate\":\"");
    parsed.counters.completed_without_certificate = parser.quoted_u64();
    parser.expect(",\"full_point_counts_completed\":\"");
    parsed.counters.full_point_counts_completed = parser.quoted_u64();
    parser.expect(",\"candidates_reaching_smoothness\":\"");
    parsed.counters.candidates_reaching_smoothness = parser.quoted_u64();
    parser.expect(",\"certificates_found\":\"");
    parsed.counters.certificates_found = parser.quoted_u64();
    parser.expect("}}");
    parser.finish();
    validate_identity(parsed.identity);
    validate_counters(parsed.identity, parsed.next_index, parsed.counters);
    return parsed;
}

class TemporaryFile {
public:
    TemporaryFile() = default;
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    TemporaryFile(TemporaryFile&& other) noexcept
        : descriptor(std::exchange(other.descriptor, -1)), path(std::move(other.path)) {
        other.path.clear();
    }
    ~TemporaryFile() {
        if (descriptor >= 0) {
            ::close(descriptor);
        }
        if (!path.empty()) {
            ::unlink(path.c_str());
        }
    }

    int descriptor = -1;
    std::filesystem::path path;
};

TemporaryFile open_temporary(const std::filesystem::path& target) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::uint64_t attempt = 0; attempt < 128U; ++attempt) {
        TemporaryFile temporary;
        temporary.path = target.string() + ".tmp." + std::to_string(tick) + "." +
                         std::to_string(sequence.fetch_add(1)) + "." +
                         std::to_string(attempt);
        temporary.descriptor =
            ::open(temporary.path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (temporary.descriptor >= 0) {
            return temporary;
        }
        if (errno != EEXIST) {
            throw SearchCheckpointError(
                errno_text("cannot create temporary checkpoint", temporary.path));
        }
    }
    throw SearchCheckpointError("cannot allocate temporary checkpoint path");
}

void write_all(int descriptor, std::string_view bytes,
               const std::filesystem::path& path) {
    while (!bytes.empty()) {
        const ssize_t written = ::write(descriptor, bytes.data(), bytes.size());
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw SearchCheckpointError(errno_text("cannot write", path));
        }
        if (written == 0) {
            throw SearchCheckpointError("short write to " + path.string());
        }
        bytes.remove_prefix(static_cast<std::size_t>(written));
    }
}

class ReadFile {
public:
    explicit ReadFile(const std::filesystem::path& path) : path_(path) {
        descriptor_ = ::open(path.c_str(), O_RDONLY);
        if (descriptor_ < 0) {
            throw SearchCheckpointError(errno_text("cannot open checkpoint", path));
        }
    }
    ReadFile(const ReadFile&) = delete;
    ReadFile& operator=(const ReadFile&) = delete;
    ~ReadFile() { ::close(descriptor_); }

    std::string read_bounded() const {
        struct stat attributes {};
        if (::fstat(descriptor_, &attributes) != 0) {
            throw SearchCheckpointError(errno_text("cannot stat checkpoint", path_));
        }
        if (!S_ISREG(attributes.st_mode) || attributes.st_size <= 0 ||
            static_cast<std::uint64_t>(attributes.st_size) > kMaximumCheckpointBytes) {
            throw SearchCheckpointError("checkpoint is not a bounded regular file");
        }
        std::string bytes(static_cast<std::size_t>(attributes.st_size), '\0');
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const ssize_t received =
                ::read(descriptor_, bytes.data() + offset, bytes.size() - offset);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw SearchCheckpointError(errno_text("cannot read checkpoint", path_));
            }
            if (received == 0) {
                throw SearchCheckpointError("truncated checkpoint " + path_.string());
            }
            offset += static_cast<std::size_t>(received);
        }
        return bytes;
    }

private:
    int descriptor_ = -1;
    std::filesystem::path path_;
};

void sync_parent_directory(const std::filesystem::path& target) {
    const std::filesystem::path parent =
        target.parent_path().empty() ? std::filesystem::path(".")
                                     : target.parent_path();
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    const int descriptor = ::open(parent.c_str(), flags);
    if (descriptor < 0) {
        throw SearchCheckpointError(errno_text("cannot open checkpoint directory", parent));
    }
    const int result = ::fsync(descriptor);
    const int saved_errno = errno;
    ::close(descriptor);
    if (result != 0) {
        errno = saved_errno;
        throw SearchCheckpointError(errno_text("cannot flush checkpoint directory", parent));
    }
}

}  // namespace

SearchRange partition_search_range(SearchRange global, std::uint64_t worker_id,
                                   std::uint64_t worker_count) {
    if (global.first > global.end) {
        throw std::invalid_argument("cannot partition a reversed search range");
    }
    if (worker_count == 0 || worker_id >= worker_count) {
        throw std::invalid_argument("invalid worker id/count for search partition");
    }
    const std::uint64_t count = global.end - global.first;
    const std::uint64_t base = count / worker_count;
    const std::uint64_t remainder = count % worker_count;
    const std::uint64_t extra_before = std::min(worker_id, remainder);
    const std::uint64_t offset = worker_id * base + extra_before;
    const std::uint64_t shard_size = base + (worker_id < remainder ? 1U : 0U);
    return {global.first + offset, global.first + offset + shard_size};
}

SearchState::SearchState(SearchIdentity identity)
    : identity_(std::move(identity)), next_index_(identity_.range.first) {
    validate_identity(identity_);
}

void SearchState::record_completed(std::uint64_t index,
                                   const CurveSearchOutcome& outcome) {
    validate_counters(identity_, next_index_, counters_);
    validate_outcome(outcome);
    if (complete()) {
        throw SearchCheckpointError("cannot record a curve after the assigned range");
    }
    if (index != next_index_) {
        throw SearchCheckpointError("curve completion is not the next assigned index");
    }

    SearchCounters updated = counters_;
    updated.curves_attempted = incremented(updated.curves_attempted, "curves attempted");
    switch (outcome.terminal_stage) {
        case CurveTerminalStage::rejected_invalid_curve:
            updated.rejected_invalid_curve =
                incremented(updated.rejected_invalid_curve, "invalid curve rejects");
            break;
        case CurveTerminalStage::rejected_sea:
            updated.rejected_sea = incremented(updated.rejected_sea, "SEA rejects");
            break;
        case CurveTerminalStage::rejected_sound_early_abort:
            updated.rejected_sound_early_abort = incremented(
                updated.rejected_sound_early_abort, "sound early-abort rejects");
            break;
        case CurveTerminalStage::rejected_heuristic:
            updated.rejected_heuristic =
                incremented(updated.rejected_heuristic, "heuristic rejects");
            break;
        case CurveTerminalStage::rejected_certificate_assembly:
            updated.rejected_certificate_assembly = incremented(
                updated.rejected_certificate_assembly, "certificate assembly rejects");
            break;
        case CurveTerminalStage::completed_without_certificate:
            updated.completed_without_certificate = incremented(
                updated.completed_without_certificate, "completed without certificate");
            break;
        case CurveTerminalStage::verified_certificate_found:
            updated.certificates_found =
                incremented(updated.certificates_found, "certificates found");
            break;
    }
    if (outcome.full_point_count_completed) {
        updated.full_point_counts_completed = incremented(
            updated.full_point_counts_completed, "full point counts completed");
    }
    if (outcome.reached_smoothness_testing) {
        updated.candidates_reaching_smoothness = incremented(
            updated.candidates_reaching_smoothness, "smoothness candidates");
    }

    const std::uint64_t updated_next = next_index_ + 1U;
    validate_counters(identity_, updated_next, updated);
    counters_ = updated;
    next_index_ = updated_next;
}

std::uint64_t run_search_chunk(SearchState& state, std::uint64_t max_curves,
                               const CurveSearchProcessor& processor) {
    if (!processor) {
        throw std::invalid_argument("search processor callback is empty");
    }
    std::uint64_t processed = 0;
    while (!state.complete() && processed < max_curves) {
        const std::uint64_t index = state.next_index();
        const CurveSearchOutcome outcome = processor(index);
        state.record_completed(index, outcome);
        ++processed;
    }
    return processed;
}

void save_search_checkpoint(const SearchState& state,
                            const std::filesystem::path& checkpoint_path) {
    if (checkpoint_path.empty()) {
        throw SearchCheckpointError("checkpoint path is empty");
    }
    validate_identity(state.identity_);
    validate_counters(state.identity_, state.next_index_, state.counters_);
    const std::string payload = checkpoint_payload(state);
    std::string encoded = payload.substr(0, payload.size() - 1U);
    encoded += ",\"crc64_ecma\":\"" + hex_crc(crc64_ecma(payload)) + "\"}\n";
    if (encoded.size() > kMaximumCheckpointBytes) {
        throw SearchCheckpointError("checkpoint exceeds byte limit");
    }

    TemporaryFile temporary = open_temporary(checkpoint_path);
    write_all(temporary.descriptor, encoded, temporary.path);
    if (::fsync(temporary.descriptor) != 0) {
        throw SearchCheckpointError(errno_text("cannot flush checkpoint", temporary.path));
    }
    const int descriptor = temporary.descriptor;
    temporary.descriptor = -1;
    if (::close(descriptor) != 0) {
        throw SearchCheckpointError(errno_text("cannot close checkpoint", temporary.path));
    }
    if (::rename(temporary.path.c_str(), checkpoint_path.c_str()) != 0) {
        throw SearchCheckpointError(
            errno_text("cannot atomically rename checkpoint", checkpoint_path));
    }
    temporary.path.clear();
    sync_parent_directory(checkpoint_path);
}

SearchState load_search_checkpoint(const std::filesystem::path& checkpoint_path,
                                   const SearchIdentity& expected_identity) {
    validate_identity(expected_identity);
    const std::string encoded = ReadFile(checkpoint_path).read_bounded();
    constexpr std::string_view marker = ",\"crc64_ecma\":\"";
    if (!encoded.ends_with("\"}\n")) {
        throw SearchCheckpointError("checkpoint lacks canonical line ending");
    }
    const std::size_t marker_position = encoded.rfind(marker);
    if (marker_position == std::string::npos ||
        marker_position + marker.size() + 16U + 3U != encoded.size()) {
        throw SearchCheckpointError("checkpoint checksum field is malformed");
    }
    const std::uint64_t recorded_crc = parse_hex_crc(
        std::string_view(encoded).substr(marker_position + marker.size(), 16U));
    const std::string payload = encoded.substr(0, marker_position) + "}";
    if (crc64_ecma(payload) != recorded_crc) {
        throw SearchCheckpointError("checkpoint checksum mismatch");
    }
    ParsedCheckpoint parsed = parse_payload(payload);
    if (!(parsed.identity == expected_identity)) {
        throw SearchCheckpointError("checkpoint identity does not match requested search");
    }
    SearchState state(parsed.identity);
    state.next_index_ = parsed.next_index;
    state.counters_ = parsed.counters;
    return state;
}

std::string search_progress_json(const SearchState& state) {
    validate_identity(state.identity_);
    validate_counters(state.identity_, state.next_index_, state.counters_);
    const SearchIdentity& identity = state.identity_;
    const SearchCounters& counters = state.counters_;
    std::ostringstream output;
    output << "{\"schema\":\"oneshotsea.search-progress.v1\",\"prime\":\""
           << identity.prime << "\",\"seed\":\"" << identity.seed
           << "\",\"worker_id\":\"" << identity.worker_id
           << "\",\"worker_count\":\"" << identity.worker_count
           << "\",\"range_start\":\"" << identity.range.first
           << "\",\"range_end\":\"" << identity.range.end
           << "\",\"schedule_sha256\":\"" << identity.schedule_sha256
           << "\",\"table_manifest_sha256\":\""
           << identity.table_manifest_sha256 << "\",\"build_id\":\""
           << identity.build_id
           << "\",\"next_index\":\"" << state.next_index_
           << "\",\"complete\":" << (state.complete() ? "true" : "false")
           << ",\"counters\":{\"curves_attempted\":\""
           << counters.curves_attempted << "\",\"rejections\":{\"invalid_curve\":\""
           << counters.rejected_invalid_curve << "\",\"sea\":\""
           << counters.rejected_sea << "\",\"sound_early_abort\":\""
           << counters.rejected_sound_early_abort << "\",\"heuristic\":\""
           << counters.rejected_heuristic << "\",\"certificate_assembly\":\""
           << counters.rejected_certificate_assembly
           << "\"},\"completed_without_certificate\":\""
           << counters.completed_without_certificate
           << "\",\"full_point_counts_completed\":\""
           << counters.full_point_counts_completed
           << "\",\"candidates_reaching_smoothness\":\""
           << counters.candidates_reaching_smoothness
           << "\",\"certificates_found\":\"" << counters.certificates_found
           << "\"}}";
    return output.str();
}

void append_search_progress_jsonl(const SearchState& state,
                                  const std::filesystem::path& progress_path) {
    if (progress_path.empty()) {
        throw SearchCheckpointError("progress path is empty");
    }
    const std::string record = search_progress_json(state) + "\n";
    const int descriptor =
        ::open(progress_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (descriptor < 0) {
        throw SearchCheckpointError(errno_text("cannot open progress log", progress_path));
    }
    try {
        write_all(descriptor, record, progress_path);
    } catch (...) {
        ::close(descriptor);
        throw;
    }
    if (::close(descriptor) != 0) {
        throw SearchCheckpointError(errno_text("cannot close progress log", progress_path));
    }
}

}  // namespace oneshotsea
