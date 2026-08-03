#include "oneshotsea/direct_context_cache.hpp"

#include "oneshotsea/class_polynomial.hpp"
#include "oneshotsea/integrity.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace oneshotsea {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagic = {
    'O', 'S', 'D', 'C', 'T', 'X', '0', '1'};
constexpr std::size_t kHeaderBytes =
    static_cast<std::size_t>(kClassicalDirectContextCacheHeaderBytes);
constexpr std::uint64_t kCrc64Polynomial = UINT64_C(0x42f0e1eba9ea3693);
constexpr std::size_t kIoChunkBytes = 1U << 30U;
constexpr std::size_t kPayloadWriteBufferBytes = 1U << 20U;
constexpr std::uint64_t kMaximumTargetBytes = 1U << 20U;

std::string errno_text(const std::string& operation,
                       const std::filesystem::path& path) {
    return operation + " " + path.string() + ": " + std::strerror(errno);
}

void validate_limits(ClassicalDirectContextCacheLimits limits) {
    if (limits.max_file_bytes < kClassicalDirectContextCacheHeaderBytes ||
        limits.max_levels == 0U ||
        limits.max_witnesses_per_level == 0U) {
        throw ClassicalDirectContextCacheError(
            "classical direct context cache limits must be positive");
    }
}

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right,
                          const char* label) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw ClassicalDirectContextCacheError(
            std::string("classical direct context ") + label +
            " overflows uint64");
    }
    return left + right;
}

std::uint64_t checked_mul(std::uint64_t left, std::uint64_t right,
                          const char* label) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw ClassicalDirectContextCacheError(
            std::string("classical direct context ") + label +
            " overflows uint64");
    }
    return left * right;
}

std::uint64_t square_entries(std::uint64_t level) {
    return checked_mul(checked_add(level, 2U, "matrix width"),
                       checked_add(level, 2U, "matrix width"),
                       "matrix entry count");
}

void put_u32(std::array<std::uint8_t, kHeaderBytes>& bytes,
             std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>((3U - index) * 8U));
    }
}

void put_u64(std::array<std::uint8_t, kHeaderBytes>& bytes,
             std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>((7U - index) * 8U));
    }
}

std::uint32_t get_u32(
    const std::array<std::uint8_t, kHeaderBytes>& bytes,
    std::size_t offset) {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value = static_cast<std::uint32_t>(
            (value << 8U) | bytes[offset + index]);
    }
    return value;
}

std::uint64_t get_u64(
    const std::array<std::uint8_t, kHeaderBytes>& bytes,
    std::size_t offset) {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value = (value << 8U) | bytes[offset + index];
    }
    return value;
}

std::array<std::uint8_t, 8U> encode_u64(std::uint64_t value) {
    std::array<std::uint8_t, 8U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>((7U - index) * 8U));
    }
    return bytes;
}

std::uint64_t decode_u64(const std::array<std::uint8_t, 8U>& bytes) {
    std::uint64_t value = 0U;
    for (const std::uint8_t byte : bytes) {
        value = (value << 8U) | byte;
    }
    return value;
}

std::uint64_t crc64_update(std::uint64_t crc, const std::uint8_t* bytes,
                           std::size_t length) {
    for (std::size_t index = 0U; index < length; ++index) {
        crc ^= static_cast<std::uint64_t>(bytes[index]) << 56U;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const bool high =
                (crc & UINT64_C(0x8000000000000000)) != 0U;
            crc <<= 1U;
            if (high) {
                crc ^= kCrc64Polynomial;
            }
        }
    }
    return crc;
}

std::uint64_t header_crc_seed(
    const std::array<std::uint8_t, kHeaderBytes>& header) {
    std::uint64_t crc = crc64_update(0U, header.data(), 24U);
    return crc64_update(crc, header.data() + 32U,
                        header.size() - 32U);
}

