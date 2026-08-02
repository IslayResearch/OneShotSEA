#pragma once

#include "oneshotsea/curve.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/poly.hpp"

#include <cstdint>
#include <stdexcept>

namespace oneshotsea {

// Recover the uniquely normalized codomain attached to one simple classical-j
// modular neighbor. Exceptional invariants and zero modular derivatives are
// rejected rather than divided through.
Curve normalized_codomain_from_classical_modpoly(
    const Curve& source, const SparseModularPolynomial& modular_polynomial,
    const mpz_class& neighbor_j);

// Equation (8) of Sutherland 2012 specialized to the BLS Weber-f
// normalization. The X/Y derivative orientation is deliberately explicit.
Curve normalized_codomain_from_weber_modpoly(
    const Curve& source, const SparseModularPolynomial& weber_modular_polynomial,
    const mpz_class& source_weber_f, const mpz_class& neighbor_weber_f);

// The same normalized Weber codomain construction using only the specialized
// polynomials produced by Sutherland's direct-evaluation interface.  No full
// bivariate modular polynomial is required at this boundary.
Curve normalized_codomain_from_weber_specialization(
    const Curve& source,
    const ModularPolynomialSpecialization& weber_specialization,
    const mpz_class& neighbor_weber_f);

struct BmssIsogenyResult {
    Poly kernel;
    Poly numerator;
    Poly denominator;
};

// A mathematically well-formed normalized-codomain candidate for which the
// BMSS series does not reconstruct a degree-ell isogeny.  Weber modular
// polynomials can produce such incompatible class-invariant lifts.  Callers
// enumerating those lifts may skip this error specifically; all other BMSS
// exceptions indicate invalid inputs or failed implementation invariants and
// must propagate.
class BmssIncompatibleNeighborError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Validate the complete x-coordinate map x |-> numerator/denominator before
// treating `kernel` as a proved cyclic ell-isogeny kernel.  In particular the
// map must be reduced, have exact degree ell, have denominator kernel^2, and
// satisfy the Weierstrass isogeny identity.  This is intentionally public so
// callers that skip the independent subgroup-closure check can revalidate the
// full proof object at that trust boundary.
void validate_rational_isogeny_reference(
    const Curve& source, const Curve& normalized_codomain, std::uint64_t ell,
    const BmssIsogenyResult& isogeny);

// Correctness-first implementation of the odd-degree BMSS fastElkies' path.
// It uses the paper's power-series equation and Pade reconstruction, with the
// defensive precision 4*ell+4. Polynomial arithmetic is currently quadratic;
// a production Newton/fast-multiplication backend will retain this interface.
BmssIsogenyResult bmss_isogeny_reference(
    const Curve& source, const Curve& normalized_codomain, std::uint64_t ell);

}  // namespace oneshotsea
