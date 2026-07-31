#include "oneshotsea/search_pipeline.hpp"

#include "oneshotsea/sea.hpp"
#include "oneshotsea/weber_curve_generator.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

namespace oneshotsea {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::chrono::seconds kSubprocessTimeout{30};

std::uint64_t elapsed_us(Clock::time_point start) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start)
            .count();
    if (elapsed < 0) {
        throw std::logic_error("steady clock moved backwards");
    }
    return static_cast<std::uint64_t>(elapsed);
}

std::uint64_t peak_rss_bytes() {
    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) {
        throw std::runtime_error("cannot read process peak RSS");
    }
    const std::uint64_t raw = static_cast<std::uint64_t>(usage.ru_maxrss);
#if defined(__APPLE__)
    return raw;
#else
    if (raw > std::numeric_limits<std::uint64_t>::max() / 1024U) {
        throw std::overflow_error("peak RSS does not fit bytes");
    }
    return raw * 1024U;
#endif
}

class Sha256 {
public:
    void update(std::string_view bytes) {
        update(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    }

    void update(const unsigned char* bytes, std::size_t size) {
        if (finalized_) {
            throw std::logic_error("cannot update a finalized SHA-256");
        }
        constexpr std::uint64_t maximum_bytes =
            std::numeric_limits<std::uint64_t>::max() / 8U;
        if (byte_count_ > maximum_bytes ||
            size > maximum_bytes - byte_count_) {
            throw std::overflow_error("SHA-256 input length overflow");
        }
        byte_count_ += static_cast<std::uint64_t>(size);
        while (size != 0U) {
            const std::size_t take = std::min(size, block_.size() - used_);
            std::copy_n(bytes, take, block_.begin() +
                                         static_cast<std::ptrdiff_t>(used_));
            bytes += take;
            size -= take;
            used_ += take;
            if (used_ == block_.size()) {
                transform(block_.data());
                used_ = 0;
            }
        }
    }

    std::string hex_digest() {
        if (!finalized_) {
            const std::uint64_t bit_count = byte_count_ * 8U;
            block_[used_++] = 0x80U;
            if (used_ > 56U) {
                std::fill(block_.begin() + static_cast<std::ptrdiff_t>(used_),
                          block_.end(), 0U);
                transform(block_.data());
                used_ = 0;
            }
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(used_),
                      block_.begin() + 56, 0U);
            for (unsigned index = 0; index < 8U; ++index) {
                block_[63U - index] = static_cast<unsigned char>(
                    bit_count >> (index * 8U));
            }
            transform(block_.data());
            used_ = 0;
            finalized_ = true;
        }
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (const std::uint32_t word : state_) {
            output << std::setw(8) << word;
        }
        return output.str();
    }

private:
    static std::uint32_t load_be(const unsigned char* value) {
        return (static_cast<std::uint32_t>(value[0]) << 24U) |
               (static_cast<std::uint32_t>(value[1]) << 16U) |
               (static_cast<std::uint32_t>(value[2]) << 8U) |
               static_cast<std::uint32_t>(value[3]);
    }

    void transform(const unsigned char* block) {
        static constexpr std::array<std::uint32_t, 64> constants = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
            0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
            0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
            0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
            0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
            0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
            0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
            0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
            0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
            0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            words[index] = load_be(block + 4U * index);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const std::uint32_t left = words[index - 15U];
            const std::uint32_t right = words[index - 2U];
            const std::uint32_t s0 = std::rotr(left, 7) ^ std::rotr(left, 18) ^
                                     (left >> 3U);
            const std::uint32_t s1 = std::rotr(right, 17) ^
                                     std::rotr(right, 19) ^ (right >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }
        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^
                                     std::rotr(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temporary1 =
                h + s1 + choice + constants[index] + words[index];
            const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^
                                     std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_ = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<unsigned char, 64> block_{};
    std::size_t used_ = 0;
    std::uint64_t byte_count_ = 0;
    bool finalized_ = false;
};

void validate_digest(const std::string& value, const char* label) {
    if (value.size() != 64U ||
        !std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9') ||
                   (character >= 'a' && character <= 'f');
        })) {
        throw std::invalid_argument(std::string(label) +
                                    " must be a lowercase SHA-256 digest");
    }
}

void validate_config(const SearchPipelineConfig& config,
                     const ExactSmoothEngine* engine = nullptr) {
    if (config.prime <= 7 || mpz_even_p(config.prime.get_mpz_t()) != 0 ||
        mpz_probab_prime_p(config.prime.get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "search target must be an odd probable prime greater than seven");
    }
    if (config.max_level < 5U || config.early_trace_cap == 0U ||
        config.assembly_attempts == 0U ||
        config.max_certificate_candidates == 0U ||
        config.max_candidate_search_nodes == 0U) {
        throw std::invalid_argument("invalid SEA/search resource limit");
    }
    if (!std::filesystem::is_directory(config.table_directory)) {
        throw std::invalid_argument("Weber table directory does not exist");
    }
    if (config.canonical_verifier.empty() ||
        !std::filesystem::is_regular_file(config.canonical_verifier)) {
        throw std::invalid_argument("canonical voneshot verifier does not exist");
    }
    const std::filesystem::path python(config.python_executable);
    if (config.python_executable.empty() || !python.is_absolute() ||
        !std::filesystem::is_regular_file(python) ||
        ::access(python.c_str(), X_OK) != 0) {
        throw std::invalid_argument(
            "Python executable must be an absolute executable regular file");
    }
    if (engine != nullptr && engine->prime() != config.prime) {
        throw std::invalid_argument("smooth engine belongs to another target");
    }
    if (!config.expected_schedule_sha256.empty()) {
        validate_digest(config.expected_schedule_sha256,
                        "expected schedule digest");
    }
    if (!config.expected_table_manifest_sha256.empty()) {
        validate_digest(config.expected_table_manifest_sha256,
                        "expected table-manifest digest");
    }
    if (!config.expected_verifier_sha256.empty()) {
        validate_digest(config.expected_verifier_sha256,
                        "expected verifier digest");
    }
    if (!config.expected_python_sha256.empty()) {
        validate_digest(config.expected_python_sha256,
                        "expected Python digest");
    }
}