std::vector<std::uint8_t> export_positive(const mpz_class& value) {
    if (value <= 0) {
        throw ClassicalDirectContextCacheError(
            "classical direct cache target must be positive");
    }
    const std::size_t bits = mpz_sizeinbase(value.get_mpz_t(), 2);
    const std::size_t length = (bits + 7U) / 8U;
    if (length == 0U || length > kMaximumTargetBytes) {
        throw ClassicalDirectContextCacheError(
            "classical direct cache target encoding is excessive");
    }
    std::vector<std::uint8_t> bytes(length);
    std::size_t written = 0U;
    mpz_export(bytes.data(), &written, 1, 1, 1, 0, value.get_mpz_t());
    if (written != length || bytes.front() == 0U) {
        throw ClassicalDirectContextCacheError(
            "GMP produced a noncanonical direct-cache target");
    }
    return bytes;
}

std::uint64_t export_required_u64(const mpz_class& value,
                                  const char* label) {
    std::uint64_t encoded = 0U;
    if (!export_u64(value, encoded)) {
        throw ClassicalDirectContextCacheError(
            std::string("classical direct cache ") + label +
            " does not fit uint64");
    }
    return encoded;
}

class TemporaryFile {
public:
    TemporaryFile() = default;
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    TemporaryFile(TemporaryFile&& other) noexcept
        : descriptor(std::exchange(other.descriptor, -1)),
          path(std::move(other.path)) {
        other.path.clear();
    }
    TemporaryFile& operator=(TemporaryFile&&) = delete;
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
    static std::atomic<std::uint64_t> sequence{0U};
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::uint64_t attempt = 0U; attempt < 128U; ++attempt) {
        std::ostringstream suffix;
        suffix << ".tmp." << std::hex << tick << '.'
               << sequence.fetch_add(1U, std::memory_order_relaxed) << '.'
               << attempt;
        TemporaryFile temporary;
        temporary.path = target.string() + suffix.str();
        temporary.descriptor = ::open(
            temporary.path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (temporary.descriptor >= 0) {
            return temporary;
        }
        if (errno != EEXIST) {
            throw ClassicalDirectContextCacheError(
                errno_text("cannot create temporary direct cache",
                           temporary.path));
        }
    }
    throw ClassicalDirectContextCacheError(
        "cannot allocate a temporary direct-cache path");
}

void write_all(int descriptor, const std::uint8_t* bytes,
               std::size_t length, const std::filesystem::path& path) {
    while (length != 0U) {
        const std::size_t chunk = std::min(length, kIoChunkBytes);
        const ssize_t written = ::write(descriptor, bytes, chunk);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw ClassicalDirectContextCacheError(
                errno_text("cannot write direct cache", path));
        }
        if (written == 0) {
            throw ClassicalDirectContextCacheError(
                "short write to direct cache " + path.string());
        }
        const std::size_t count = static_cast<std::size_t>(written);
        bytes += count;
        length -= count;
    }
}

void pwrite_all(int descriptor, const std::uint8_t* bytes,
                std::size_t length, off_t offset,
                const std::filesystem::path& path) {
    while (length != 0U) {
        const ssize_t written = ::pwrite(descriptor, bytes, length, offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw ClassicalDirectContextCacheError(
                errno_text("cannot finalize direct cache", path));
        }
        if (written == 0) {
            throw ClassicalDirectContextCacheError(
                "short final write to direct cache " + path.string());
        }
        const std::size_t count = static_cast<std::size_t>(written);
        bytes += count;
        length -= count;
        offset += static_cast<off_t>(count);
    }
}

class PayloadWriter {
public:
    PayloadWriter(int descriptor, std::filesystem::path path,
                  std::uint64_t crc)
        : descriptor_(descriptor), path_(std::move(path)), crc_(crc),
          buffer_(kPayloadWriteBufferBytes) {}

    void bytes(const std::uint8_t* value, std::size_t length) {
        crc_ = crc64_update(crc_, value, length);
        count_ = checked_add(count_, static_cast<std::uint64_t>(length),
                             "written payload size");
        while (length != 0U) {
            if (buffered_ == buffer_.size()) {
                flush();
            }
            const std::size_t available = buffer_.size() - buffered_;
            const std::size_t chunk = std::min(length, available);
            std::memcpy(buffer_.data() + buffered_, value, chunk);
            buffered_ += chunk;
            value += chunk;
            length -= chunk;
        }
    }

