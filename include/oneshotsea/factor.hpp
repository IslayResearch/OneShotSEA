#pragma once

#include "oneshotsea/poly.hpp"

#include <optional>
#include <vector>

namespace oneshotsea {

struct IrreducibleFactor {
    Poly polynomial;
    unsigned long multiplicity;
};

// Completely factor a nonzero polynomial over a probable-prime F_p.
//
// The leading scalar is intentionally omitted: every returned polynomial is
// monic, irreducible, and paired with its positive multiplicity, so multiplying
// factor^multiplicity reconstructs polynomial.monic().  Repeated factors are
// therefore represented explicitly rather than silently discarded.  A nonzero
// constant has an empty factorization; the zero polynomial is rejected.
//
// Results are deterministic and sorted by degree, then lexicographically by
// the coefficient tuple (constant coefficient first).  Equal-degree splitting
// uses a fixed, deterministic Cantor-Zassenhaus attempt budget and throws on
// exhaustion rather than returning a partial factorization.  Before returning,
// the implementation independently checks irreducibility and exact
// reconstruction.
std::vector<IrreducibleFactor> factor_polynomial(const Poly& polynomial);

// Certify that a square-free nonconstant polynomial over a probable-prime
// field is a product of distinct irreducible polynomials all having one common
// degree.  Return that degree, including one for a product of distinct linear
// factors.  Return nullopt for constants, repeated factors, or mixed factor
// degrees; reject the zero polynomial and composite field moduli.
//
// This uses Frobenius identities and gcd certificates only.  It deliberately
// avoids equal-degree splitting when a caller needs the common degree but not
// the factors themselves.
std::optional<unsigned int> uniform_irreducible_factor_degree(
    const Poly& polynomial);

// The same certificate while reusing the already validated X^p image retained
// by a complete rational-root computation for this exact polynomial.
std::optional<unsigned int> uniform_irreducible_factor_degree(
    const CertifiedLinearRoots& roots);

}  // namespace oneshotsea