std::size_t exact_level_count(const WeberSeaResult& result) {
    return static_cast<std::size_t>(std::count_if(
        result.levels.begin(), result.levels.end(),
        [](const WeberSeaLevelRecord& level) { return level.exact; }));
}

bool has_large_enough_smooth_part(
    const std::vector<CurveTwistSmoothParts>& parts,
    const mpz_class& lower_bound) {
    return std::any_of(parts.begin(), parts.end(), [&](const auto& value) {
        return value.curve_smooth_part.value > lower_bound ||
               value.twist_smooth_part.value > lower_bound;
    });
}

const char* montgomery_side_name(MontgomerySide side) {
    switch (side) {
        case MontgomerySide::curve:
            return "curve";
        case MontgomerySide::twist:
            return "twist";
        case MontgomerySide::either:
            return "either";
    }
    return "unknown";
}

std::string json_escape(std::string_view input) {
    std::ostringstream output;
    for (const char character : input) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (byte < 0x20U) {
                    output << "\\u00" << std::hex << std::setw(2)
                           << std::setfill('0') << static_cast<unsigned>(byte)
                           << std::dec;
                } else {
                    output << static_cast<char>(byte);
                }
        }
    }
    return output.str();
}

void append_line(const std::filesystem::path& path, const std::string& line) {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        throw std::runtime_error("cannot open NDJSON progress path: " +
                                 path.string());
    }
    output << line << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error("cannot append NDJSON progress path: " +
                                 path.string());
    }
}

void save_text_atomic(const std::filesystem::path& path,
                      const std::string& encoded, const char* label) {
    if (path.empty()) {
        throw std::invalid_argument(std::string(label) + " output path is empty");
    }
    const std::filesystem::path parent =
        path.parent_path().empty() ? std::filesystem::path(".")
                                   : path.parent_path();
    std::filesystem::create_directories(parent);
    std::string temporary_path = path.string() + ".tmp.XXXXXX";
    int descriptor = ::mkstemp(temporary_path.data());
    if (descriptor < 0) {
        throw std::runtime_error(std::string("cannot create ") + label +
                                 " temporary file: " +
                                 std::string(std::strerror(errno)));
    }
    bool temporary_exists = true;
    try {
        std::size_t offset = 0;
        while (offset < encoded.size()) {
            const ssize_t count = ::write(
                descriptor, encoded.data() + offset, encoded.size() - offset);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("cannot write ") + label +
                                         ": " +
                                         std::string(std::strerror(errno)));
            }
            if (count == 0) {
                throw std::runtime_error(std::string("short write while saving ") +
                                         label);
            }
            offset += static_cast<std::size_t>(count);
        }
        if (::fsync(descriptor) != 0) {
            throw std::runtime_error(std::string("cannot flush ") + label +
                                     ": " +
                                     std::string(std::strerror(errno)));
        }
        const int close_result = ::close(descriptor);
        const int close_error = errno;
        descriptor = -1;
        if (close_result != 0) {
            throw std::runtime_error(std::string("cannot close ") + label +
                                     ": " +
                                     std::string(std::strerror(close_error)));
        }
        if (::rename(temporary_path.c_str(), path.c_str()) != 0) {
            throw std::runtime_error(std::string("cannot publish ") + label +
                                     ": " +
                                     std::string(std::strerror(errno)));
        }
        temporary_exists = false;
        const int parent_descriptor = ::open(parent.c_str(), O_RDONLY);
        if (parent_descriptor < 0) {
            throw std::runtime_error(std::string("cannot open ") + label +
                                     " parent directory: " +
                                     std::string(std::strerror(errno)));
        }
        const int sync_result = ::fsync(parent_descriptor);
        const int sync_error = errno;
        ::close(parent_descriptor);
        if (sync_result != 0) {
            throw std::runtime_error(std::string("cannot flush ") + label +
                                     " parent directory: " +
                                     std::string(std::strerror(sync_error)));
        }
    } catch (...) {
        if (descriptor >= 0) {
            ::close(descriptor);
        }
        if (temporary_exists) {
            ::unlink(temporary_path.c_str());
        }
        throw;
    }
}

mpz_class parse_decimal_mpz(const std::string& text) {
    mpz_class result;
    if (text.empty() || mpz_set_str(result.get_mpz_t(), text.c_str(), 10) != 0) {
        throw std::runtime_error("malformed saved certificate integer");
    }
    return result;
}

MontgomeryCertificate load_certificate(
    const std::filesystem::path& certificate_path) {
    std::ifstream input(certificate_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open saved certificate");
    }
    std::vector<std::string> tokens;
    for (std::string token; input >> token;) {
        tokens.push_back(std::move(token));
    }
    if (!input.eof() || tokens.size() < 4U) {
        throw std::runtime_error("malformed saved certificate line");
    }
    MontgomeryCertificate certificate{
        parse_decimal_mpz(tokens[0]), parse_decimal_mpz(tokens[1]),
        parse_decimal_mpz(tokens[2]), parse_decimal_mpz(tokens[3]), {}};
    for (std::size_t index = 4U; index < tokens.size(); ++index) {
        std::uint64_t divisor = 0;
        const auto converted = std::from_chars(
            tokens[index].data(), tokens[index].data() + tokens[index].size(),
            divisor, 10);
        if (tokens[index].empty() || converted.ec != std::errc{} ||
            converted.ptr != tokens[index].data() + tokens[index].size()) {
            throw std::runtime_error("malformed saved certificate divisor");
        }
        certificate.large_prime_divisors.push_back(divisor);
    }
    return certificate;
}

std::filesystem::path certificate_metadata_path(
    const std::filesystem::path& certificate_path) {
    return certificate_path.string() + ".meta.json";
}

