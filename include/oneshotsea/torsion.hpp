#pragma once

#include "oneshotsea/curve.hpp"
#include "oneshotsea/poly.hpp"

#include <cstdint>
#include <optional>

namespace oneshotsea {

// Slow division-polynomial construction used by independent Schoof/Elkies
// reference paths. Production kernel reconstruction will not build full
// psi_ell at target-sized levels.
Poly division_polynomial_reference(const Curve& curve, std::uint64_t ell);

// Given a validated Frobenius-stable cyclic ell-kernel polynomial of degree
// (ell-1)/2, find the unique lambda in F_ell^* for which pi(P)=[lambda]P in
// the kernel quotient algebra. Throws on an invalid or non-eigenkernel input.
std::uint64_t frobenius_eigenvalue_reference(const Curve& curve,
                                             const Poly& kernel,
                                             std::uint64_t ell);

// Candidate-screening form: malformed inputs still throw, while a divisor of
// psi_ell that is not one closed Frobenius eigenline returns nullopt.
std::optional<std::uint64_t> try_frobenius_eigenvalue_reference(
    const Curve& curve, const Poly& kernel, std::uint64_t ell);

// Production-kernel form: the caller has already proved that `kernel` is the
// denominator square root of a validated degree-ell rational isogeny, so this
// skips construction of the full division polynomial.
std::optional<std::uint64_t> try_frobenius_eigenvalue_from_isogeny_kernel(
    const Curve& curve, const Poly& kernel, std::uint64_t ell);

}  // namespace oneshotsea
