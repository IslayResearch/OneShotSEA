#pragma once

#include "oneshotsea/curve.hpp"

#include <cstdint>
#include <vector>

namespace oneshotsea {

// Slow, independent Schoof characteristic-equation computation. This is the
// native differential reference for exact SEA residues, not the production
// Elkies kernel path. Intended for small odd ell and capped at ell <= 31.
std::uint64_t schoof_trace_mod_ell(const Curve& curve, std::uint64_t ell);

struct SchoofCountResult {
    mpz_class trace;
    mpz_class order;
    mpz_class residue_modulus;
    std::vector<std::uint64_t> levels;
};

// Complete reference point count by CRT-combining exact Schoof residues until
// exactly one trace remains in the Hasse interval. Intended for small fields;
// production uses the faster Elkies path but must return the same result.
SchoofCountResult schoof_count_reference(const Curve& curve, std::uint64_t max_ell);

}  // namespace oneshotsea
