#pragma once

#include "oneshotsea/curve.hpp"

#include <cstdint>

namespace oneshotsea {

// A Weber-first search sample.  For j != 0,1728 there are exactly two
// F_p-isomorphism classes with this j-invariant; curve and twist represent
// both classes.  The sampled Weber-f value is a rational source lift for each.
struct WeberCurvePair {
    mpz_class weber_f;
    mpz_class j_invariant;
    mpz_class twist_parameter;
    Curve curve;
    Curve twist;
    std::uint64_t rejected_samples;
};

// Construct the two nonexceptional curve classes belonging to a rational
// Weber-f value.  The field modulus must be a probable prime greater than 7.
// Zero and the ramified j=0,1728 images are rejected with std::domain_error.
WeberCurvePair weber_curve_pair_from_f(const Field& field,
                                       const mpz_class& weber_f);

// Deterministically sample Weber-f first, retrying zero, exceptional images,
// and images with no F_p-rational Montgomery coefficient usable by the
// canonical certificate tail, then construct the corresponding curve/twist
// pair. Replaying the same (prime, seed, global_index) returns exactly the
// same field values.
WeberCurvePair deterministic_weber_curve_pair(const mpz_class& prime,
                                               std::uint64_t seed,
                                               std::uint64_t global_index);

}  // namespace oneshotsea