    void u64(std::uint64_t value) {
        const std::array<std::uint8_t, 8U> encoded = encode_u64(value);
        bytes(encoded.data(), encoded.size());
    }

    std::uint64_t crc() const { return crc_; }
    std::uint64_t count() const { return count_; }

    void flush() {
        if (buffered_ == 0U) {
            return;
        }
        write_all(descriptor_, buffer_.data(), buffered_, path_);
        buffered_ = 0U;
    }

private:
    int descriptor_;
    std::filesystem::path path_;
    std::uint64_t crc_;
    std::vector<std::uint8_t> buffer_;
    std::size_t buffered_ = 0U;
    std::uint64_t count_ = 0U;
};

class ReadFile {
public:
    explicit ReadFile(const std::filesystem::path& path)
        : path_(path), buffer_(kPayloadWriteBufferBytes) {
        descriptor_ = ::open(path.c_str(), O_RDONLY);
        if (descriptor_ < 0) {
            throw ClassicalDirectContextCacheError(
                errno_text("cannot open direct cache", path_));
        }
        struct stat attributes {};
        if (::fstat(descriptor_, &attributes) != 0) {
            const int saved_errno = errno;
            ::close(descriptor_);
            descriptor_ = -1;
            errno = saved_errno;
            throw ClassicalDirectContextCacheError(
                errno_text("cannot stat direct cache", path_));
        }
        if (attributes.st_size < 0 || !S_ISREG(attributes.st_mode)) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw ClassicalDirectContextCacheError(
                "direct cache is not a regular file");
        }
        size_ = static_cast<std::uint64_t>(attributes.st_size);
    }

    ReadFile(const ReadFile&) = delete;
    ReadFile& operator=(const ReadFile&) = delete;
    ~ReadFile() { ::close(descriptor_); }

    std::uint64_t size() const { return size_; }

    void read(std::uint8_t* bytes, std::size_t length) {
        while (length != 0U) {
            if (buffer_offset_ == buffered_) {
                const ssize_t received = ::read(
                    descriptor_, buffer_.data(), buffer_.size());
                if (received < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    throw ClassicalDirectContextCacheError(
                        errno_text("cannot read direct cache", path_));
                }
                if (received == 0) {
                    throw ClassicalDirectContextCacheError(
                        "truncated direct cache " + path_.string());
                }
                buffer_offset_ = 0U;
                buffered_ = static_cast<std::size_t>(received);
            }
            const std::size_t chunk = std::min(
                length, buffered_ - buffer_offset_);
            std::memcpy(bytes, buffer_.data() + buffer_offset_, chunk);
            buffer_offset_ += chunk;
            bytes += chunk;
            length -= chunk;
        }
    }

private:
    int descriptor_ = -1;
    std::filesystem::path path_;
    std::vector<std::uint8_t> buffer_;
    std::size_t buffer_offset_ = 0U;
    std::size_t buffered_ = 0U;
    std::uint64_t size_ = 0U;
};

class PayloadReader {
public:
    PayloadReader(ReadFile& source, std::uint64_t crc,
                  std::uint64_t expected_bytes)
        : source_(source), crc_(crc), expected_bytes_(expected_bytes) {}

    void bytes(std::uint8_t* value, std::size_t length) {
        if (consumed_ > expected_bytes_ ||
            static_cast<std::uint64_t>(length) >
            expected_bytes_ - consumed_) {
            throw ClassicalDirectContextCacheError(
                "direct cache payload length is inconsistent");
        }
        source_.read(value, length);
        crc_ = crc64_update(crc_, value, length);
        consumed_ += static_cast<std::uint64_t>(length);
    }

    std::uint64_t u64() {
        std::array<std::uint8_t, 8U> encoded{};
        bytes(encoded.data(), encoded.size());
        return decode_u64(encoded);
    }