std::string certificate_metadata(const SearchIdentity& identity,
                                 std::uint64_t global_index,
                                 const MontgomeryCertificate& certificate,
                                 const std::string& certificate_sha256) {
    std::ostringstream output;
    output << "{\"schema\":\"oneshotsea.certificate-binding.v1\""
           << ",\"prime\":\"" << identity.prime
           << "\",\"seed\":\"" << identity.seed
           << "\",\"worker_id\":\"" << identity.worker_id
           << "\",\"worker_count\":\"" << identity.worker_count
           << "\",\"range_start\":\"" << identity.range.first
           << "\",\"range_end\":\"" << identity.range.end
           << "\",\"schedule_sha256\":\"" << identity.schedule_sha256
           << "\",\"table_manifest_sha256\":\""
           << identity.table_manifest_sha256 << "\",\"build_id\":\""
           << identity.build_id << "\",\"global_index\":\"" << global_index
           << "\",\"certificate_sha256\":\"" << certificate_sha256
           << "\",\"certificate_line\":\""
           << json_escape(certificate.line()) << "\"}\n";
    return output.str();
}

std::string read_small_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open certificate metadata");
    }
    std::ostringstream output;
    std::array<char, 4096> buffer{};
    std::size_t total = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            total += static_cast<std::size_t>(count);
            if (total > 1024U * 1024U) {
                throw std::runtime_error("certificate metadata exceeds byte limit");
            }
            output.write(buffer.data(), count);
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("cannot read certificate metadata");
    }
    return output.str();
}

void require_distinct_pipeline_paths(const SearchPipelineRunOptions& options) {
    std::vector<std::filesystem::path> paths;
    for (const auto& path : {options.checkpoint_path, options.progress_path,
                             options.certificate_path,
                             certificate_metadata_path(options.certificate_path)}) {
        if (path.empty()) {
            continue;
        }
        for (const std::filesystem::path& existing : paths) {
            if (paths_alias(path, existing)) {
                throw std::invalid_argument(
                    "checkpoint, progress, certificate, and metadata paths must be distinct");
            }
        }
        paths.push_back(path);
    }
}

int wait_for_child(pid_t process, std::chrono::seconds timeout,
                   const char* label) {
    const Clock::time_point deadline = Clock::now() + timeout;
    for (;;) {
        int status = 0;
        const pid_t waited = ::waitpid(process, &status, WNOHANG);
        if (waited == process) {
            return status;
        }
        if (waited < 0 && errno != EINTR) {
            throw std::runtime_error(std::string("cannot wait for ") + label +
                                     ": " + std::strerror(errno));
        }
        if (Clock::now() >= deadline) {
            if (::kill(process, SIGKILL) != 0 && errno != ESRCH) {
                throw std::runtime_error(std::string("cannot terminate timed-out ") +
                                         label + ": " +
                                         std::strerror(errno));
            }
            while (::waitpid(process, &status, 0) < 0 && errno == EINTR) {
            }
            throw std::runtime_error(std::string(label) + " timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

struct PreparedOrder {
    bool twist = false;
    mpz_class order;
    mpz_class smooth_part;
};

bool verify_with_pinned_runtime(const SearchPipelineConfig& config,
                                const MontgomeryCertificate& certificate) {
    if (!config.expected_python_sha256.empty() &&
        sha256_file(config.python_executable) !=
            config.expected_python_sha256) {
        throw std::runtime_error(
            "Python executable changed after search identity creation");
    }
    if (!config.expected_verifier_sha256.empty() &&
        sha256_file(config.canonical_verifier) !=
            config.expected_verifier_sha256) {
        throw std::runtime_error(
            "canonical verifier changed after search identity creation");
    }
    return verify_with_canonical_voneshot(
        certificate, config.canonical_verifier, config.python_executable);
}

}  // namespace

std::string resolve_executable_path(const std::string& executable) {
    if (executable.empty() || executable.find('\0') != std::string::npos) {
        throw std::invalid_argument("executable name is empty or malformed");
    }
    std::vector<std::filesystem::path> candidates;
    if (executable.find('/') != std::string::npos) {
        candidates.emplace_back(executable);
    } else {
        const char* raw_path = std::getenv("PATH");
        if (raw_path == nullptr) {
            throw std::invalid_argument("PATH is unavailable for executable lookup");
        }
        const std::string path(raw_path);
        std::size_t begin = 0;
        for (;;) {
            const std::size_t end = path.find(':', begin);
            const std::string component = path.substr(begin, end - begin);
            candidates.push_back(
                (component.empty() ? std::filesystem::path(".")
                                   : std::filesystem::path(component)) /
                executable);
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1U;
        }
    }
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate, error) || error ||
            ::access(candidate.c_str(), X_OK) != 0) {
            continue;
        }
        const std::filesystem::path resolved =
            std::filesystem::canonical(candidate, error);
        if (!error && resolved.is_absolute() &&
            std::filesystem::is_regular_file(resolved) &&
            ::access(resolved.c_str(), X_OK) == 0) {
            return resolved.string();
        }
    }
    throw std::invalid_argument("cannot resolve executable: " + executable);
}

