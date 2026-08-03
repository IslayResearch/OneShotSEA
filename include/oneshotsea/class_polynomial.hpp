#pragma once

#include "oneshotsea/direct_modpoly.hpp"

namespace oneshotsea {

// An H_O modulo one witnessed auxiliary prime whose coefficients were derived
// internally from the fixed exact classical Phi_3 and the ring-class tower
// D=-7*3^(2n).  Construction is restricted so the CM interpolation boundary
// cannot accidentally treat caller-supplied coefficients as authenticated.
class ClassicalCmClassPolynomial {
public:
    const mpz_class& discriminant() const { return discriminant_; }
    const mpz_class& auxiliary_prime() const { return auxiliary_prime_; }
    const Poly& polynomial() const { return polynomial_; }

private:
    ClassicalCmClassPolynomial(mpz_class discriminant,
                               mpz_class auxiliary_prime,
                               Poly polynomial);

    friend ClassicalCmClassPolynomial
    derive_three_power_class_polynomial_mod_prime(
        const SutherlandSuitableOrder&, const SutherlandCrtPrime&);

    mpz_class discriminant_;
    mpz_class auxiliary_prime_;
    Poly polynomial_;
};

// Deterministically select the suitable family D=-7*3^(2n) described after
// Definition 1 of Sutherland 2012, using its stated constants c1=4,c2=16.
// This classical-j family is available for every odd prime ell>3; it is not a
// Weber-f family because its conductor is divisible by 3.
SutherlandSuitableOrder derive_three_power_suitable_order(unsigned level);

// Compute H_O modulo the retained auxiliary prime without an integer HCP,
// floating-point CM approximation, or target-level modular polynomial.  The
// exact identities are
//
//   Res_X(H_1(X), Phi_3(X,Y)) = H_3(Y),
//   Res_X(H_f(X), Phi_3(X,Y))
//       = H_(f/3)(Y)^[h(f)/h(f/3)] H_(3f)(Y)  (3 | f).
//
// Every division is checked exactly in F_p[Y], and all expected degrees are
// checked against the already validated order.
ClassicalCmClassPolynomial derive_three_power_class_polynomial_mod_prime(
    const SutherlandSuitableOrder& order,
    const SutherlandCrtPrime& prime_witness);

}  // namespace oneshotsea