    std::uint64_t crc() const { return crc_; }
    std::uint64_t consumed() const { return consumed_; }
    std::uint64_t remaining() const {
        if (consumed_ > expected_bytes_) {
            throw ClassicalDirectContextCacheError(
                "direct cache reader consumed excessive payload");
        }
        return expected_bytes_ - consumed_;
    }

private:
    ReadFile& source_;
    std::uint64_t crc_;
    std::uint64_t expected_bytes_;
    std::uint64_t consumed_ = 0U;
};

void sync_parent_directory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path().empty()
        ? std::filesystem::path(".")
        : path.parent_path();
    const int descriptor = ::open(parent.c_str(), O_RDONLY);
    if (descriptor < 0) {
        throw ClassicalDirectContextCacheError(
            errno_text("cannot open direct-cache directory", parent));
    }
    const int result = ::fsync(descriptor);
    const int saved_errno = errno;
    ::close(descriptor);
    if (result != 0) {
        errno = saved_errno;
        throw ClassicalDirectContextCacheError(
            errno_text("cannot flush direct-cache directory", parent));
    }
}

void validate_compact_matrices(
    std::uint64_t level, std::uint64_t prime,
    const std::vector<std::uint64_t>& lagrange,
    const std::vector<std::uint64_t>& neighbors) {
    const std::uint64_t width_u64 = checked_add(level, 2U, "matrix width");
    const std::uint64_t entries_u64 = square_entries(level);
    if (entries_u64 > std::numeric_limits<std::size_t>::max()) {
        throw ClassicalDirectContextCacheError(
            "direct-cache matrix does not fit address space");
    }
    const std::size_t width = static_cast<std::size_t>(width_u64);
    const std::size_t entries = static_cast<std::size_t>(entries_u64);
    if (prime < 2U || !is_prime_u64(prime) ||
        lagrange.size() != entries || neighbors.size() != entries) {
        throw ClassicalDirectContextCacheError(
            "direct-cache interpolation dimensions or prime are invalid");
    }
    for (std::size_t index = 0U; index < entries; ++index) {
        if (lagrange[index] >= prime || neighbors[index] >= prime) {
            throw ClassicalDirectContextCacheError(
                "direct-cache interpolation coefficient is noncanonical");
        }
    }
    for (std::size_t degree = 0U; degree < width; ++degree) {
        std::uint64_t sum = 0U;
        for (std::size_t row = 0U; row < width; ++row) {
            sum = static_cast<std::uint64_t>(
                (static_cast<unsigned __int128>(sum) +
                 lagrange[row * width + degree]) % prime);
        }
        const std::uint64_t expected = degree == 0U ? 1U : 0U;
        if (sum != expected) {
            throw ClassicalDirectContextCacheError(
                "direct-cache Lagrange rows do not partition unity");
        }
    }
    for (std::size_t row = 0U; row < width; ++row) {
        if (neighbors[row * width + width - 1U] != 1U) {
            throw ClassicalDirectContextCacheError(
                "direct-cache neighbor row is not monic");
        }
    }
}

bool same_witness(const SutherlandCrtPrime& expected,
                  std::uint64_t prime, std::uint64_t trace,
                  std::uint64_t volcano_parameter) {
    std::uint64_t expected_prime = 0U;
    std::uint64_t expected_trace = 0U;
    std::uint64_t expected_volcano_parameter = 0U;
    return export_u64(expected.prime, expected_prime) &&
           export_u64(expected.trace, expected_trace) &&
           export_u64(expected.volcano_parameter,
                      expected_volcano_parameter) &&
           expected_prime == prime && expected_trace == trace &&
           expected_volcano_parameter == volcano_parameter;
}

}  // namespace

