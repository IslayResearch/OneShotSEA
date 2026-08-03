#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace oneshotsea {

// Incremental project-owned SHA-256.  Exposing the accumulator lets large
// authenticated artifacts bind independently streamed regions without
// materializing those regions in memory.
class Sha256Hasher {
public:
    void update(std::string_view bytes);
    void update(const std::uint8_t* bytes, std::size_t size);
    std::string hex_digest();

private:
    static std::uint32_t load_be(const std::uint8_t* value);
    void transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_ = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_ = 0;
    std::uint64_t byte_count_ = 0;
    bool finalized_ = false;
};

bool is_lower_sha256(const std::string& value);
std::string sha256_file(const std::filesystem::path& path);

// True when two pathnames resolve to the same pathname or, when both already
// exist, to the same filesystem object (including hard links).
bool paths_alias(const std::filesystem::path& left,
                 const std::filesystem::path& right);

}  // namespace oneshotsea