bool authenticate_python3_interpreter(
    const std::string& absolute_python_executable) {
    const std::string resolved =
        resolve_executable_path(absolute_python_executable);
    if (resolved != absolute_python_executable) {
        throw std::invalid_argument(
            "Python executable path is not canonical and absolute");
    }
    int output_pipe[2] = {-1, -1};
    if (::pipe(output_pipe) != 0) {
        throw std::runtime_error("cannot create Python authentication pipe: " +
                                 std::string(std::strerror(errno)));
    }
    const int null_descriptor = ::open("/dev/null", O_WRONLY);
    if (null_descriptor < 0) {
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        throw std::runtime_error("cannot open /dev/null for Python probe");
    }
    posix_spawn_file_actions_t actions;
    const int initialized = posix_spawn_file_actions_init(&actions);
    int action_error = initialized;
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_addclose(&actions,
                                                         output_pipe[0]);
    }
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_adddup2(
            &actions, output_pipe[1], STDOUT_FILENO);
    }
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_adddup2(
            &actions, null_descriptor, STDERR_FILENO);
    }
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_addclose(&actions,
                                                         output_pipe[1]);
    }
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_addclose(&actions,
                                                         null_descriptor);
    }
    if (action_error != 0) {
        if (initialized == 0) {
            posix_spawn_file_actions_destroy(&actions);
        }
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        ::close(null_descriptor);
        throw std::runtime_error("cannot configure Python authentication probe");
    }

    std::vector<std::string> arguments = {
        resolved,
        "-I",
        "-c",
        "import sys; print('oneshotsea-python3') if sys.version_info.major == 3 else sys.exit(17)",
    };
    std::vector<char*> argv;
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    pid_t process = 0;
    const int spawned = posix_spawn(&process, resolved.c_str(), &actions,
                                    nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(output_pipe[1]);
    ::close(null_descriptor);
    if (spawned != 0) {
        ::close(output_pipe[0]);
        throw std::runtime_error("cannot launch Python authentication probe: " +
                                 std::string(std::strerror(spawned)));
    }
    int status = 0;
    try {
        status = wait_for_child(process, kSubprocessTimeout,
                                "Python authentication probe");
    } catch (...) {
        ::close(output_pipe[0]);
        throw;
    }
    std::string output;
    std::array<char, 128> buffer{};
    for (;;) {
        const ssize_t count = ::read(output_pipe[0], buffer.data(), buffer.size());
        if (count > 0) {
            if (output.size() <= 1024U) {
                output.append(buffer.data(), static_cast<std::size_t>(count));
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            ::close(output_pipe[0]);
            throw std::runtime_error("cannot read Python authentication marker");
        }
        break;
    }
    ::close(output_pipe[0]);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
           output == "oneshotsea-python3\n";
}

const char* search_curve_status_name(SearchCurveStatus status) {
    switch (status) {
        case SearchCurveStatus::no_rational_weber_lift:
            return "no_rational_weber_lift";
        case SearchCurveStatus::sea_level_limit:
            return "sea_level_limit";
        case SearchCurveStatus::sound_smoothness_reject:
            return "sound_smoothness_reject";
        case SearchCurveStatus::no_certificate_candidate:
            return "no_certificate_candidate";
        case SearchCurveStatus::certificate_assembly_failed:
            return "certificate_assembly_failed";
        case SearchCurveStatus::canonical_verifier_rejected:
            return "canonical_verifier_rejected";
        case SearchCurveStatus::verified_certificate:
            return "verified_certificate";
    }
    return "unknown";
}

bool verify_with_canonical_voneshot(
    const MontgomeryCertificate& certificate,
    const std::filesystem::path& verifier,
    const std::string& python_executable) {
    if (!std::filesystem::is_regular_file(verifier)) {
        throw std::invalid_argument("canonical verifier is not a regular file");
    }
    if (!authenticate_python3_interpreter(python_executable)) {
        throw std::invalid_argument(
            "configured executable did not authenticate as Python 3");
    }
    std::vector<std::string> arguments = {
        python_executable, "-I", verifier.string(), certificate.prime.get_str(),
        certificate.coefficient.get_str(), certificate.x.get_str(),
        certificate.order.get_str(),
    };
    for (const std::uint64_t divisor : certificate.large_prime_divisors) {
        arguments.push_back(std::to_string(divisor));
    }
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1U);
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        throw std::runtime_error("cannot initialize canonical verifier process");
    }
    int output_pipe[2] = {-1, -1};
    if (::pipe(output_pipe) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        throw std::runtime_error("cannot create canonical verifier pipe");
    }
    const int null_descriptor = ::open("/dev/null", O_WRONLY);
    if (null_descriptor < 0) {
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        posix_spawn_file_actions_destroy(&actions);
        throw std::runtime_error("cannot open /dev/null for canonical verifier");
    }
    int action_error = posix_spawn_file_actions_addclose(
        &actions, output_pipe[0]);
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_adddup2(
            &actions, output_pipe[1], STDOUT_FILENO);
    }
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_adddup2(
            &actions, null_descriptor, STDERR_FILENO);
    }
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_addclose(
            &actions, output_pipe[1]);
    }
    if (action_error == 0) {
        action_error = posix_spawn_file_actions_addclose(&actions, null_descriptor);
    }
    if (action_error != 0) {
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        ::close(null_descriptor);
        posix_spawn_file_actions_destroy(&actions);
        throw std::runtime_error("cannot configure canonical verifier process");
    }

    pid_t process = 0;
    const int spawned = posix_spawnp(&process, python_executable.c_str(),
                                     &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(output_pipe[1]);
    ::close(null_descriptor);
    if (spawned != 0) {
        ::close(output_pipe[0]);
        throw std::runtime_error("cannot launch canonical verifier: " +
                                 std::string(std::strerror(spawned)));
    }
    int status = 0;
    try {
        status = wait_for_child(process, kSubprocessTimeout,
                                "canonical verifier");
    } catch (...) {
        ::close(output_pipe[0]);
        throw;
    }
    std::string output;
    std::array<char, 128> buffer{};
    for (;;) {
        const ssize_t count = ::read(output_pipe[0], buffer.data(), buffer.size());
        if (count > 0) {
            if (output.size() + static_cast<std::size_t>(count) > 1024U) {
                ::close(output_pipe[0]);
                throw std::runtime_error(
                    "canonical verifier output exceeds byte limit");
            }
            output.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            ::close(output_pipe[0]);
            throw std::runtime_error("cannot read canonical verifier output");
        }
        break;
    }
    ::close(output_pipe[0]);
    if (!WIFEXITED(status)) {
        throw std::runtime_error("canonical verifier terminated abnormally");
    }
    if (WEXITSTATUS(status) == 0 && output == "True\n") {
        return true;
    }
    if (WEXITSTATUS(status) == 1 && output == "False\n") {
        return false;
    }
    throw std::runtime_error(
        "canonical verifier returned an unexpected status or output");
}