class DirectContextCacheCodec {
public:
    static std::string save(
        const ClassicalDirectSeaContext& context,
        const std::filesystem::path& cache_path,
        ClassicalDirectContextCacheLimits limits) {
        validate_limits(limits);
        if (cache_path.empty()) {
            throw ClassicalDirectContextCacheError(
                "classical direct context cache path is empty");
        }
        if (context.levels_.empty() ||
            context.levels_.size() > limits.max_levels) {
            throw ClassicalDirectContextCacheError(
                "classical direct context cache level count is invalid");
        }
        const std::vector<std::uint8_t> target =
            export_positive(context.target_modulus_);

        std::vector<const ClassicalDirectLevelContext*> level_contexts;
        level_contexts.reserve(context.levels_.size());
        std::uint64_t total_witnesses = 0U;
        std::uint64_t total_coefficients = 0U;
        std::uint64_t payload_bytes =
            static_cast<std::uint64_t>(target.size());
        for (std::size_t index = 0U; index < context.levels_.size(); ++index) {
            const ClassicalDirectLevelContext& level_context =
                context.level_context(index);
            if (level_context.level() != context.levels_[index] ||
                level_context.target_modulus_ != context.target_modulus_ ||
                level_context.witnesses_.size() !=
                    level_context.interpolation_surfaces_.size() ||
                level_context.witnesses_.empty() ||
                level_context.witnesses_.size() >
                    limits.max_witnesses_per_level) {
                throw ClassicalDirectContextCacheError(
                    "classical direct context is incomplete or mismatched");
            }
            const std::uint64_t witness_count =
                static_cast<std::uint64_t>(level_context.witnesses_.size());
            const std::uint64_t entries = square_entries(
                static_cast<std::uint64_t>(level_context.level()));
            const std::uint64_t coefficients = checked_mul(
                checked_mul(witness_count, 2U, "coefficient count"),
                entries, "coefficient count");
            total_witnesses = checked_add(total_witnesses, witness_count,
                                          "witness count");
            total_coefficients = checked_add(
                total_coefficients, coefficients, "coefficient count");
            payload_bytes = checked_add(payload_bytes, 16U,
                                        "payload size");
            const std::uint64_t witness_bytes = checked_add(
                24U, checked_mul(16U, entries, "witness matrix bytes"),
                "witness bytes");
            payload_bytes = checked_add(
                payload_bytes,
                checked_mul(witness_count, witness_bytes,
                            "level payload size"),
                "payload size");
            level_contexts.push_back(&level_context);
        }
        const std::uint64_t file_bytes = checked_add(
            kClassicalDirectContextCacheHeaderBytes, payload_bytes,
            "file size");
        if (file_bytes > limits.max_file_bytes) {
            throw ClassicalDirectContextCacheError(
                "classical direct context cache exceeds configured byte limit");
        }

        std::array<std::uint8_t, kHeaderBytes> header{};
        std::copy(kMagic.begin(), kMagic.end(), header.begin());
        put_u32(header, 8U, kClassicalDirectContextCacheVersion);
        put_u32(header, 12U,
                static_cast<std::uint32_t>(kHeaderBytes));
        put_u64(header, 16U, payload_bytes);
        put_u64(header, 24U, 0U);
        put_u64(header, 32U,
                static_cast<std::uint64_t>(level_contexts.size()));
        put_u64(header, 40U, context.maximum_prime_candidates_);
        put_u64(header, 48U,
                context.maximum_x_candidates_per_surface_);
        put_u64(header, 56U,
                static_cast<std::uint64_t>(target.size()));
        put_u64(header, 64U, total_witnesses);
        put_u64(header, 72U, total_coefficients);
        put_u64(header, 80U, 0U);
        put_u64(header, 88U, 0U);

        TemporaryFile temporary = open_temporary(cache_path);
        write_all(temporary.descriptor, header.data(), header.size(),
                  temporary.path);
        PayloadWriter writer(temporary.descriptor, temporary.path,
                             header_crc_seed(header));
        writer.bytes(target.data(), target.size());
        for (const ClassicalDirectLevelContext* level_context :
             level_contexts) {
            writer.u64(static_cast<std::uint64_t>(level_context->level()));
            writer.u64(static_cast<std::uint64_t>(
                level_context->witnesses_.size()));
            for (std::size_t index = 0U;
                 index < level_context->witnesses_.size(); ++index) {
                const SutherlandCrtPrime& witness =
                    level_context->witnesses_[index];
                const ClassicalDirectLevelContext::InterpolationSurface&
                    surface = level_context->interpolation_surfaces_[index];
                const std::uint64_t prime = export_required_u64(
                    witness.prime, "auxiliary prime");
                const std::uint64_t trace = export_required_u64(
                    witness.trace, "auxiliary trace");
                const std::uint64_t volcano_parameter = export_required_u64(
                    witness.volcano_parameter, "volcano parameter");
                if (surface.auxiliary_prime != witness.prime) {
                    throw ClassicalDirectContextCacheError(
                        "direct-cache surface prime lost witness synchronization");
                }
                validate_compact_matrices(
                    static_cast<std::uint64_t>(level_context->level()),
                    prime, surface.lagrange_coefficients,
                    surface.neighbor_coefficients);
                writer.u64(prime);
                writer.u64(trace);
                writer.u64(volcano_parameter);
                for (const std::uint64_t coefficient :
                     surface.lagrange_coefficients) {
                    writer.u64(coefficient);
                }
                for (const std::uint64_t coefficient :
                     surface.neighbor_coefficients) {
                    writer.u64(coefficient);
                }
            }
        }
        if (writer.count() != payload_bytes) {
            throw ClassicalDirectContextCacheError(
                "direct-cache writer produced the wrong payload length");
        }
        writer.flush();
        const std::array<std::uint8_t, 8U> encoded_crc =
            encode_u64(writer.crc());
        pwrite_all(temporary.descriptor, encoded_crc.data(),
                   encoded_crc.size(), 24, temporary.path);
        if (::fsync(temporary.descriptor) != 0) {
            throw ClassicalDirectContextCacheError(
                errno_text("cannot flush direct cache", temporary.path));
        }
        const int descriptor = temporary.descriptor;
        temporary.descriptor = -1;
        if (::close(descriptor) != 0) {
            throw ClassicalDirectContextCacheError(
                errno_text("cannot close direct cache", temporary.path));
        }
        if (::rename(temporary.path.c_str(), cache_path.c_str()) != 0) {
            throw ClassicalDirectContextCacheError(
                errno_text("cannot atomically rename direct cache",
                           cache_path));
        }
        temporary.path.clear();
        sync_parent_directory(cache_path);
        return sha256_file(cache_path);
    }

