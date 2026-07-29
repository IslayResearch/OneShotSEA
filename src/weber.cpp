#include "oneshotsea/weber.hpp"

#include <stdexcept>

namespace oneshotsea {
namespace {

Poly polynomial_power(Poly base, unsigned exponent) {
    Poly result = Poly::constant(base.field(), 1);
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = mul(result, base);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = mul(base, base);
        }
    }
    return result;
}

mpz_class weber_f_24(const Field& field, const mpz_class& weber_f) {
    return field.pow(field.normalize(weber_f), 24);
}

}  // namespace

mpz_class j_from_weber_f(const Field& field, const mpz_class& weber_f) {
    const mpz_class power = weber_f_24(field, weber_f);
    if (power == 0) {
        throw std::domain_error("a Weber-f lift cannot be zero");
    }
    return field.divide(
        field.mul(field.sub(power, 16),
                  field.square(field.sub(power, 16))),
        power);
}

mpz_class j_derivative_from_weber_f(const Field& field,
                                    const mpz_class& weber_f) {
    const mpz_class normalized = field.normalize(weber_f);
    if (normalized == 0) {
        throw std::domain_error("a Weber-f lift cannot be zero");
    }
    const mpz_class power = weber_f_24(field, normalized);
    const mpz_class j = j_from_weber_f(field, normalized);
    return field.divide(
        field.mul(24, field.sub(field.mul(3, field.square(field.sub(power, 16))),
                                j)),
        normalized);
}

std::vector<mpz_class> weber_f_lifts(const Field& field, const mpz_class& j) {
    const Poly x = Poly::x(field);
    const Poly x_24 = polynomial_power(x, 24);
    const Poly shifted = sub(x_24, Poly::constant(field, 16));
    const Poly relation = sub(
        mul(mul(shifted, shifted), shifted),
        scalar_mul(x_24, field.normalize(j)));
    return linear_roots(relation);
}

}  // namespace oneshotsea
