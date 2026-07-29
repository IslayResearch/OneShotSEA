#include "oneshotsea/curve.hpp"

#include <stdexcept>

namespace oneshotsea {

Curve::Curve(Field field, mpz_class a, mpz_class b)
    : field_(std::move(field)), a_(field_.normalize(a)), b_(field_.normalize(b)) {}

mpz_class Curve::discriminant_factor() const {
    const mpz_class four_a3 = field_.mul(4, field_.mul(a_, field_.square(a_)));
    const mpz_class twenty_seven_b2 = field_.mul(27, field_.square(b_));
    return field_.add(four_a3, twenty_seven_b2);
}

bool Curve::is_singular() const {
    return discriminant_factor() == 0;
}

mpz_class Curve::j_invariant() const {
    if (is_singular()) {
        throw std::domain_error("singular curve has no j-invariant");
    }
    const mpz_class four_a3 = field_.mul(4, field_.mul(a_, field_.square(a_)));
    return field_.divide(field_.mul(1728, four_a3), discriminant_factor());
}

Curve Curve::quadratic_twist(const mpz_class& nonsquare) const {
    if (field_.legendre(nonsquare) != -1) {
        throw std::invalid_argument("quadratic twist parameter must be a nonsquare");
    }
    const mpz_class d2 = field_.square(nonsquare);
    const mpz_class d3 = field_.mul(d2, nonsquare);
    return Curve(field_, field_.mul(d2, a_), field_.mul(d3, b_));
}

MontgomeryCurve::MontgomeryCurve(Field field, mpz_class coefficient)
    : field_(std::move(field)), coefficient_(field_.normalize(coefficient)) {}

bool MontgomeryCurve::is_singular() const {
    return field_.square(coefficient_) == 4;
}

mpz_class MontgomeryCurve::j_invariant() const {
    if (is_singular()) {
        throw std::domain_error("singular Montgomery curve has no j-invariant");
    }
    const mpz_class a2 = field_.square(coefficient_);
    const mpz_class numerator = field_.mul(
        256, field_.mul(field_.sub(a2, 3), field_.square(field_.sub(a2, 3))));
    return field_.divide(numerator, field_.sub(a2, 4));
}

Curve MontgomeryCurve::short_weierstrass() const {
    if (is_singular()) {
        throw std::domain_error("cannot convert singular Montgomery curve");
    }
    const mpz_class a2 = field_.square(coefficient_);
    const mpz_class a3 = field_.mul(a2, coefficient_);
    const mpz_class short_a = field_.sub(1, field_.divide(a2, 3));
    const mpz_class short_b = field_.sub(field_.divide(field_.mul(2, a3), 27),
                                         field_.divide(coefficient_, 3));
    return Curve(field_, short_a, short_b);
}

mpz_class MontgomeryCurve::short_x(const mpz_class& montgomery_x_value) const {
    return field_.add(montgomery_x_value, field_.divide(coefficient_, 3));
}

mpz_class MontgomeryCurve::montgomery_x(const mpz_class& short_x_value) const {
    return field_.sub(short_x_value, field_.divide(coefficient_, 3));
}

Curve deterministic_curve(const mpz_class& prime, std::uint64_t seed, std::uint64_t index) {
    Field field(prime);
    const mpz_class a = deterministic_residue(field, seed, index, UINT64_C(0xa11ce001));
    const mpz_class b = deterministic_residue(field, seed, index, UINT64_C(0xb00b0002));
    return Curve(std::move(field), a, b);
}

MontgomeryCurve deterministic_montgomery_curve(const mpz_class& prime, std::uint64_t seed,
                                                std::uint64_t index) {
    Field field(prime);
    const mpz_class coefficient = deterministic_residue(
        field, seed, index, UINT64_C(0x4d4f4e54474f4d45));
    return MontgomeryCurve(std::move(field), coefficient);
}

mpz_class count_points_bruteforce(const Curve& curve, unsigned long limit) {
    if (curve.is_singular()) {
        throw std::invalid_argument("cannot count points on a singular curve");
    }
    if (!mpz_fits_ulong_p(curve.field().modulus().get_mpz_t())) {
        throw std::invalid_argument("brute-force modulus does not fit unsigned long");
    }
    const unsigned long p = curve.field().modulus().get_ui();
    if (p > limit) {
        throw std::invalid_argument("brute-force point-count limit exceeded");
    }
    mpz_class count = 1;
    for (unsigned long x = 0; x < p; ++x) {
        const mpz_class xx = x;
        const mpz_class rhs = curve.field().add(
            curve.field().add(curve.field().mul(curve.field().square(xx), xx),
                              curve.field().mul(curve.a(), xx)),
            curve.b());
        count += 1 + curve.field().legendre(rhs);
    }
    return count;
}

}  // namespace oneshotsea
