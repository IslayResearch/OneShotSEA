#pragma once

#include <filesystem>

namespace oneshotsea {

// Authenticate the complete checked-in Weber-f table set against the pinned
// provenance manifest. A copied directory is accepted only when every table,
// byte count, digest, and filename matches; missing, extra, or altered inputs
// fail closed before production SEA begins.
void authenticate_trusted_weber_table_set(
    const std::filesystem::path& table_directory);

}  // namespace oneshotsea
