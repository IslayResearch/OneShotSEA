#pragma once

#include <filesystem>

namespace oneshotsea {

// Authenticate a nonempty Weber-f table subset against the pinned normalized
// source catalog derived from the content-addressed upstream archive. A copied
// directory is accepted only when its manifest records are present in that
// catalog and every declared filename, byte count, and digest matches. Missing,
// extra, unknown, or altered inputs fail closed before production SEA begins.
void authenticate_trusted_weber_table_set(
    const std::filesystem::path& table_directory);

}  // namespace oneshotsea
