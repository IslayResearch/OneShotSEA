#pragma once

#include "oneshotsea/curve.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/poly.hpp"

#include <cstdint>

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

struct BmssIsogenyResult {
    Poly kernel;
    Poly numerator;
    Poly denominator;
};

// Correctness-first implementation of the odd-degree BMSS fastElkies' path.
// It uses the paper's power-series equation and Pade reconstruction, with the
// defensive precision 4*ell+4. Polynomial arithmetic is currently quadratic;
// a production Newton/fast-multiplication backend will retain this interface.
BmssIsogenyResult bmss_isogeny_reference(
    const Curve& source, const Curve& normalized_codomain, std::uint64_t ell);

}  // namespace oneshotsea
