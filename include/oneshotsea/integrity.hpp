#pragma once

#include <filesystem>
#include <string>

namespace oneshotsea {

bool is_lower_sha256(const std::string& value);
std::string sha256_file(const std::filesystem::path& path);

}  // namespace oneshotsea
