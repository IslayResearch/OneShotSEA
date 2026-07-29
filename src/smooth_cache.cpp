#include "oneshotsea/smooth_cache.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>

namespace oneshotsea {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {
    'O', 'S', 'S', 'M', 'B', 'A', 'S', 'E'};
constexpr std::size_t kHeaderBytes =
    static_cast<std::size_t>(kPortableSmoothCacheHeaderBytes);
constexpr std::uint64_t kCrc64Polynomial = UINT64_C(0x42f0e1eba9ea3693);
constexpr std::size_t kIoChunkBytes = 1U << 30U;

std::string errno_text(const std::string& operation,
                       const std::filesystem::path& path) {
    return operation + " " + path.string() + ": " + std::strerror(errno);
}

void put_u32(std::array<std::uint8_t, kHeaderBytes>& bytes, std::size_t offset,
             std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>((3U - index) * 8U));
    }
}

void put_u64(std::array<std::uint8_t, kHeaderBytes>& bytes, std::size_t offset,
             std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>((7U - index) * 8U));
    }
}

std::uint32_t get_u32(const std::array<std::uint8_t, kHeaderBytes>& bytes,
                      std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = static_cast<std::uint32_t>((value << 8U) | bytes[offset + index]);
    }
    return value;
}

std::uint64_t get_u64(const std::array<std::uint8_t, kHeaderBytes>& bytes,
                      std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | bytes[offset + index];
    }
    return value;
}

std::uint64_t crc64_update(std::uint64_t crc, const std::uint8_t* bytes,
                           std::size_t length) {
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= static_cast<std::uint64_t>(bytes[index]) << 56U;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool high = (crc & UINT64_C(0x8000000000000000)) != 0;
            crc <<= 1U;
            if (high) {
                crc ^= kCrc64Polynomial;
            }
        }
    }
    return crc;
}

std::uint64_t cache_crc(const std::array<std::uint8_t, kHeaderBytes>& header,
                        const std::vector<std::uint8_t>& product) {
    // The checksum field at [48,56) is omitted. The reserved field remains
    // covered, as do all other metadata, the magic, and the product bytes.
    std::uint64_t crc = crc64_update(0, header.data(), 48);
    crc = crc64_update(crc, header.data() + 56, 8);
    return crc64_update(crc, product.data(), product.size());
}

void validate_limits(SmoothCacheLimits limits) {
    if (limits.max_product_bytes == 0) {
        throw SmoothCacheError("smooth-cache byte limit must be positive");
    }
}

void validate_metadata(std::uint64_t y, std::uint64_t lo,
                       std::uint64_t nprimes) {
    if (lo > y) {
        throw SmoothCacheError("smooth-cache lower bound exceeds upper bound");
    }
    if (nprimes > y) {
        throw SmoothCacheError("smooth-cache prime count is inconsistent with upper bound");
    }
}

std::vector<std::uint8_t> export_product(const smooth_base& base,
                                         SmoothCacheLimits limits) {
    if (mpz_sgn(base.P) <= 0) {
        throw SmoothCacheError("smooth-cache product must be positive");
    }
    const std::size_t bits = mpz_sizeinbase(base.P, 2);
    const std::size_t length = (bits + 7U) / 8U;
    if (length > limits.max_product_bytes) {
        throw SmoothCacheError("smooth-cache product exceeds configured byte limit");
    }
    std::vector<std::uint8_t> bytes(length);
    std::size_t written = 0;
    mpz_export(bytes.data(), &written, 1, 1, 1, 0, base.P);
    if (written != length || bytes.empty() || bytes.front() == 0) {
        throw SmoothCacheError("GMP produced a non-canonical smooth-cache product");
    }
    return bytes;
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
    static std::atomic<std::uint64_t> sequence{0};
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::uint64_t attempt = 0; attempt < 128; ++attempt) {
        std::ostringstream suffix;
        suffix << ".tmp." << std::hex << tick << '.'
               << sequence.fetch_add(1, std::memory_order_relaxed) << '.' << attempt;
        TemporaryFile temporary;
        temporary.path = target.string() + suffix.str();
        temporary.descriptor = ::open(temporary.path.c_str(),
                                      O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (temporary.descriptor >= 0) {
            return temporary;
        }
        if (errno != EEXIST) {
            throw SmoothCacheError(errno_text("cannot create temporary cache", temporary.path));
        }
    }
    throw SmoothCacheError("cannot allocate a unique temporary smooth-cache path");
}

void write_all(int descriptor, const std::uint8_t* bytes, std::size_t length,
               const std::filesystem::path& path) {
    while (length != 0) {
        const std::size_t chunk = std::min(length, kIoChunkBytes);
        const ssize_t written = ::write(descriptor, bytes, chunk);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw SmoothCacheError(errno_text("cannot write cache", path));
        }
        if (written == 0) {
            throw SmoothCacheError("short write to smooth-cache file " + path.string());
        }
        const auto count = static_cast<std::size_t>(written);
        bytes += count;
        length -= count;
    }
}

class ReadFile {
public:
    explicit ReadFile(const std::filesystem::path& source) : path(source) {
        descriptor = ::open(path.c_str(), O_RDONLY);
        if (descriptor < 0) {
            throw SmoothCacheError(errno_text("cannot open cache", path));
        }
    }

    ReadFile(const ReadFile&) = delete;
    ReadFile& operator=(const ReadFile&) = delete;
    ~ReadFile() { ::close(descriptor); }

