#include "oneshotsea/weber_table_trust.hpp"

#include "oneshotsea/integrity.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>

namespace oneshotsea {
namespace {

constexpr const char* kTrustedManifestSha256 =
    "e5b84a87335523f79d47f140d70ceddee9e1c6891a7fae8aaadcae5c080de3ab";
constexpr std::uintmax_t kMaximumManifestBytes = 1024U * 1024U;

struct TrustedTableRecord {
    std::uint64_t level;
    std::uintmax_t bytes;
    std::string sha256;
};

std::string read_manifest(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(path, error);
    if (error || bytes == 0U || bytes > kMaximumManifestBytes) {
        throw std::runtime_error(
            "trusted Weber manifest has an invalid byte size");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open trusted Weber manifest");
    }
    std::string payload((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    if (input.bad() || payload.size() != bytes) {
        throw std::runtime_error("cannot read trusted Weber manifest");
    }
    return payload;
}

std::map<std::string, TrustedTableRecord> parse_pinned_manifest(
    const std::string& payload) {
    // The manifest itself is content-addressed before this parser runs. This
    // intentionally accepts only the checked-in sorted json.dumps layout.
    const std::regex record(
        R"manifest("(phi_([0-9]+)\.txt)"\s*:\s*\{\s*"bytes"\s*:\s*([0-9]+),\s*"level"\s*:\s*([0-9]+),\s*"sha256"\s*:\s*"([0-9a-f]{64})"\s*\})manifest");
    std::map<std::string, TrustedTableRecord> records;
    for (std::sregex_iterator current(payload.begin(), payload.end(), record),
         end;
         current != end; ++current) {
        const std::string filename = (*current)[1].str();
        std::uint64_t filename_level = 0;
        std::uint64_t level = 0;
        std::uintmax_t bytes = 0;
        try {
            filename_level = std::stoull((*current)[2].str());
            bytes = static_cast<std::uintmax_t>(
                std::stoull((*current)[3].str()));
            level = std::stoull((*current)[4].str());
        } catch (const std::exception&) {
            throw std::runtime_error(
                "trusted Weber manifest contains an invalid integer");
        }
        if (bytes == 0U || level != filename_level ||
            !records.emplace(filename,
                             TrustedTableRecord{level, bytes,
                                                (*current)[5].str()})
                 .second) {
            throw std::runtime_error(
                "trusted Weber manifest contains an invalid table record");
        }
    }
    if (records.empty()) {
        throw std::runtime_error("trusted Weber manifest has no table records");
    }
    return records;
}

}  // namespace

void authenticate_trusted_weber_table_set(
    const std::filesystem::path& table_directory) {
    if (!std::filesystem::is_directory(table_directory)) {
        throw std::invalid_argument("Weber table directory does not exist");
    }
    const std::filesystem::path manifest = table_directory / "MANIFEST.json";
    if (!std::filesystem::is_regular_file(manifest) ||
        sha256_file(manifest) != kTrustedManifestSha256) {
        throw std::runtime_error("trusted Weber manifest digest mismatch");
    }
    const auto records = parse_pinned_manifest(read_manifest(manifest));
    const std::regex table_name(R"(^phi_([0-9]+)\.txt$)");
    std::size_t actual_tables = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(table_directory)) {
        if (!std::regex_match(entry.path().filename().string(), table_name)) {
            continue;
        }
        ++actual_tables;
        const std::string filename = entry.path().filename().string();
        const auto expected = records.find(filename);
        if (expected == records.end() || !entry.is_regular_file() ||
            entry.file_size() != expected->second.bytes ||
            sha256_file(entry.path()) != expected->second.sha256) {
            throw std::runtime_error(
                "trusted Weber table authentication failed: " + filename);
        }
    }
    if (actual_tables != records.size()) {
        throw std::runtime_error(
            "trusted Weber table set is missing a manifest entry");
    }
}

}  // namespace oneshotsea
