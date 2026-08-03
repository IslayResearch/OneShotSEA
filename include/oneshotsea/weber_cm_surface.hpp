#pragma once

#include "oneshotsea/cm_surface.hpp"
#include "oneshotsea/modpoly.hpp"

#include <cstddef>
#include <vector>

namespace oneshotsea {

struct WeberCmSpecialization {
    CrtSpecializationResidue residue;
    std::size_t surface_invariant_count;
    std::size_t floor_invariant_count;
    std::size_t orientation_relation_count;
    mpz_class relative_sign_coefficient;
};

// Convert a checked classical CM surface/floor enumeration to Weber-f rows.
// The caller supplies a Weber class polynomial for the surface and one or more
// independently authenticated, target-independent small Weber modular
// polynomials whose union connects both class-group torsors.  The auxiliary
// prime must be 11 mod 12, so every j has exactly the +/- Weber pair.  Exactly
// one of the two global floor orientations must yield the normalized
// X^ell*Y^ell coefficient -1; zero or two matches fail closed as prescribed in
// BLS Section 7.3.  The target-level Weber polynomial is explicitly rejected.
WeberCmSpecialization specialize_weber_from_cm_surfaces(
    const CmSurfaceEnumeration& surfaces,
    const Poly& weber_surface_class_polynomial_mod_prime,
    const std::vector<SparseModularPolynomial>& orientation_relations,
    const std::vector<mpz_class>& target_power_lifts);

}  // namespace oneshotsea
