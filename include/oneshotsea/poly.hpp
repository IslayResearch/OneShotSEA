#pragma once

#include "oneshotsea/field.hpp"

#include <utility>
#include <vector>

namespace oneshotsea {

class PolyModContext;

class Poly {
public:
    explicit Poly(const Field& field);
    Poly(const Field& field, std::vector<mpz_class> coefficients);

    static Poly constant(const Field& field, const mpz_class& value);
    static Poly x(const Field& field);

    const Field& field() const { return field_; }
    const std::vector<mpz_class>& coefficients() const { return coefficients_; }
    int degree() const;
    bool is_zero() const;
    bool is_one() const;
    mpz_class coefficient(std::size_t index) const;
    mpz_class leading_coefficient() const;
    mpz_class evaluate(const mpz_class& value) const;

    Poly derivative() const;
    Poly monic() const;

private:
    struct NormalizedCoefficientsTag {};

    // Selected internal arithmetic results have already been reduced
    // coefficient by coefficient into [0,p). Preserve that proof boundary so
    // hot paths can trim the representation without normalizing and copying
    // it again.
    Poly(const Field& field, std::vector<mpz_class> coefficients,
         NormalizedCoefficientsTag);

    friend Poly mulmod(const Poly& lhs, const Poly& rhs,
                       const Poly& modulus);
    friend Poly squaremod(const Poly& value, const Poly& modulus);
    friend Poly add(const Poly& lhs, const Poly& rhs);
    friend Poly sub(const Poly& lhs, const Poly& rhs);
    friend Poly neg(const Poly& value);
    friend Poly scalar_mul(const Poly& value, const mpz_class& scalar);
    friend std::pair<Poly, Poly> divmod(const Poly& numerator,
                                       const Poly& denominator);
    friend class PolyModContext;

    // Own the immutable field context so a polynomial cannot outlive it.
    Field field_;
    std::vector<mpz_class> coefficients_;

    void trim();
};

// Prepared arithmetic in one quotient F_p[x]/(modulus).  The reciprocal of a
// sufficiently large monic modulus is computed once and reused across every
// multiplication, square, and exponentiation.  This matters for SEA
// Frobenius/eigenvalue work, where hundreds of point operations share the
// same kernel polynomial.
class PolyModContext {
public:
    explicit PolyModContext(const Poly& modulus);

    const Poly& modulus() const { return modulus_; }
    Poly reduce(const Poly& value) const;
    Poly multiply(const Poly& lhs, const Poly& rhs) const;
    Poly square(const Poly& value) const;
    Poly pow(Poly base, mpz_class exponent) const;

private:
    Poly modulus_;
    std::vector<mpz_class> reciprocal_;

    void reduce_coefficients(std::vector<mpz_class>& coefficients) const;
};

Poly add(const Poly& lhs, const Poly& rhs);
Poly sub(const Poly& lhs, const Poly& rhs);
Poly neg(const Poly& value);
Poly mul(const Poly& lhs, const Poly& rhs);
Poly scalar_mul(const Poly& value, const mpz_class& scalar);
std::pair<Poly, Poly> divmod(const Poly& numerator, const Poly& denominator);
Poly mod(const Poly& numerator, const Poly& denominator);
// Multiply or square directly in F_p[x]/(modulus), avoiding a full product
// allocation followed by generic long division.
Poly mulmod(const Poly& lhs, const Poly& rhs, const Poly& modulus);
Poly squaremod(const Poly& value, const Poly& modulus);
Poly gcd(Poly lhs, Poly rhs);
Poly powmod(Poly base, mpz_class exponent, const Poly& modulus);
int rational_root_count(const Poly& polynomial);
// Return every distinct F_p root in sorted order.  Cantor-Zassenhaus splitting
// is deterministic and validated; failure to split within the fixed attempt
// budget throws rather than returning an incomplete set.
std::vector<mpz_class> linear_roots(const Poly& polynomial);
bool equal(const Poly& lhs, const Poly& rhs);

}  // namespace oneshotsea
