#include "oneshotsea/weber_table_trust.hpp"

#include "oneshotsea/integrity.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace oneshotsea {
namespace {

constexpr const char* kTrustedSourceCatalogSha256 =
    "031c35989f12d8f93c3a992014d6275edb93a21a3a9c70b4b78ce317e7db5dd5";
constexpr const char* kTrustedSourceArchiveSha256 =
    "4ecc78a3163ba7232d67e3b2f5e678a2dbc038c7ee4a9d2e8c00c9e0b5a58176";
constexpr const char* kSourceCatalogHeader =
    "# OneShotSEA normalized Weber-f source catalog v1; "
    "archive_sha256=4ecc78a3163ba7232d67e3b2f5e678a2dbc038c7ee4a9d2e8c00c9e0b5a58176";
constexpr std::uintmax_t kMaximumManifestBytes = 1024U * 1024U;
constexpr std::uintmax_t kMaximumSourceCatalogBytes = 64U * 1024U;
constexpr std::size_t kTrustedSourceCatalogLevels = 166U;

struct TrustedTableRecord {
    std::uint64_t level;
    std::uintmax_t bytes;
    std::string sha256;
};

std::string read_bounded_file(const std::filesystem::path& path,
                              std::uintmax_t maximum_bytes,
                              const std::string& description) {
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(path, error);
    if (error || bytes == 0U || bytes > maximum_bytes) {
        throw std::runtime_error(description + " has an invalid byte size");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + description);
    }
    std::string payload((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    if (input.bad() || payload.size() != bytes) {
        throw std::runtime_error("cannot read " + description);
    }
    return payload;
}

std::map<std::string, TrustedTableRecord> parse_pinned_source_catalog(
    const std::string& payload) {
    std::istringstream input(payload);
    std::string line;
    if (!std::getline(input, line) || line != kSourceCatalogHeader) {
        throw std::runtime_error("trusted Weber source catalog header mismatch");
    }
    std::map<std::string, TrustedTableRecord> records;
    std::size_t line_number = 1U;
    while (std::getline(input, line)) {
        ++line_number;
        std::istringstream row(line);
        std::uint64_t level = 0U;
        std::uintmax_t bytes = 0U;
        std::string digest;
        std::string extra;
        if (!(row >> level >> bytes >> digest) || row >> extra || bytes == 0U ||
            !is_lower_sha256(digest)) {
            throw std::runtime_error(
                "malformed trusted Weber source catalog line " +
                std::to_string(line_number));
        }
        const std::string filename =
            "phi_" + std::to_string(level) + ".txt";
        if (!records.emplace(filename,
                             TrustedTableRecord{level, bytes, digest})
                 .second) {
            throw std::runtime_error(
                "trusted Weber source catalog contains a duplicate level");
        }
    }
    if (records.size() != kTrustedSourceCatalogLevels) {
        throw std::runtime_error(
            "trusted Weber source catalog has the wrong level set");
    }
    return records;
}

std::map<std::string, TrustedTableRecord> parse_pinned_manifest(
    const std::string& payload) {
    // Only source-catalog records are accepted below.  This parser
    // intentionally accepts the sorted json.dumps record layout emitted by
    // the archive converter, while allowing any nonempty catalog subset.
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
    const std::filesystem::path catalog =
        table_directory / "SOURCE_CATALOG.txt";
    if (!std::filesystem::is_regular_file(catalog) ||
        sha256_file(catalog) != kTrustedSourceCatalogSha256) {
        throw std::runtime_error("trusted Weber source catalog digest mismatch");
    }
    const auto catalog_records = parse_pinned_source_catalog(
        read_bounded_file(catalog, kMaximumSourceCatalogBytes,
                          "trusted Weber source catalog"));
    const std::filesystem::path manifest = table_directory / "MANIFEST.json";
    if (!std::filesystem::is_regular_file(manifest)) {
        throw std::runtime_error("trusted Weber manifest is missing");
    }
    const std::string manifest_payload = read_bounded_file(
        manifest, kMaximumManifestBytes, "trusted Weber manifest");
    const std::string archive_marker =
        std::string("\"source_archive_sha256\": \"") +
        kTrustedSourceArchiveSha256 + "\"";
    const std::string catalog_marker =
        std::string("\"source_catalog_sha256\": \"") +
        kTrustedSourceCatalogSha256 + "\"";
    if (manifest_payload.find(archive_marker) == std::string::npos ||
        manifest_payload.find(catalog_marker) == std::string::npos) {
        throw std::runtime_error(
            "trusted Weber manifest source provenance mismatch");
    }
    const auto records = parse_pinned_manifest(manifest_payload);
    for (const auto& [filename, record] : records) {
        const auto catalog_record = catalog_records.find(filename);
        if (catalog_record == catalog_records.end() ||
            catalog_record->second.level != record.level ||
            catalog_record->second.bytes != record.bytes ||
            catalog_record->second.sha256 != record.sha256) {
            throw std::runtime_error(
                "trusted Weber manifest record is absent from the pinned "
                "source catalog: " + filename);
        }
    }
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