SearchCurveReport process_search_curve(
    const SearchPipelineConfig& config, const ExactSmoothEngine& smooth_engine,
    std::uint64_t global_index,
    const CanonicalCertificateVerifier& injected_verifier) {
    validate_config(config, &smooth_engine);
    const Clock::time_point total_start = Clock::now();
    SearchCurveReport report;
    report.global_index = global_index;

    Clock::time_point stage_start = Clock::now();
    const WeberCurvePair pair = deterministic_weber_curve_pair(
        config.prime, config.seed, global_index);
    report.rejected_generator_samples = pair.rejected_samples;
    report.timings.generation_us = elapsed_us(stage_start);

    auto run_sea = [&](std::size_t trace_cap) {
        stage_start = Clock::now();
        const std::size_t pass = report.sea_passes + 1U;
        const WeberSeaProgress progress = [&](const WeberSeaLevelRecord& level) {
            report.sea_level_timings.push_back({
                pass,
                level.ell,
                level.exact,
                level.timings.source_lifts_us,
                level.timings.modular_roots_us,
                level.timings.normalized_codomain_us,
                level.timings.bmss_us,
                level.timings.eigenvalue_us,
            });
        };
        WeberSeaResult result = run_weber_sea_reference(
            pair.curve, config.table_directory.string(), config.max_level,
            trace_cap, progress);
        report.timings.sea_us += elapsed_us(stage_start);
        ++report.sea_passes;
        report.sea_levels += result.levels.size();
        report.exact_sea_levels += exact_level_count(result);
        return result;
    };

    WeberSeaResult sea = run_sea(config.early_trace_cap);
    if (sea.compatible_source_lifts.empty() && sea.levels.empty()) {
        report.status = SearchCurveStatus::no_rational_weber_lift;
        report.outcome = {CurveTerminalStage::rejected_sea, false, false};
        report.timings.total_us = elapsed_us(total_start);
        return report;
    }
    if (!sea.traces.has_value()) {
        report.status = SearchCurveStatus::sea_level_limit;
        report.outcome = {CurveTerminalStage::rejected_sea, false, false};
        report.timings.total_us = elapsed_us(total_start);
        return report;
    }
    report.initial_trace_count = sea.traces->size();
    if (sea.traces->empty()) {
        throw std::logic_error("SEA enumerated an empty complete trace set");
    }

    stage_start = Clock::now();
    std::vector<CurveTwistSmoothParts> initial_parts =
        smooth_engine.extract_curve_twist(*sea.traces);
    report.timings.smoothness_us += elapsed_us(stage_start);
    const CertificateBounds bounds = canonical_certificate_bounds(config.prime);
    if (!has_large_enough_smooth_part(initial_parts,
                                      bounds.lower_order_bound)) {
        report.status = SearchCurveStatus::sound_smoothness_reject;
        report.outcome = {
            CurveTerminalStage::rejected_sound_early_abort,
            sea.traces->size() == 1U,
            true,
        };
        if (sea.traces->size() == 1U) {
            report.exact_trace = sea.traces->front();
        }
        report.timings.total_us = elapsed_us(total_start);
        return report;
    }

    std::vector<CurveTwistSmoothParts> exact_parts;
    if (sea.traces->size() == 1U) {
        report.exact_trace = sea.traces->front();
        exact_parts = std::move(initial_parts);
    } else {
        // The early enumeration was complete, so the rejection above was
        // sound.  A survivor must now meet the stricter unique-trace gate.
        sea = run_sea(1U);
        if (!sea.traces.has_value()) {
            report.status = SearchCurveStatus::sea_level_limit;
            report.outcome = {CurveTerminalStage::rejected_sea, false, true};
            report.timings.total_us = elapsed_us(total_start);
            return report;
        }
        if (sea.traces->size() != 1U) {
            throw std::logic_error(
                "SEA trace-cap one returned a non-singleton trace set");
        }
        report.exact_trace = sea.traces->front();
        const auto found = std::find_if(
            initial_parts.begin(), initial_parts.end(), [&](const auto& value) {
                return value.trace == *report.exact_trace;
            });
        if (found != initial_parts.end()) {
            exact_parts.push_back(*found);
        } else {
            const std::array<mpz_class, 1> trace = {*report.exact_trace};
            stage_start = Clock::now();
            exact_parts = smooth_engine.extract_curve_twist(trace);
            report.timings.smoothness_us += elapsed_us(stage_start);
        }
    }
    if (exact_parts.size() != 1U) {
        throw std::logic_error("unique SEA trace did not produce one order pair");
    }

    const std::array<PreparedOrder, 2> orders = {{
        {false, exact_parts.front().curve_order,
         exact_parts.front().curve_smooth_part.value},
        {true, exact_parts.front().twist_order,
         exact_parts.front().twist_smooth_part.value},
    }};
    const CanonicalCertificateVerifier canonical = injected_verifier
        ? injected_verifier
        : [&](const MontgomeryCertificate& certificate) {
              return verify_with_pinned_runtime(config, certificate);
          };

    bool had_candidate = false;
    for (const PreparedOrder& order : orders) {
        if (report.candidate_attempts ==
                config.max_certificate_candidates ||
            report.candidate_search_nodes ==
                config.max_candidate_search_nodes) {
            throw std::runtime_error(
                "certificate candidate enumeration limit reached before both order classes were exhausted");
        }
        const CandidateEnumerationLimits remaining_limits{
            config.max_certificate_candidates - report.candidate_attempts,
            config.max_candidate_search_nodes - report.candidate_search_nodes,
        };
        const Clock::time_point enumeration_start = Clock::now();
        std::uint64_t visitor_us = 0;
        const CandidateEnumerationResult enumeration =
            enumerate_certificate_candidates(
                config.prime, order.order, order.smooth_part,
                [&](const CertificateCandidate& candidate,
                    CandidateOrigin origin) {
            const Clock::time_point visitor_start = Clock::now();
            ++report.candidate_attempts;
            had_candidate = true;
            const bool odd_only =
                origin == CandidateOrigin::preferred_odd_only;
            for (const MontgomerySide side : {
                     MontgomerySide::curve, MontgomerySide::twist}) {
                ++report.assembly_attempts;
                std::uint64_t candidate_domain = 0;
                if (origin == CandidateOrigin::preferred_odd_only) {
                    candidate_domain = UINT64_C(0x4f4444);
                } else if (origin == CandidateOrigin::exhaustive) {
                    candidate_domain = splitmix64(
                        UINT64_C(0x45584841555354) ^
                        static_cast<std::uint64_t>(
                            candidate.point_order.get_ui()));
                }
                AssemblyOptions assembly;
                assembly.seed = splitmix64(
                    config.certificate_seed ^ global_index ^
                    (order.twist ? UINT64_C(0x5457495354) :
                                   UINT64_C(0x4355525645)) ^
                    candidate_domain ^
                    static_cast<std::uint64_t>(side));
                assembly.attempts_per_coefficient = config.assembly_attempts;
                assembly.side = side;
                const Clock::time_point assembly_start = Clock::now();
                const auto certificate =
                    assemble_montgomery_certificate_from_j(
                        candidate, pair.j_invariant, assembly);
                report.timings.assembly_us += elapsed_us(assembly_start);
                if (!certificate.has_value() ||
                    !validate_montgomery_certificate(*certificate)) {
                    continue;
                }
                const Clock::time_point verifier_start = Clock::now();
                const bool accepted = canonical(*certificate);
                report.timings.verifier_us += elapsed_us(verifier_start);
                if (!accepted) {
                    ++report.canonical_rejections;
                    continue;
                }
                report.status = SearchCurveStatus::verified_certificate;
                report.outcome = {
                    CurveTerminalStage::verified_certificate_found, true, true};
                report.certificate_uses_twist_order = order.twist;
                report.certificate_uses_odd_only = odd_only;
                report.certificate_montgomery_side = side;
                report.certificate = certificate;
                report.timings.total_us = elapsed_us(total_start);
                visitor_us += elapsed_us(visitor_start);
                return false;
            }
            visitor_us += elapsed_us(visitor_start);
            return true;
        }, remaining_limits);
        report.candidate_search_nodes += enumeration.search_nodes_visited;
        const std::uint64_t enumeration_total_us =
            elapsed_us(enumeration_start);
        if (enumeration_total_us >= visitor_us) {
            report.timings.candidate_us += enumeration_total_us - visitor_us;
        }
        if (report.certificate.has_value()) {
            return report;
        }
        if (enumeration.limit !=
            CandidateEnumerationResult::Limit::none) {
            throw std::runtime_error(
                enumeration.limit ==
                        CandidateEnumerationResult::Limit::candidates
                    ? "certificate candidate limit reached; rerun with a larger --max-certificate-candidates"
                    : "candidate DFS node limit reached; rerun with a larger --max-candidate-search-nodes");
        }
        if (enumeration.failure != CandidateFailure::none &&
            enumeration.failure != CandidateFailure::no_admissible_divisor) {
            throw std::logic_error(
                "validated exact smooth part failed candidate enumeration");
        }
    }

    if (!had_candidate) {
        report.status = SearchCurveStatus::no_certificate_candidate;
        report.outcome = {
            CurveTerminalStage::completed_without_certificate, true, true};
    } else if (report.canonical_rejections != 0U) {
        report.status = SearchCurveStatus::canonical_verifier_rejected;
        report.outcome = {
            CurveTerminalStage::rejected_certificate_assembly, true, true};
    } else {
        report.status = SearchCurveStatus::certificate_assembly_failed;
        report.outcome = {
            CurveTerminalStage::rejected_certificate_assembly, true, true};
    }
    report.timings.total_us = elapsed_us(total_start);
    return report;
}

