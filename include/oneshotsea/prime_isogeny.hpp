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

// The compact evidence needed while constructing interpolation rows.  The
// exact-order generator witnesses the subgroup; the codomain is obtained by
// direct Velu point sums.  Omitting the kernel polynomial avoids constructing
// ell+1 degree-(ell-1)/2 polynomials when only neighbor j-invariants survive.
struct RationalPrimeIsogenyNeighbor {
    AffinePoint generator;
    Curve codomain;
};

struct RationalPrimeIsogenyNeighborEnumeration {
    std::vector<RationalPrimeIsogenyNeighbor> neighbors;
    std::uint64_t x_candidates_tested;
    unsigned group_order_level_valuation;
};

// Deterministically scan affine x-coordinates and project rational points to
// the ell-primary subgroup until two independent order-ell points are proved.
// The projective line <P>, <Q+kP> then enumerates all ell+1 cyclic subgroups
// without a coupon-collector scan.  Exhausting maximum_x_candidates before a
// basis is found fails closed.  x_candidates_tested counts the basis scan.
RationalPrimeIsogenyEnumeration enumerate_rational_prime_isogenies(
    const Curve& curve, unsigned level, const mpz_class& exact_group_order,
    std::uint64_t maximum_x_candidates);

// The same proved-basis/projective-line enumeration, retaining only generator
// and codomain evidence.  This is the direct-specialization hot path; the full
// kernel-retaining API above remains an independent differential oracle.
RationalPrimeIsogenyNeighborEnumeration
enumerate_rational_prime_isogeny_neighbors(
    const Curve& curve, unsigned level, const mpz_class& exact_group_order,
    std::uint64_t maximum_x_candidates);

}  // namespace oneshotsea
