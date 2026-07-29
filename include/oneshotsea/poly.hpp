#pragma once

#include "oneshotsea/field.hpp"

#include <utility>
#include <vector>

namespace oneshotsea {

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
    // Own the immutable field context so a polynomial cannot outlive it.
    Field field_;
    std::vector<mpz_class> coefficients_;

    void trim();
};

Poly add(const Poly& lhs, const Poly& rhs);
Poly sub(const Poly& lhs, const Poly& rhs);
Poly neg(const Poly& value);
Poly mul(const Poly& lhs, const Poly& rhs);
Poly scalar_mul(const Poly& value, const mpz_class& scalar);
std::pair<Poly, Poly> divmod(const Poly& numerator, const Poly& denominator);
Poly mod(const Poly& numerator, const Poly& denominator);
Poly gcd(Poly lhs, Poly rhs);
Poly powmod(Poly base, mpz_class exponent, const Poly& modulus);
int rational_root_count(const Poly& polynomial);
bool equal(const Poly& lhs, const Poly& rhs);

}  // namespace oneshotsea
