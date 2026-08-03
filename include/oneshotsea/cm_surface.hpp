#pragma once

#include "oneshotsea/class_polynomial.hpp"
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
    friend CmSurfaceEnumeration enumerate_cm_interpolation_surfaces_limited(
        const SutherlandSuitableOrder&, const SutherlandCrtPrime&,
        const Poly&, std::uint64_t, std::size_t);
    friend CrtSpecializationResidue specialize_classical_from_cm_surfaces(
        const CmSurfaceEnumeration&, const std::vector<mpz_class>&);

    unsigned level_;
    mpz_class auxiliary_prime_;
    std::vector<mpz_class> all_surface_invariants_;
    std::vector<CmSurfaceCurve> surface_curves_;
    mpz_class exact_group_order_;
    std::size_t horizontal_edges_per_surface_;
};

// Immutable curve-independent state for one classical direct SEA level over a
// fixed target characteristic.  Construction internally selects every
// witnessed CRT prime, derives its ring class polynomial, and admits the
// complete interpolation surface.  It then retains only the immutable
// Lagrange and neighbor-coefficient matrices: auxiliary curves, kernels, and
// isogenies are released before the context is returned.  Search workers may
// share this compact context; only target-j matrix evaluation, CRT
// combination, and the target-field SEA consumer remain per curve.
class ClassicalDirectLevelContext {
public:
    unsigned level() const { return order_.level(); }
    const mpz_class& target_modulus() const { return target_modulus_; }
    const mpz_class& order_discriminant() const {
        return order_.discriminant();
    }
    std::uint64_t class_number() const { return order_.class_number(); }
    std::size_t auxiliary_prime_count() const { return witnesses_.size(); }
    std::size_t interpolation_coefficient_count() const;
    std::size_t interpolation_storage_bytes() const;

private:
    struct InterpolationSurface {
        mpz_class auxiliary_prime;
        std::vector<std::uint64_t> lagrange_coefficients;
        std::vector<std::uint64_t> neighbor_coefficients;
    };

    ClassicalDirectLevelContext(
        SutherlandSuitableOrder order, mpz_class target_modulus,
        mpz_class coefficient_abs_bound,
        std::vector<SutherlandCrtPrime> witnesses,
        std::vector<InterpolationSurface> interpolation_surfaces);

    friend ClassicalDirectLevelContext
    prepare_classical_direct_level_context(
        const SutherlandSuitableOrder&, const Field&, std::uint64_t,
        std::uint64_t, std::size_t);
    friend CrtSpecializationResult
    reconstruct_classical_specialization_from_prepared_context(
        const ClassicalDirectLevelContext&, const Field&, const mpz_class&);

    SutherlandSuitableOrder order_;
    mpz_class target_modulus_;
    mpz_class coefficient_abs_bound_;
    std::vector<SutherlandCrtPrime> witnesses_;
    std::vector<InterpolationSurface> interpolation_surfaces_;
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

// Checked direct-evaluation overload: the HCP is derived internally from the
// fixed Phi_3 ring-class tower and tied by its opaque type and metadata to this
// order and auxiliary prime.
CmSurfaceEnumeration enumerate_cm_interpolation_surfaces(
    const SutherlandSuitableOrder& order,
    const SutherlandCrtPrime& prime_witness,
    const ClassicalCmClassPolynomial& class_polynomial,
    std::uint64_t maximum_x_candidates_per_surface);

// Evaluate the interpolation polynomial only through the two Algorithm 1
// linear functionals defined by the supplied target-field power lifts.  This
// returns Phi_ell(j,Y) and Phi_X(j,Y) modulo the auxiliary prime without ever
// constructing or loading the bivariate target-level modular polynomial.
CrtSpecializationResidue specialize_classical_from_cm_surfaces(
    const CmSurfaceEnumeration& surfaces,
    const std::vector<mpz_class>& target_power_lifts);

// Complete table-free classical Algorithm 1 route for the D=-7*3^(2n)
// family.  This is the narrow entry point that prevents caller-supplied class
// polynomials or target-level modular polynomials from entering the producer.
CrtSpecializationResult reconstruct_classical_specialization_from_cm(
    const SutherlandSuitableOrder& order, const Field& target_field,
    const mpz_class& source_j, std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface);

// Independent CRT witnesses may be prepared concurrently. A zero worker
// limit selects hardware concurrency (falling back to one); a positive value
// is a strict ceiling, additionally capped by the witness count. The returned
// witness and surface order is independent of scheduling.
ClassicalDirectLevelContext prepare_classical_direct_level_context(
    const SutherlandSuitableOrder& order, const Field& target_field,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface,
    std::size_t worker_threads = 1U);

CrtSpecializationResult
reconstruct_classical_specialization_from_prepared_context(
    const ClassicalDirectLevelContext& context, const Field& target_field,
    const mpz_class& source_j);

}  // namespace oneshotsea