SearchPipelineRunResult run_search_pipeline(
    const SearchPipelineConfig& config, const ExactSmoothEngine& smooth_engine,
    SearchState& state, const SearchPipelineRunOptions& options,
    const SearchReportCallback& report_callback,
    const CanonicalCertificateVerifier& verifier) {
    validate_config(config, &smooth_engine);
    if (state.identity().prime != config.prime ||
        state.identity().seed != config.seed) {
        throw std::invalid_argument("search state does not match pipeline target/seed");
    }
    if (!config.expected_schedule_sha256.empty() &&
        state.identity().schedule_sha256 != config.expected_schedule_sha256) {
        throw std::invalid_argument(
            "search state does not match the configured schedule identity");
    }
    if (!config.expected_table_manifest_sha256.empty() &&
        state.identity().table_manifest_sha256 !=
            config.expected_table_manifest_sha256) {
        throw std::invalid_argument(
            "search state does not match the configured table identity");
    }
    if (!config.expected_table_manifest_sha256.empty() &&
        weber_table_manifest_sha256(config.table_directory, config.max_level) !=
            config.expected_table_manifest_sha256) {
        throw std::invalid_argument(
            "Weber table contents changed after search identity creation");
    }
    if (options.checkpoint_every == 0U) {
        throw std::invalid_argument("checkpoint interval must be positive");
    }
    require_distinct_pipeline_paths(options);
    SearchPipelineRunResult result;
    const CanonicalCertificateVerifier canonical = verifier
        ? verifier
        : [&](const MontgomeryCertificate& certificate) {
              return verify_with_pinned_runtime(config, certificate);
          };
    if (options.max_curves != 0U &&
        (options.certificate_path.empty() || options.checkpoint_path.empty())) {
        throw std::invalid_argument(
            "durable checkpoint and certificate paths are required for search");
    }
    const std::filesystem::path metadata_path =
        certificate_metadata_path(options.certificate_path);
    const bool certificate_exists = !options.certificate_path.empty() &&
        std::filesystem::is_regular_file(options.certificate_path);
    const bool metadata_exists = !options.certificate_path.empty() &&
        std::filesystem::is_regular_file(metadata_path);
    if (metadata_exists && !certificate_exists) {
        throw std::runtime_error(
            "certificate metadata exists without its canonical artifact");
    }
    if (certificate_exists && metadata_exists) {
        MontgomeryCertificate saved = load_certificate(options.certificate_path);
        const std::uint64_t bound_index =
            state.counters().certificates_found == 0U
                ? state.next_index()
                : (state.next_index() - 1U);
        const std::string certificate_sha =
            sha256_file(options.certificate_path);
        const std::string expected_metadata = certificate_metadata(
            state.identity(), bound_index, saved, certificate_sha);
        if (read_small_file(metadata_path) != expected_metadata) {
            throw std::runtime_error(
                "certificate metadata does not match search identity/index/artifact");
        }
        if (saved.prime != config.prime ||
            !validate_montgomery_certificate(saved) || !canonical(saved)) {
            throw std::runtime_error(
                "existing certificate output is not a valid canonical proof");
        }
        SearchCurveReport recovered;
        recovered.global_index =
            bound_index;
        recovered.status = SearchCurveStatus::verified_certificate;
        recovered.outcome = {
            CurveTerminalStage::verified_certificate_found, true, true};
        recovered.certificate = std::move(saved);
        if (state.counters().certificates_found == 0U && !state.complete()) {
            // This is the crash window after durable certificate publication
            // but before checkpoint publication.  Advance the replayed cursor.
            state.record_completed(state.next_index(), recovered.outcome);
            if (!options.checkpoint_path.empty()) {
                save_search_checkpoint(state, options.checkpoint_path);
            }
        }
        result.verified = std::move(recovered);
        result.exhausted_assigned_range = state.complete();
        return result;
    }
    if (state.counters().certificates_found != 0U) {
        throw std::runtime_error(
            "checkpoint records a certificate but its durable artifact is missing");
    }
    while (!state.complete() && result.curves_processed < options.max_curves) {
        const std::uint64_t index = state.next_index();
        if (weber_table_manifest_sha256(config.table_directory,
                                        config.max_level) !=
            state.identity().table_manifest_sha256) {
            throw std::runtime_error(
                "Weber table contents changed before curve processing");
        }
        SearchCurveReport report = process_search_curve(
            config, smooth_engine, index, verifier);
        if (weber_table_manifest_sha256(config.table_directory,
                                        config.max_level) !=
            state.identity().table_manifest_sha256) {
            throw std::runtime_error(
                "Weber table contents changed during curve processing");
        }
        if (report.status == SearchCurveStatus::sea_level_limit ||
            report.status == SearchCurveStatus::no_rational_weber_lift) {
            // These are implementation/resource outcomes, not mathematical
            // rejections.  Persist and report the unchanged cursor, then stop
            // this chunk so a retry cannot silently skip the curve.
            if (!options.checkpoint_path.empty()) {
                save_search_checkpoint(state, options.checkpoint_path);
            }
            if (!options.progress_path.empty()) {
                append_line(options.progress_path,
                            search_curve_report_json(report, state));
            }
            if (report_callback) {
                report_callback(report, state);
            }
            break;
        }
        if (report.certificate.has_value()) {
            // Anchor the exact winning cursor before exposing its artifacts.
            // This makes crash recovery unambiguous even when the ordinary
            // checkpoint interval is greater than one curve.
            save_search_checkpoint(state, options.checkpoint_path);
            save_text_atomic(options.certificate_path,
                             report.certificate->line() + '\n', "certificate");
            const std::string certificate_sha =
                sha256_file(options.certificate_path);
            save_text_atomic(
                metadata_path,
                certificate_metadata(state.identity(), index,
                                     *report.certificate, certificate_sha),
                "certificate metadata");
        }
        state.record_completed(index, report.outcome);
        ++result.curves_processed;

        const bool should_checkpoint =
            result.curves_processed % options.checkpoint_every == 0U ||
            state.complete() || report.certificate.has_value();
        if (should_checkpoint && !options.checkpoint_path.empty()) {
            save_search_checkpoint(state, options.checkpoint_path);
        }
        if (!options.progress_path.empty()) {
            append_line(options.progress_path,
                        search_curve_report_json(report, state));
        }
        if (report_callback) {
            report_callback(report, state);
        }
        if (report.certificate.has_value()) {
            result.verified = std::move(report);
            break;
        }
    }
    result.exhausted_assigned_range = state.complete();
    return result;
}