    std::uint64_t size() const {
        struct stat attributes {};
        if (::fstat(descriptor, &attributes) != 0) {
            throw SmoothCacheError(errno_text("cannot stat cache", path));
        }
        if (attributes.st_size < 0) {
            throw SmoothCacheError("smooth-cache file has negative size");
        }
        return static_cast<std::uint64_t>(attributes.st_size);
    }

    void read_all(std::uint8_t* bytes, std::size_t length) const {
        while (length != 0) {
            const std::size_t chunk = std::min(length, kIoChunkBytes);
            const ssize_t received = ::read(descriptor, bytes, chunk);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw SmoothCacheError(errno_text("cannot read cache", path));
            }
            if (received == 0) {
                throw SmoothCacheError("truncated smooth-cache file " + path.string());
            }
            const auto count = static_cast<std::size_t>(received);
            bytes += count;
            length -= count;
        }
    }

    int descriptor = -1;
    std::filesystem::path path;
};

}  // namespace

void save_portable_smooth_base(const smooth_base& base,
                               const std::filesystem::path& cache_path,
                               SmoothCacheLimits limits) {
    validate_limits(limits);
    if (cache_path.empty()) {
        throw SmoothCacheError("smooth-cache path is empty");
    }
    validate_metadata(base.y, base.lo, base.nprimes);
    const std::vector<std::uint8_t> product = export_product(base, limits);

    std::array<std::uint8_t, kHeaderBytes> header{};
    std::copy(kMagic.begin(), kMagic.end(), header.begin());
    put_u32(header, 8, kPortableSmoothCacheVersion);
    put_u32(header, 12, static_cast<std::uint32_t>(kHeaderBytes));
    put_u64(header, 16, base.y);
    put_u64(header, 24, base.lo);
    put_u64(header, 32, base.nprimes);
    put_u64(header, 40, static_cast<std::uint64_t>(product.size()));
    put_u64(header, 56, 0);
    put_u64(header, 48, cache_crc(header, product));

    TemporaryFile temporary = open_temporary(cache_path);
    write_all(temporary.descriptor, header.data(), header.size(), temporary.path);
    write_all(temporary.descriptor, product.data(), product.size(), temporary.path);
    if (::fsync(temporary.descriptor) != 0) {
        throw SmoothCacheError(errno_text("cannot flush cache", temporary.path));
    }
    const int descriptor = temporary.descriptor;
    temporary.descriptor = -1;
    if (::close(descriptor) != 0) {
        throw SmoothCacheError(errno_text("cannot close cache", temporary.path));
    }
    if (::rename(temporary.path.c_str(), cache_path.c_str()) != 0) {
        throw SmoothCacheError(errno_text("cannot atomically rename cache", cache_path));
    }
    temporary.path.clear();
}

void load_portable_smooth_base(smooth_base& destination,
                               const std::filesystem::path& cache_path,
                               SmoothCacheLimits limits) {
    validate_limits(limits);
    ReadFile source(cache_path);
    const std::uint64_t file_size = source.size();
    if (file_size < kPortableSmoothCacheHeaderBytes) {
        throw SmoothCacheError("truncated smooth-cache header");
    }

    std::array<std::uint8_t, kHeaderBytes> header{};
    source.read_all(header.data(), header.size());
    if (!std::equal(kMagic.begin(), kMagic.end(), header.begin())) {
        throw SmoothCacheError("wrong smooth-cache magic");
    }
    if (get_u32(header, 8) != kPortableSmoothCacheVersion) {
        throw SmoothCacheError("unsupported smooth-cache version");
    }
    if (get_u32(header, 12) != kPortableSmoothCacheHeaderBytes) {
        throw SmoothCacheError("invalid smooth-cache header length");
    }

    const std::uint64_t y = get_u64(header, 16);
    const std::uint64_t lo = get_u64(header, 24);
    const std::uint64_t nprimes = get_u64(header, 32);
    const std::uint64_t product_length = get_u64(header, 40);
    const std::uint64_t recorded_crc = get_u64(header, 48);
    if (get_u64(header, 56) != 0) {
        throw SmoothCacheError("nonzero reserved smooth-cache metadata");
    }
    validate_metadata(y, lo, nprimes);
    if (product_length == 0 || product_length > limits.max_product_bytes ||
        product_length > std::numeric_limits<std::size_t>::max()) {
        throw SmoothCacheError("invalid or excessive smooth-cache product length");
    }
    if (product_length > std::numeric_limits<std::uint64_t>::max() -
                             kPortableSmoothCacheHeaderBytes ||
        file_size != kPortableSmoothCacheHeaderBytes + product_length) {
        throw SmoothCacheError("truncated or trailing smooth-cache payload");
    }

    std::vector<std::uint8_t> product(static_cast<std::size_t>(product_length));
    source.read_all(product.data(), product.size());
    if (product.front() == 0) {
        throw SmoothCacheError("non-canonical smooth-cache product encoding");
    }
    if (cache_crc(header, product) != recorded_crc) {
        throw SmoothCacheError("smooth-cache checksum mismatch");
    }

    mpz_t imported;
    mpz_init(imported);
    mpz_import(imported, product.size(), 1, 1, 1, 0, product.data());
    if (mpz_sgn(imported) <= 0) {
        mpz_clear(imported);
        throw SmoothCacheError("smooth-cache product is not positive");
    }
    mpz_swap(destination.P, imported);
    mpz_clear(imported);
    destination.y = y;
    destination.lo = lo;
    destination.nprimes = nprimes;
}

}  // namespace oneshotsea
