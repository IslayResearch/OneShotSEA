#pragma once

#include "oneshotsea/modpoly.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace oneshotsea {

// One quadratic order that has passed the finite, checkable conditions in
// Sutherland 2012, Definition 1.  Construction is intentionally restricted to
// validate_sutherland_suitable_order so downstream prime selection cannot be
// given an unchecked discriminant by accident.
class SutherlandSuitableOrder {
public:
    unsigned level() const { return level_; }
    const mpz_class& fundamental_discriminant() const {
        return fundamental_discriminant_;
    }
    const mpz_class& conductor() const { return conductor_; }
    const mpz_class& discriminant() const { return discriminant_; }
    std::uint64_t class_number() const { return class_number_; }

    // Sutherland 2012, Section 3.9 requires (D/2)=1 and (D/3)!=0 for the
    // Weber-f class polynomial to split in the ring class field.  These are
    // necessary order congruences, not proof that a particular auxiliary
    // Weber value is a root of the corresponding class polynomial.
    bool weber_f_order_congruences_hold() const;

private:
    SutherlandSuitableOrder(unsigned level,
                            mpz_class fundamental_discriminant,
                            mpz_class conductor, mpz_class discriminant,
                            std::uint64_t class_number);

    friend SutherlandSuitableOrder validate_sutherland_suitable_order(
        unsigned, const mpz_class&, const mpz_class&, std::uint64_t,
        std::uint64_t, std::uint64_t);

    unsigned level_;
    mpz_class fundamental_discriminant_;
    mpz_class conductor_;
    mpz_class discriminant_;
    std::uint64_t class_number_;
};

// Count primitive reduced positive-definite binary quadratic forms of the
// given negative discriminant.  This correctness-first implementation is
// intended for the O(ell^2)-sized discriminants used during order selection,
// not as a replacement for a fast class-group implementation.
std::uint64_t negative_order_class_number(const mpz_class& discriminant);

// Validate Definition 1 with c1=c1_numerator/c1_denominator and integer c2.
// The defaults are the practical constants c1=1.5 and c2=256 stated in the
// paper.  All conditions, including fundamental-discriminant/conductor
// structure and the exact class number, are recomputed here.
SutherlandSuitableOrder validate_sutherland_suitable_order(
    unsigned level, const mpz_class& fundamental_discriminant,
    const mpz_class& conductor, std::uint64_t c1_numerator = 3U,
    std::uint64_t c1_denominator = 2U, std::uint64_t c2 = 256U);

struct SutherlandCrtPrime {
    mpz_class prime;
    mpz_class trace;
    mpz_class volcano_parameter;
};

// Deterministic version of the paper's practical prime search: fix v=2 when
// D=1 mod 8 (otherwise v=1), enumerate positive t=2 mod ell with the required
// parity, and retain probable primes p=(t^2-ell^2*v^2*D)/4.  Exhausting the
// explicit candidate cap fails closed.
std::vector<SutherlandCrtPrime> select_sutherland_crt_primes(
    const SutherlandSuitableOrder& order, const mpz_class& target_modulus,
    const mpz_class& coefficient_abs_bound,
    std::uint64_t maximum_candidates);

// Canonical target-field powers lifted to integers in [0,q), as required by
// Algorithm 1.  Reducing these lifts modulo an auxiliary CRT prime is not the
// same operation as exponentiating the integer representative there.
std::vector<mpz_class> lifted_target_powers(
    const Field& target_field, const mpz_class& source_x,
    unsigned maximum_degree);

struct CrtSpecializationResidue {
    mpz_class prime;
    // Both arrays are padded to ell+2 coefficients in ascending Y degree.
    std::vector<mpz_class> value_coefficients;
    std::vector<mpz_class> x_derivative_coefficients;
};

using CrtSpecializationResidueProvider =
    std::function<CrtSpecializationResidue(const mpz_class& prime)>;

using SutherlandSpecializationResidueProvider =
    std::function<CrtSpecializationResidue(
        const SutherlandCrtPrime& prime,
        const std::vector<mpz_class>& target_power_lifts)>;

struct CrtSpecializationResult {
    ModularPolynomialSpecialization specialization;
    mpz_class crt_product;
    mpz_class coefficient_abs_bound;
    std::size_t prime_count;
};

// Reconstruct Phi_ell(x,Y) and Phi_X(x,Y) modulo the target field from a
// streamed sequence of auxiliary-prime specializations.  The prime product
// must exceed four times the declared absolute coefficient bound.  Every
// centered CRT lift is then checked against that bound before the result can
// cross the specialization trust boundary.  Auxiliary primes are currently
// restricted to the 64-bit range and proved prime by deterministic
// Miller--Rabin; larger probable primes are rejected rather than trusted.
CrtSpecializationResult reconstruct_specialization_explicit_crt(
    unsigned level, const Field& target_field, const mpz_class& source_x,
    const mpz_class& coefficient_abs_bound,
    const std::vector<mpz_class>& auxiliary_primes,
    const CrtSpecializationResidueProvider& provider);

// Checked Algorithm 1 orchestration for the Weber-f path.  It enforces the
// necessary order congruences, selects suitable CRT primes with their (t,v)
// witnesses, and reconstructs the target specialization.  The callback is the
// deliberately narrow integration point for the still-separate per-prime
// isogeny-volcano evaluator; that producer must also prove that its Weber
// values belong to the corresponding class invariant set.
CrtSpecializationResult reconstruct_weber_specialization_algorithm1(
    const SutherlandSuitableOrder& order, const Field& target_field,
    const mpz_class& source_x, const mpz_class& coefficient_abs_bound,
    std::uint64_t maximum_candidates,
    const SutherlandSpecializationResidueProvider& provider);

// Full-table reference adapter for differential tests of Algorithm 1.  It
// substitutes the target-field lifted powers into a sparse bivariate table
// modulo one auxiliary prime and computes the algebraic X derivative using
// x^(i-1).  Production direct evaluation must replace the table with volcano
// output while retaining this residue interface.
CrtSpecializationResidue specialize_sparse_modpoly_for_crt_reference(
    const SparseModularPolynomial& modular_polynomial,
    const std::vector<mpz_class>& target_power_lifts,
    const mpz_class& auxiliary_prime);

}  // namespace oneshotsea