std::string search_curve_report_json(const SearchCurveReport& report,
                                     const SearchState& state) {
    std::ostringstream output;
    output << "{\"schema\":\"oneshotsea.search-curve.v1\",\"index\":\""
           << report.global_index << "\",\"status\":\""
           << search_curve_status_name(report.status)
           << "\",\"peak_rss_bytes\":\"" << peak_rss_bytes()
           << "\",\"heuristic\":false,\"outcome_class\":\""
           << (report.status == SearchCurveStatus::sound_smoothness_reject
                   ? "sound_rejection"
                   : (report.status == SearchCurveStatus::sea_level_limit
                          ? "implementation_level_limit"
                          : (report.status ==
                                     SearchCurveStatus::no_rational_weber_lift
                                 ? "implementation_no_lift"
                                 : "terminal")))
           << "\",\"sound_early_abort\":"
           << (report.status == SearchCurveStatus::sound_smoothness_reject
                   ? "true" : "false")
           << ",\"full_point_count\":"
           << (report.outcome.full_point_count_completed ? "true" : "false")
           << ",\"reached_smoothness\":"
           << (report.outcome.reached_smoothness_testing ? "true" : "false")
           << ",\"generator_rejections\":\""
           << report.rejected_generator_samples << "\",\"sea_passes\":\""
           << report.sea_passes << "\",\"sea_levels\":\""
           << report.sea_levels << "\",\"exact_sea_levels\":\""
           << report.exact_sea_levels << "\",\"initial_trace_count\":\""
           << report.initial_trace_count << '"';
    if (report.exact_trace.has_value()) {
        output << ",\"trace\":\"" << *report.exact_trace << '"';
    }
    output << ",\"candidate_attempts\":\"" << report.candidate_attempts
           << "\",\"candidate_search_nodes\":\""
           << report.candidate_search_nodes
           << "\",\"assembly_calls\":\"" << report.assembly_attempts
           << "\",\"canonical_rejections\":\""
           << report.canonical_rejections << "\",\"timings_us\":{"
           << "\"generation\":\"" << report.timings.generation_us
           << "\",\"sea\":\"" << report.timings.sea_us
           << "\",\"smoothness\":\"" << report.timings.smoothness_us
           << "\",\"candidate\":\"" << report.timings.candidate_us
           << "\",\"assembly\":\"" << report.timings.assembly_us
           << "\",\"verifier\":\"" << report.timings.verifier_us
           << "\",\"total\":\"" << report.timings.total_us
           << "\"},\"sea_level_timings\":[";
    for (std::size_t index = 0; index < report.sea_level_timings.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        const SearchSeaLevelTiming& level = report.sea_level_timings[index];
        output << "{\"pass\":\"" << level.pass << "\",\"ell\":\""
               << level.ell << "\",\"exact\":"
               << (level.exact ? "true" : "false")
               << ",\"source_lifts_us\":\"" << level.source_lifts_us
               << "\",\"modular_roots_us\":\"" << level.modular_roots_us
               << "\",\"normalized_codomain_us\":\""
               << level.normalized_codomain_us << "\",\"bmss_us\":\""
               << level.bmss_us << "\",\"eigenvalue_us\":\""
               << level.eigenvalue_us << "\"}";
    }
    output << ']';
    if (report.certificate.has_value()) {
        output << ",\"certificate\":{\"order_source\":\""
               << (report.certificate_uses_twist_order ? "twist" : "curve")
               << "\",\"odd_only\":"
               << (report.certificate_uses_odd_only ? "true" : "false")
               << ",\"montgomery_side\":\""
               << montgomery_side_name(report.certificate_montgomery_side)
               << "\",\"line\":\""
               << json_escape(report.certificate->line()) << "\"}";
    }
    output << ",\"state\":" << search_progress_json(state) << '}';
    return output.str();
}

