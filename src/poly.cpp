#include "oneshotsea/poly.hpp"

#include <algorithm>
#include <stdexcept>

namespace oneshotsea {
namespace {

void require_same_field(const Poly& lhs, const Poly& rhs) {
    if (lhs.field().modulus() != rhs.field().modulus()) {
        throw std::invalid_argument("polynomials belong to different fields");
    }
}

}  // namespace

Poly::Poly(const Field& field) : field_(&field) {}

Poly::Poly(const Field& field, std::vector<mpz_class> coefficients)
    : field_(&field), coefficients_(std::move(coefficients)) {
    for (auto& coefficient : coefficients_) {
        coefficient = field_->normalize(coefficient);
    }
    trim();
}

Poly Poly::constant(const Field& field, const mpz_class& value) {
    return Poly(field, {value});
}

Poly Poly::x(const Field& field) {
    return Poly(field, {0, 1});
}

int Poly::degree() const {
    return coefficients_.empty() ? -1 : static_cast<int>(coefficients_.size() - 1U);
}

bool Poly::is_zero() const {
    return coefficients_.empty();
}

bool Poly::is_one() const {
    return degree() == 0 && coefficients_[0] == 1;
}

mpz_class Poly::coefficient(std::size_t index) const {
    return index < coefficients_.size() ? coefficients_[index] : mpz_class(0);
}

mpz_class Poly::leading_coefficient() const {
    if (is_zero()) {
        throw std::domain_error("zero polynomial has no leading coefficient");
    }
    return coefficients_.back();
}

mpz_class Poly::evaluate(const mpz_class& value) const {
    mpz_class result = 0;
    for (auto it = coefficients_.rbegin(); it != coefficients_.rend(); ++it) {
        result = field_->add(field_->mul(result, value), *it);
    }
    return result;
}

Poly Poly::derivative() const {
    if (degree() <= 0) {
        return Poly(*field_);
    }
    std::vector<mpz_class> output(coefficients_.size() - 1U);
    for (std::size_t i = 1; i < coefficients_.size(); ++i) {
        output[i - 1U] = field_->mul(coefficients_[i], mpz_class(i));
    }
    return Poly(*field_, std::move(output));
}

Poly Poly::monic() const {
    if (is_zero()) {
        return *this;
    }
    return scalar_mul(*this, field_->inverse(leading_coefficient()));
}

void Poly::trim() {
    while (!coefficients_.empty() && coefficients_.back() == 0) {
        coefficients_.pop_back();
    }
}

Poly add(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    const std::size_t size = std::max(lhs.coefficients().size(), rhs.coefficients().size());
    std::vector<mpz_class> output(size);
    for (std::size_t i = 0; i < size; ++i) {
        output[i] = lhs.field().add(lhs.coefficient(i), rhs.coefficient(i));
    }
    return Poly(lhs.field(), std::move(output));
}

Poly sub(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    const std::size_t size = std::max(lhs.coefficients().size(), rhs.coefficients().size());
    std::vector<mpz_class> output(size);
    for (std::size_t i = 0; i < size; ++i) {
        output[i] = lhs.field().sub(lhs.coefficient(i), rhs.coefficient(i));
    }
    return Poly(lhs.field(), std::move(output));
}

Poly neg(const Poly& value) {
    std::vector<mpz_class> output = value.coefficients();
    for (auto& coefficient : output) {
        coefficient = value.field().neg(coefficient);
    }
    return Poly(value.field(), std::move(output));
}

Poly mul(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    if (lhs.is_zero() || rhs.is_zero()) {
        return Poly(lhs.field());
    }
    const std::size_t size = lhs.coefficients().size() + rhs.coefficients().size() - 1U;
    std::vector<mpz_class> output(size, 0);
    for (std::size_t i = 0; i < lhs.coefficients().size(); ++i) {
        for (std::size_t j = 0; j < rhs.coefficients().size(); ++j) {
            output[i + j] = lhs.field().add(
                output[i + j], lhs.field().mul(lhs.coefficient(i), rhs.coefficient(j)));
        }
    }
    return Poly(lhs.field(), std::move(output));
}

Poly scalar_mul(const Poly& value, const mpz_class& scalar) {
    std::vector<mpz_class> output = value.coefficients();
    for (auto& coefficient : output) {
        coefficient = value.field().mul(coefficient, scalar);
    }
    return Poly(value.field(), std::move(output));
}

std::pair<Poly, Poly> divmod(const Poly& numerator, const Poly& denominator) {
    require_same_field(numerator, denominator);
    if (denominator.is_zero()) {
        throw std::domain_error("polynomial division by zero");
    }
    Poly remainder = numerator;
    if (remainder.degree() < denominator.degree()) {
        return {Poly(numerator.field()), remainder};
    }
    std::vector<mpz_class> quotient(
        static_cast<std::size_t>(remainder.degree() - denominator.degree() + 1), 0);
    const mpz_class inverse_lead = numerator.field().inverse(denominator.leading_coefficient());
    while (!remainder.is_zero() && remainder.degree() >= denominator.degree()) {
        const int shift = remainder.degree() - denominator.degree();
        const mpz_class factor = numerator.field().mul(
            remainder.leading_coefficient(), inverse_lead);
        quotient[static_cast<std::size_t>(shift)] = factor;
        std::vector<mpz_class> term(static_cast<std::size_t>(shift) + 1U, 0);
        term.back() = factor;
        remainder = sub(remainder, mul(denominator, Poly(numerator.field(), std::move(term))));
    }
    return {Poly(numerator.field(), std::move(quotient)), remainder};
}

Poly mod(const Poly& numerator, const Poly& denominator) {
    return divmod(numerator, denominator).second;
}

Poly gcd(Poly lhs, Poly rhs) {
    require_same_field(lhs, rhs);
    while (!rhs.is_zero()) {
        Poly remainder = mod(lhs, rhs);
        lhs = std::move(rhs);
        rhs = std::move(remainder);
    }
    return lhs.monic();
}

Poly powmod(Poly base, mpz_class exponent, const Poly& modulus) {
    require_same_field(base, modulus);
    if (modulus.is_zero()) {
        throw std::domain_error("polynomial modulus is zero");
    }
    if (exponent < 0) {
        throw std::invalid_argument("negative polynomial exponent");
    }
    Poly result = Poly::constant(base.field(), 1);
    base = mod(base, modulus);
    while (exponent > 0) {
        if (mpz_odd_p(exponent.get_mpz_t()) != 0) {
            result = mod(mul(result, base), modulus);
        }
        exponent >>= 1;
        if (exponent > 0) {
            base = mod(mul(base, base), modulus);
        }
    }
    return result;
}

int rational_root_count(const Poly& polynomial) {
    if (polynomial.is_zero()) {
        throw std::invalid_argument("zero polynomial has every field element as a root");
    }
    const Poly xp = powmod(Poly::x(polynomial.field()), polynomial.field().modulus(), polynomial);
    const Poly split = gcd(polynomial, sub(xp, Poly::x(polynomial.field())));
    return split.degree();
}

bool equal(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    return lhs.coefficients() == rhs.coefficients();
}

}  // namespace oneshotsea
