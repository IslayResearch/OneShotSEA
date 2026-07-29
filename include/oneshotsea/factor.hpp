#pragma once

#include "oneshotsea/poly.hpp"

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

}  // namespace oneshotsea
