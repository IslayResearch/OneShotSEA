#pragma once

#include "oneshotsea/curve.hpp"
#include "oneshotsea/poly.hpp"

#include <cstdint>
#include <vector>

namespace oneshotsea {

// Affine point arithmetic used over the small auxiliary prime fields in the
// isogeny-volcano producer.  Infinity has the canonical representation
// (x,y,infinity)=(0,0,true); finite coordinates must lie in [0,p).
struct AffinePoint {
    mpz_class x = 0;
    mpz_class y = 0;
    bool infinity = true;
};

bool affine_point_is_on_curve(const Curve& curve, const AffinePoint& point);
AffinePoint affine_point_negate(const Curve& curve,
                                const AffinePoint& point);
AffinePoint affine_point_add(const Curve& curve, const AffinePoint& lhs,
                             const AffinePoint& rhs);
AffinePoint affine_scalar_multiply(const Curve& curve,
                                   const mpz_class& scalar,
                                   AffinePoint point);

// Construct the monic product of X-x(kP), for 1<=k<=(ell-1)/2.  The input is
// accepted only after P is proved to have exact odd-prime order ell.
Poly cyclic_kernel_polynomial(const Curve& curve,
                              const AffinePoint& generator,
                              unsigned level);

// Apply Velu's quotient formulas directly to every nonzero point of <P>.
// This deliberately does not consult Phi_ell or the SEA division-polynomial
// path; tests compare it to both as independent oracles.
Curve velu_codomain_from_cyclic_subgroup(const Curve& curve,
                                         const AffinePoint& generator,
                                         unsigned level);

struct RationalPrimeIsogeny {
    AffinePoint generator;
    Poly kernel;
    Curve codomain;
};

struct RationalPrimeIsogenyEnumeration {
    std::vector<RationalPrimeIsogeny> isogenies;
    std::uint64_t x_candidates_tested;
    unsigned group_order_level_valuation;
};

// Deterministically scan affine x-coordinates, project rational points to the
// ell-primary subgroup using the supplied exact group order, and retain one
// generator for each distinct cyclic order-ell subgroup.  A full rational
// E[ell] has exactly ell+1 such subgroups.  Returning fewer would make a
// volcano row incomplete, so exhaustion of maximum_x_candidates fails closed.
RationalPrimeIsogenyEnumeration enumerate_rational_prime_isogenies(
    const Curve& curve, unsigned level, const mpz_class& exact_group_order,
    std::uint64_t maximum_x_candidates);

}  // namespace oneshotsea
