#pragma once

#include "oneshotsea/curve.hpp"
#include "oneshotsea/poly.hpp"

#include <cstdint>
#include <optional>

namespace oneshotsea {

struct BmssIsogenyResult;

// Explicit search strategies exposed only so the two exact algorithms can be
// differentially tested on the same validated kernel.  Production entry
// points retain their automatic selector and fallback behavior.
enum class FrobeniusEigenvalueTestPath : std::uint8_t {
    linear,
    meet_in_the_middle,
};

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

// Differential-test hook.  Performs the same independent division-polynomial
// and subgroup-closure validation as try_frobenius_eigenvalue_reference, then
// runs exactly the requested search path without automatic fallback.
std::optional<std::uint64_t>
try_frobenius_eigenvalue_reference_for_testing(
    const Curve& curve, const Poly& kernel, std::uint64_t ell,
    FrobeniusEigenvalueTestPath path);

// Production-isogeny form: revalidate the complete rational map, including its
// reduced degree and kernel-square denominator, before deliberately skipping
// the independent division-polynomial and subgroup-closure checks.
std::optional<std::uint64_t> try_frobenius_eigenvalue_from_isogeny(
    const Curve& curve, const Curve& normalized_codomain,
    const BmssIsogenyResult& isogeny, std::uint64_t ell);

// BMSS counterpart to the differential-test hook.  The complete rational map
// is still revalidated before the requested search path is entered.
std::optional<std::uint64_t>
try_frobenius_eigenvalue_from_isogeny_for_testing(
    const Curve& curve, const Curve& normalized_codomain,
    const BmssIsogenyResult& isogeny, std::uint64_t ell,
    FrobeniusEigenvalueTestPath path);

}  // namespace oneshotsea
