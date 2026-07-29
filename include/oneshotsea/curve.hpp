#pragma once

#include "oneshotsea/field.hpp"

#include <cstdint>

namespace oneshotsea {

class Curve {
public:
    Curve(Field field, mpz_class a, mpz_class b);

    const Field& field() const { return field_; }
    const mpz_class& a() const { return a_; }
    const mpz_class& b() const { return b_; }
    mpz_class discriminant_factor() const;
    bool is_singular() const;
    mpz_class j_invariant() const;
    Curve quadratic_twist(const mpz_class& nonsquare) const;

private:
    Field field_;
    mpz_class a_;
    mpz_class b_;
};

class MontgomeryCurve {
public:
    MontgomeryCurve(Field field, mpz_class coefficient);

    const Field& field() const { return field_; }
    const mpz_class& coefficient() const { return coefficient_; }
    bool is_singular() const;
    mpz_class j_invariant() const;
    Curve short_weierstrass() const;
    mpz_class short_x(const mpz_class& montgomery_x) const;
    mpz_class montgomery_x(const mpz_class& short_x) const;

private:
    Field field_;
    mpz_class coefficient_;
};

Curve deterministic_curve(const mpz_class& prime, std::uint64_t seed, std::uint64_t index);
MontgomeryCurve deterministic_montgomery_curve(const mpz_class& prime, std::uint64_t seed,
                                                std::uint64_t index);
mpz_class count_points_bruteforce(const Curve& curve, unsigned long limit = 10000000UL);

}  // namespace oneshotsea
