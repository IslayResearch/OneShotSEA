#pragma once

#include "oneshotsea/field.hpp"
#include "oneshotsea/poly.hpp"

#include <vector>

namespace oneshotsea {

// The BLS normalization f=zeta_48^-1*eta((z+1)/2)/eta(z), with
// j=(f^24-16)^3/f^24.
mpz_class j_from_weber_f(const Field& field, const mpz_class& weber_f);
mpz_class j_derivative_from_weber_f(const Field& field,
                                    const mpz_class& weber_f);

// Return every rational root of (F^24-16)^3-F^24*j. The p=11 mod 12
// two-root shortcut is intentionally not used: both production targets are
// 1 mod 12 and require exhaustive lift enumeration.
std::vector<mpz_class> weber_f_lifts(const Field& field, const mpz_class& j);

}  // namespace oneshotsea
