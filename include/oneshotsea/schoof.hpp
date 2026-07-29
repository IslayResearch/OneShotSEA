#pragma once

#include "oneshotsea/curve.hpp"

#include <cstdint>

namespace oneshotsea {

// Slow, independent Schoof characteristic-equation computation. This is the
// native differential reference for exact SEA residues, not the production
// Elkies kernel path. Intended for small odd ell (initially through 13).
std::uint64_t schoof_trace_mod_ell(const Curve& curve, std::uint64_t ell);

}  // namespace oneshotsea
