#pragma once

#include "oneshotsea/direct_modpoly.hpp"
#include "oneshotsea/prime_isogeny.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oneshotsea {

struct CmSurfaceEdge {
    RationalPrimeIsogeny isogeny;
    bool codomain_on_surface;
};

struct CmSurfaceCurve {
    mpz_class j_invariant;
    Curve curve;
    std::vector<CmSurfaceEdge> edges;
    std::uint64_t x_candidates_tested;
};

class CmSurfaceEnumeration {
public:
    unsigned level() const { return level_; }
    const mpz_class& auxiliary_prime() const { return auxiliary_prime_; }
    const std::vector<mpz_class>& all_surface_invariants() const {
        return all_surface_invariants_;
    }
    const std::vector<CmSurfaceCurve>& surface_curves() const {
        return surface_curves_;
    }
    const mpz_class& exact_group_order() const { return exact_group_order_; }
    std::size_t horizontal_edges_per_surface() const {
        return horizontal_edges_per_surface_;
    }

private:
    CmSurfaceEnumeration(
        unsigned level, mpz_class auxiliary_prime,
        std::vector<mpz_class> all_surface_invariants,
        std::vector<CmSurfaceCurve> surface_curves,
        mpz_class exact_group_order,
        std::size_t horizontal_edges_per_surface);

    friend CmSurfaceEnumeration enumerate_cm_interpolation_surfaces(
        const SutherlandSuitableOrder&, const SutherlandCrtPrime&,
        const Poly&, std::uint64_t);
    friend CrtSpecializationResidue specialize_classical_from_cm_surfaces(
        const CmSurfaceEnumeration&, const std::vector<mpz_class>&);

    unsigned level_;
    mpz_class auxiliary_prime_;
    std::vector<mpz_class> all_surface_invariants_;
    std::vector<CmSurfaceCurve> surface_curves_;
    mpz_class exact_group_order_;
    std::size_t horizontal_edges_per_surface_;
};

// Validate one H_O mod p instance against the checked order and CRT witness,
// require complete square-free splitting into h(O) roots, and admit every
// surface curve (the first ell+2 are used for interpolation).  For every curve,
// exactly one quadratic twist must yield all ell+1 rational cyclic kernels for
// the order p+1-t.  The class polynomial's mathematical provenance remains an
// obligation of the caller until the HCP producer receives its opaque checked
// type; this function validates all finite-field consequences it can.
CmSurfaceEnumeration enumerate_cm_interpolation_surfaces(
    const SutherlandSuitableOrder& order,
    const SutherlandCrtPrime& prime_witness,
    const Poly& hilbert_class_polynomial_mod_prime,
    std::uint64_t maximum_x_candidates_per_surface);

// Evaluate the interpolation polynomial only through the two Algorithm 1
// linear functionals defined by the supplied target-field power lifts.  This
// returns Phi_ell(j,Y) and Phi_X(j,Y) modulo the auxiliary prime without ever
// constructing or loading the bivariate target-level modular polynomial.
CrtSpecializationResidue specialize_classical_from_cm_surfaces(
    const CmSurfaceEnumeration& surfaces,
    const std::vector<mpz_class>& target_power_lifts);

}  // namespace oneshotsea