std::string weber_table_manifest_sha256(
    const std::filesystem::path& table_directory, std::uint64_t max_level) {
    if (!std::filesystem::is_directory(table_directory)) {
        throw std::invalid_argument("Weber table directory does not exist");
    }
    const std::regex pattern(R"(^phi_([0-9]+)\.txt$)");
    std::vector<std::pair<std::uint64_t, std::filesystem::path>> tables;
    for (const auto& entry : std::filesystem::directory_iterator(table_directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::smatch match;
        const std::string filename = entry.path().filename().string();
        if (!std::regex_match(filename, match, pattern)) {
            continue;
        }
        std::uint64_t level = 0;
        try {
            level = std::stoull(match[1].str());
        } catch (const std::exception&) {
            continue;
        }
        if (level <= max_level) {
            tables.emplace_back(level, entry.path());
        }
    }
    std::sort(tables.begin(), tables.end(), [](const auto& left,
                                               const auto& right) {
        if (left.first != right.first) {
            return left.first < right.first;
        }
        return left.second.filename().string() <
               right.second.filename().string();
    });
    if (tables.empty()) {
        throw std::invalid_argument("no Weber tables exist through max level");
    }
    Sha256 manifest;
    manifest.update("oneshotsea.weber-table-manifest.v1\n");
    for (const auto& [level, path] : tables) {
        const std::string record = std::to_string(level) + " " +
            path.filename().string() + " " + sha256_file(path) + "\n";
        manifest.update(record);
    }
    return manifest.hex_digest();
}

std::string search_schedule_sha256(
    const SearchPipelineConfig& config,
    const std::string& smooth_cache_sha256,
    const std::string& canonical_verifier_sha256) {
    validate_digest(smooth_cache_sha256, "smooth-cache digest");
    validate_digest(canonical_verifier_sha256, "canonical-verifier digest");
    Sha256 schedule;
    std::ostringstream canonical;
    canonical << "oneshotsea.search-schedule.v1\n"
              << "curve_generator=weber-f-v1\n"
              << "sea=weber-reference-two-pass-v1\n"
              << "heuristic_rejection=disabled\n"
              << "prime=" << config.prime << '\n'
              << "max_level=" << config.max_level << '\n'
              << "early_trace_cap=" << config.early_trace_cap << '\n'
              << "assembly_attempts=" << config.assembly_attempts << '\n'
              << "certificate_seed=" << config.certificate_seed << '\n'
              << "python_executable_path=" << config.python_executable << '\n'
              << "python_executable_sha256="
              << sha256_file(config.python_executable) << '\n'
              << "smooth_cache_sha256=" << smooth_cache_sha256 << '\n'
              << "canonical_verifier_sha256="
              << canonical_verifier_sha256 << '\n';
    schedule.update(canonical.str());
    return schedule.hex_digest();
}

SearchIdentity make_search_identity(
    const SearchPipelineConfig& config, SearchRange global_range,
    std::uint64_t worker_id, std::uint64_t worker_count,
    const std::string& smooth_cache_sha256,
    const std::string& canonical_verifier_sha256,
    const std::string& build_id) {
    validate_config(config);
    if (!authenticate_python3_interpreter(config.python_executable)) {
        throw std::invalid_argument(
            "configured executable did not authenticate as Python 3");
    }
    SearchIdentity identity;
    identity.prime = config.prime;
    identity.seed = config.seed;
    identity.worker_id = worker_id;
    identity.worker_count = worker_count;
    identity.range = partition_search_range(global_range, worker_id, worker_count);
    identity.schedule_sha256 = search_schedule_sha256(
        config, smooth_cache_sha256, canonical_verifier_sha256);
    identity.table_manifest_sha256 = weber_table_manifest_sha256(
        config.table_directory, config.max_level);
    identity.build_id = build_id;
    // Construction validates every identity field, including build-id syntax.
    (void)SearchState(identity);
    return identity;
}

}  // namespace oneshotsea
