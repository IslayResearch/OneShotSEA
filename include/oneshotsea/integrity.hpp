#pragma once

#include <filesystem>
#include <string>

namespace oneshotsea {

bool is_lower_sha256(const std::string& value);
std::string sha256_file(const std::filesystem::path& path);

// True when two pathnames resolve to the same pathname or, when both already
// exist, to the same filesystem object (including hard links).
bool paths_alias(const std::filesystem::path& left,
                 const std::filesystem::path& right);

}  // namespace oneshotsea