    static ClassicalDirectSeaContext load(
        const Field& target_field,
        const std::vector<std::uint64_t>& levels,
        std::uint64_t maximum_prime_candidates,
        std::uint64_t maximum_x_candidates_per_surface,
        std::size_t preparation_threads,
        const std::filesystem::path& cache_path,
        const std::string& trusted_sha256,
        ClassicalDirectContextCacheLimits limits) {
        const auto start = std::chrono::steady_clock::now();
        validate_limits(limits);
        if (cache_path.empty()) {
            throw ClassicalDirectContextCacheError(
                "classical direct context cache path is empty");
        }
        if (!is_lower_sha256(trusted_sha256)) {
            throw ClassicalDirectContextCacheError(
                "trusted direct-cache SHA-256 is not canonical");
        }
        ClassicalDirectSeaContext result =
            make_classical_direct_sea_context(
                target_field, levels, maximum_prime_candidates,
                maximum_x_candidates_per_surface, preparation_threads);
        if (sha256_file(cache_path) != trusted_sha256) {
            throw ClassicalDirectContextCacheError(
                "classical direct context cache SHA-256 mismatch");
        }
        ReadFile source(cache_path);
        if (source.size() < kClassicalDirectContextCacheHeaderBytes ||
            source.size() > limits.max_file_bytes) {
            throw ClassicalDirectContextCacheError(
                "direct-cache file size is invalid or excessive");
        }
        std::array<std::uint8_t, kHeaderBytes> header{};
        source.read(header.data(), header.size());
        if (!std::equal(kMagic.begin(), kMagic.end(), header.begin()) ||
            get_u32(header, 8U) !=
                kClassicalDirectContextCacheVersion ||
            get_u32(header, 12U) !=
                kClassicalDirectContextCacheHeaderBytes) {
            throw ClassicalDirectContextCacheError(
                "unsupported classical direct context cache header");
        }
        const std::uint64_t payload_bytes = get_u64(header, 16U);
        const std::uint64_t recorded_crc = get_u64(header, 24U);
        const std::uint64_t level_count = get_u64(header, 32U);
        const std::uint64_t encoded_prime_cap = get_u64(header, 40U);
        const std::uint64_t encoded_x_cap = get_u64(header, 48U);
        const std::uint64_t target_bytes = get_u64(header, 56U);
        const std::uint64_t encoded_total_witnesses = get_u64(header, 64U);
        const std::uint64_t encoded_total_coefficients = get_u64(header, 72U);
        if (get_u64(header, 80U) != 0U || get_u64(header, 88U) != 0U ||
            level_count == 0U || level_count > limits.max_levels ||
            level_count != levels.size() ||
            encoded_prime_cap != maximum_prime_candidates ||
            encoded_x_cap != maximum_x_candidates_per_surface ||
            target_bytes == 0U || target_bytes > kMaximumTargetBytes ||
            payload_bytes > limits.max_file_bytes -
                                kClassicalDirectContextCacheHeaderBytes ||
            source.size() !=
                kClassicalDirectContextCacheHeaderBytes + payload_bytes) {
            throw ClassicalDirectContextCacheError(
                "direct-cache metadata does not match the expected schedule or file size");
        }

        PayloadReader reader(source, header_crc_seed(header), payload_bytes);
        std::vector<std::uint8_t> target(
            static_cast<std::size_t>(target_bytes));
        reader.bytes(target.data(), target.size());
        if (target.front() == 0U) {
            throw ClassicalDirectContextCacheError(
                "direct-cache target encoding is noncanonical");
        }
        mpz_class decoded_target;
        mpz_import(decoded_target.get_mpz_t(), target.size(), 1, 1, 1, 0,
                   target.data());
        if (decoded_target != target_field.modulus()) {
            throw ClassicalDirectContextCacheError(
                "direct-cache target does not match the requested field");
        }

        std::vector<std::unique_ptr<ClassicalDirectLevelContext>> contexts;
        contexts.reserve(levels.size());
        std::uint64_t total_witnesses = 0U;
        std::uint64_t total_coefficients = 0U;
        for (std::size_t level_index = 0U; level_index < levels.size();
             ++level_index) {
            const std::uint64_t encoded_level = reader.u64();
            const std::uint64_t witness_count = reader.u64();
            if (encoded_level != levels[level_index] ||
                encoded_level > std::numeric_limits<unsigned>::max() ||
                witness_count == 0U ||
                witness_count > limits.max_witnesses_per_level ||
                witness_count > std::numeric_limits<std::size_t>::max()) {
                throw ClassicalDirectContextCacheError(
                    "direct-cache level or witness count is invalid");
            }
            SutherlandSuitableOrder order =
                derive_three_power_suitable_order(
                    static_cast<unsigned>(encoded_level));
            const CrtCoefficientBound coefficient_bound =
                derive_proved_classical_algorithm1_coefficient_bound(
                    static_cast<unsigned>(encoded_level),
                    target_field.modulus());
            std::vector<SutherlandCrtPrime> witnesses =
                select_sutherland_crt_primes(
                    order, target_field.modulus(),
                    coefficient_bound.absolute_bound(),
                    maximum_prime_candidates);
            if (witnesses.size() != witness_count) {
                throw ClassicalDirectContextCacheError(
                    "direct-cache witness count differs from deterministic selection");
            }
            const std::uint64_t entries_u64 = square_entries(encoded_level);
            if (entries_u64 > std::numeric_limits<std::size_t>::max()) {
                throw ClassicalDirectContextCacheError(
                    "direct-cache matrix exceeds the address space");
            }
            const std::uint64_t bytes_per_witness = checked_add(
                24U,
                checked_mul(16U, entries_u64,
                            "loaded witness matrix bytes"),
                "loaded witness bytes");
            if (checked_mul(witness_count, bytes_per_witness,
                            "loaded level bytes") > reader.remaining()) {
                throw ClassicalDirectContextCacheError(
                    "direct-cache matrix dimensions exceed the remaining payload");
            }
            const std::size_t entries =
                static_cast<std::size_t>(entries_u64);
            std::vector<ClassicalDirectLevelContext::InterpolationSurface>
                surfaces;
            surfaces.reserve(static_cast<std::size_t>(witness_count));
            for (std::size_t witness_index = 0U;
                 witness_index < witnesses.size(); ++witness_index) {
                const std::uint64_t prime = reader.u64();
                const std::uint64_t trace = reader.u64();
                const std::uint64_t volcano_parameter = reader.u64();
                if (!same_witness(witnesses[witness_index], prime, trace,
                                  volcano_parameter)) {
                    throw ClassicalDirectContextCacheError(
                        "direct-cache witness differs from deterministic selection");
                }
                std::vector<std::uint64_t> lagrange(entries);
                std::vector<std::uint64_t> neighbors(entries);
                for (std::uint64_t& coefficient : lagrange) {
                    coefficient = reader.u64();
                }
                for (std::uint64_t& coefficient : neighbors) {
                    coefficient = reader.u64();
                }
                validate_compact_matrices(encoded_level, prime, lagrange,
                                          neighbors);
                surfaces.push_back({mpz_class(std::to_string(prime)),
                                    std::move(lagrange),
                                    std::move(neighbors)});
            }
            total_witnesses = checked_add(total_witnesses, witness_count,
                                          "loaded witness count");
            total_coefficients = checked_add(
                total_coefficients,
                checked_mul(checked_mul(witness_count, 2U,
                                        "loaded coefficient count"),
                            entries_u64, "loaded coefficient count"),
                "loaded coefficient count");
            contexts.push_back(
                std::unique_ptr<ClassicalDirectLevelContext>(
                    new ClassicalDirectLevelContext(
                        std::move(order), target_field.modulus(),
                        coefficient_bound.absolute_bound(),
                        std::move(witnesses), std::move(surfaces))));
        }
        if (reader.consumed() != payload_bytes ||
            reader.crc() != recorded_crc ||
            total_witnesses != encoded_total_witnesses ||
            total_coefficients != encoded_total_coefficients) {
            throw ClassicalDirectContextCacheError(
                "direct-cache checksum, payload, or aggregate count is inconsistent");
        }
        if (sha256_file(cache_path) != trusted_sha256) {
            throw ClassicalDirectContextCacheError(
                "classical direct context cache changed while loading");
        }
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed < 0 ||
            static_cast<unsigned long long>(elapsed) >
                std::numeric_limits<std::uint64_t>::max()) {
            throw ClassicalDirectContextCacheError(
                "direct-cache load timing is out of range");
        }
        result.install_cached_contexts(
            std::move(contexts), static_cast<std::uint64_t>(elapsed));
        return result;
    }
};

std::string save_classical_direct_context_cache(
    const ClassicalDirectSeaContext& context,
    const std::filesystem::path& cache_path,
    ClassicalDirectContextCacheLimits limits) {
    return DirectContextCacheCodec::save(context, cache_path, limits);
}

ClassicalDirectSeaContext load_classical_direct_context_cache(
    const Field& target_field, const std::vector<std::uint64_t>& levels,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface,
    std::size_t preparation_threads,
    const std::filesystem::path& cache_path,
    const std::string& trusted_sha256,
    ClassicalDirectContextCacheLimits limits) {
    return DirectContextCacheCodec::load(
        target_field, levels, maximum_prime_candidates,
        maximum_x_candidates_per_surface, preparation_threads, cache_path,
        trusted_sha256, limits);
}

}  // namespace oneshotsea
