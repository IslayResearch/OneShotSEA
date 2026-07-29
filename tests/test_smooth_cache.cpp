#include "oneshotsea/smooth_cache.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TestDirectory {
public:
    TestDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            path = std::filesystem::temp_directory_path() /
                   ("oneshotsea-smooth-cache-" + std::to_string(tick) + "-" +
                    std::to_string(attempt));
            if (std::filesystem::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error("could not create smooth-cache test directory");
    }

    ~TestDirectory() {
        std::error_code error;
        if (path.filename().string().starts_with("oneshotsea-smooth-cache-")) {
            std::filesystem::remove_all(path, error);
        }
    }

    std::filesystem::path path;
};

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(stream.good(), "open test cache for reading");
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    check(stream.good(), "open test cache for writing");
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    check(stream.good(), "write test cache");
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | bytes.at(offset + index);
    }
    return value;
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>(
            value >> static_cast<unsigned>((7U - index) * 8U));
    }
}

void expect_load_failure(const std::filesystem::path& path,
                         oneshotsea::SmoothCacheLimits limits = {}) {
    smooth_base destination{};
    mpz_init_set_ui(destination.P, 17);
    destination.y = 23;
    destination.lo = 5;
    destination.nprimes = 7;
    bool failed = false;
    try {
        oneshotsea::load_portable_smooth_base(destination, path, limits);
    } catch (const oneshotsea::SmoothCacheError&) {
        failed = true;
    }
    check(failed, "malformed cache was accepted");
    check(mpz_cmp_ui(destination.P, 17) == 0 && destination.y == 23 &&
              destination.lo == 5 && destination.nprimes == 7,
          "failed load modified destination");
    mpz_clear(destination.P);
}

}  // namespace

int main() {
    TestDirectory temporary;
    const auto cache = temporary.path / "base.cache";

    smooth_base built{};
    smooth_base_build(&built, 100, 2);
    const auto built_cache = temporary.path / "built.cache";
    oneshotsea::save_portable_smooth_base(built, built_cache);
    smooth_base built_loaded{};
    mpz_init_set_ui(built_loaded.P, 1);
    oneshotsea::load_portable_smooth_base(built_loaded, built_cache);
    check(built_loaded.y == built.y && built_loaded.lo == built.lo &&
              built_loaded.nprimes == built.nprimes &&
              mpz_cmp(built_loaded.P, built.P) == 0 &&
              smooth_base_selfcheck(&built_loaded) == 1,
          "portable cache roundtrip of a constructed smooth base");
    mpz_t input[1];
    mpz_t part[1];
    mpz_init_set_ui(input[0], 2094336);
    mpz_init(part[0]);
    smooth_parts(&built_loaded, input, 1, part, 2);
    check(mpz_cmp_ui(part[0], 20736) == 0,
          "loaded portable base computes exact smooth part");
    mpz_clear(part[0]);
    mpz_clear(input[0]);
    smooth_base_clear(&built_loaded);
    smooth_base_clear(&built);

    smooth_base original{};
    mpz_init(original.P);
    check(mpz_set_str(original.P, "0102030405060708090a", 16) == 0,
          "set source product");
    original.y = UINT64_C(0x0102030405060708);
    original.lo = UINT64_C(0x0001020304050607);
    original.nprimes = UINT64_C(0x0000000000012345);
    oneshotsea::save_portable_smooth_base(original, cache);

    const auto encoded = read_bytes(cache);
    bool limited_save_failed = false;
    try {
        oneshotsea::save_portable_smooth_base(
            original, cache, {.max_product_bytes = 9});
    } catch (const oneshotsea::SmoothCacheError&) {
        limited_save_failed = true;
    }
    check(limited_save_failed && read_bytes(cache) == encoded,
          "failed bounded save preserves existing cache atomically");
    const std::array<std::uint8_t, 8> magic = {'O', 'S', 'S', 'M', 'B', 'A', 'S', 'E'};
    check(encoded.size() == oneshotsea::kPortableSmoothCacheHeaderBytes + 10,
          "fixed header and canonical product length");
    check(std::equal(magic.begin(), magic.end(), encoded.begin()), "fixed cache magic");
    check(encoded.at(8) == 0 && encoded.at(9) == 0 && encoded.at(10) == 0 &&
              encoded.at(11) == oneshotsea::kPortableSmoothCacheVersion,
          "big-endian cache version");
    check(read_u64(encoded, 16) == original.y && read_u64(encoded, 24) == original.lo &&
              read_u64(encoded, 32) == original.nprimes && read_u64(encoded, 40) == 10,
          "big-endian fixed-width metadata");
    check(read_u64(encoded, 48) == UINT64_C(0x4bed7a0ead0f323e),
          "fixed CRC64-ECMA encoding");
    const std::array<std::uint8_t, 10> product = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    check(std::equal(product.begin(), product.end(), encoded.begin() + 64),
          "canonical GMP big-endian byte export");

    smooth_base loaded{};
    mpz_init_set_ui(loaded.P, 1);
    oneshotsea::load_portable_smooth_base(loaded, cache);
    check(mpz_cmp(loaded.P, original.P) == 0 && loaded.y == original.y &&
              loaded.lo == original.lo && loaded.nprimes == original.nprimes,
          "portable smooth-cache roundtrip");
    mpz_clear(loaded.P);

    auto corrupt = encoded;
    corrupt.back() ^= 0x80U;
    const auto corrupt_path = temporary.path / "corrupt.cache";
    write_bytes(corrupt_path, corrupt);
    expect_load_failure(corrupt_path);

    auto truncated = encoded;
    truncated.pop_back();
    const auto truncated_path = temporary.path / "truncated.cache";
    write_bytes(truncated_path, truncated);
    expect_load_failure(truncated_path);

    auto wrong_magic = encoded;
    wrong_magic.front() ^= 0xffU;
    const auto wrong_magic_path = temporary.path / "wrong-magic.cache";
    write_bytes(wrong_magic_path, wrong_magic);
    expect_load_failure(wrong_magic_path);

    auto excessive = encoded;
    write_u64(excessive, 40, 1025);
    const auto excessive_path = temporary.path / "excessive.cache";
    write_bytes(excessive_path, excessive);
    expect_load_failure(excessive_path, {.max_product_bytes = 1024});

    mpz_set_ui(original.P, 65537);
    original.y = 100;
    original.lo = 0;
    original.nprimes = 25;
    oneshotsea::save_portable_smooth_base(original, cache);
    mpz_init_set_ui(loaded.P, 0);
    oneshotsea::load_portable_smooth_base(loaded, cache);
    check(mpz_cmp_ui(loaded.P, 65537) == 0 && loaded.y == 100,
          "atomic save replaces an existing cache");
    mpz_clear(loaded.P);

    for (const auto& entry : std::filesystem::directory_iterator(temporary.path)) {
        check(entry.path().filename().string().find(".tmp.") == std::string::npos,
              "atomic save left a temporary file behind");
    }

    mpz_clear(original.P);
    std::cout << "all smooth-cache tests passed\n";
    return 0;
}
