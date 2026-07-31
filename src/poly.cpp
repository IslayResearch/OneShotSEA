#include "oneshotsea/poly.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace oneshotsea {
namespace {

void require_same_field(const Poly& lhs, const Poly& rhs) {
    if (lhs.field().modulus() != rhs.field().modulus()) {
        throw std::invalid_argument("polynomials belong to different fields");
    }
}

void require_probable_prime_field(const Poly& polynomial) {
    if (mpz_probab_prime_p(polynomial.field().modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("polynomial root extraction requires probable-prime p");
    }
}

void split_linear_factors(const Poly& polynomial, std::vector<mpz_class>& roots) {
    if (polynomial.degree() <= 0) {
        return;
    }
    const Field& field = polynomial.field();
    if (polynomial.degree() == 1) {
        roots.push_back(field.divide(field.neg(polynomial.coefficient(0)),
                                     polynomial.coefficient(1)));
        return;
    }

    const Poly x = Poly::x(field);
    const mpz_class exponent = (field.modulus() - 1) / 2;
    constexpr std::uint64_t kSplitAttempts = 256;
    for (std::uint64_t attempt = 0; attempt < kSplitAttempts; ++attempt) {
        const mpz_class offset = deterministic_residue(
            field, UINT64_C(0x726f6f7473706c74), attempt,
            static_cast<std::uint64_t>(polynomial.degree()));
        const Poly character = powmod(
            add(x, Poly::constant(field, offset)), exponent, polynomial);
        const Poly candidates[] = {
            gcd(polynomial, character),
            gcd(polynomial, sub(character, Poly::constant(field, 1))),
            gcd(polynomial, add(character, Poly::constant(field, 1))),
        };
        for (const Poly& factor : candidates) {
            if (factor.degree() <= 0 || factor.degree() >= polynomial.degree()) {
                continue;
            }
            const auto [cofactor, remainder] = divmod(polynomial, factor);
            if (!remainder.is_zero()) {
                throw std::logic_error("root splitter produced a non-factor");
            }
            split_linear_factors(factor.monic(), roots);
            split_linear_factors(cofactor.monic(), roots);
            return;
        }
    }
    throw std::runtime_error("deterministic linear-root splitting attempt limit reached");
}

}  // namespace

Poly::Poly(const Field& field) : field_(field) {}

Poly::Poly(const Field& field, std::vector<mpz_class> coefficients)
    : field_(field), coefficients_(std::move(coefficients)) {
    for (auto& coefficient : coefficients_) {
        coefficient = field_.normalize(coefficient);
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
        result = field_.add(field_.mul(result, value), *it);
    }
    return result;
}

Poly Poly::derivative() const {
    if (degree() <= 0) {
        return Poly(field_);
    }
    std::vector<mpz_class> output(coefficients_.size() - 1U);
    for (std::size_t i = 1; i < coefficients_.size(); ++i) {
        output[i - 1U] = field_.mul(coefficients_[i], mpz_class(i));
    }
    return Poly(field_, std::move(output));
}

Poly Poly::monic() const {
    if (is_zero()) {
        return *this;
    }
    return scalar_mul(*this, field_.inverse(leading_coefficient()));
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
            mpz_addmul(output[i + j].get_mpz_t(),
                       lhs.coefficient(i).get_mpz_t(),
                       rhs.coefficient(j).get_mpz_t());
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
    if (numerator.degree() < denominator.degree()) {
        return {Poly(numerator.field()), numerator};
    }
    const Field& field = numerator.field();
    std::vector<mpz_class> remainder = numerator.coefficients();
    std::vector<mpz_class> quotient(
        static_cast<std::size_t>(numerator.degree() - denominator.degree() + 1), 0);
    const std::size_t denominator_degree =
        static_cast<std::size_t>(denominator.degree());
    const mpz_class inverse_lead =
        field.inverse(denominator.leading_coefficient());
    while (remainder.size() > denominator_degree) {
        const std::size_t shift = remainder.size() - denominator_degree - 1U;
        const mpz_class factor = field.mul(remainder.back(), inverse_lead);
        quotient[shift] = factor;
        for (std::size_t index = 0; index <= denominator_degree; ++index) {
            remainder[shift + index] = field.sub(
                remainder[shift + index],
                field.mul(factor, denominator.coefficient(index)));
        }
        while (!remainder.empty() && remainder.back() == 0) {
            remainder.pop_back();
        }
    }
    return {Poly(field, std::move(quotient)),
            Poly(field, std::move(remainder))};
}

Poly mod(const Poly& numerator, const Poly& denominator) {
    return divmod(numerator, denominator).second;
}

namespace {

void reduce_product_coefficients(std::vector<mpz_class>& coefficients,
                                 const Poly& modulus) {
    const Field& field = modulus.field();
    const std::size_t modulus_degree =
        static_cast<std::size_t>(modulus.degree());
    const mpz_class inverse_lead =
        modulus.leading_coefficient() == 1
            ? mpz_class(1)
            : field.inverse(modulus.leading_coefficient());
    for (std::size_t degree = coefficients.size(); degree-- > modulus_degree;) {
        if (coefficients[degree] == 0) {
            continue;
        }
        const mpz_class factor = field.mul(coefficients[degree], inverse_lead);
        const std::size_t shift = degree - modulus_degree;
        for (std::size_t index = 0; index < modulus_degree; ++index) {
            coefficients[shift + index] = field.sub(
                coefficients[shift + index],
                field.mul(factor, modulus.coefficient(index)));
        }
        coefficients[degree] = 0;
    }
    coefficients.resize(modulus_degree);
}

const Poly& reduced_operand(const Poly& value, const Poly& modulus,
                            Poly& storage) {
    if (value.degree() >= modulus.degree()) {
        storage = mod(value, modulus);
        return storage;
    }
    return value;
}

}  // namespace

Poly mulmod(const Poly& lhs, const Poly& rhs, const Poly& modulus) {
    require_same_field(lhs, rhs);
    require_same_field(lhs, modulus);
    if (modulus.is_zero()) {
        throw std::domain_error("polynomial modulus is zero");
    }
    if (modulus.degree() == 0 || lhs.is_zero() || rhs.is_zero()) {
        return Poly(lhs.field());
    }
    Poly lhs_storage(lhs.field());
    Poly rhs_storage(lhs.field());
    const Poly& reduced_lhs = reduced_operand(lhs, modulus, lhs_storage);
    const Poly& reduced_rhs = reduced_operand(rhs, modulus, rhs_storage);
    if (reduced_lhs.is_zero() || reduced_rhs.is_zero()) {
        return Poly(lhs.field());
    }
    std::vector<mpz_class> output(
        reduced_lhs.coefficients().size() +
            reduced_rhs.coefficients().size() - 1U,
        0);
    for (std::size_t lhs_index = 0;
         lhs_index < reduced_lhs.coefficients().size(); ++lhs_index) {
        for (std::size_t rhs_index = 0;
             rhs_index < reduced_rhs.coefficients().size(); ++rhs_index) {
            mpz_addmul(output[lhs_index + rhs_index].get_mpz_t(),
                       reduced_lhs.coefficient(lhs_index).get_mpz_t(),
                       reduced_rhs.coefficient(rhs_index).get_mpz_t());
        }
    }
    for (mpz_class& coefficient : output) {
        coefficient = lhs.field().normalize(coefficient);
    }
    reduce_product_coefficients(output, modulus);
    return Poly(lhs.field(), std::move(output));
}

Poly squaremod(const Poly& value, const Poly& modulus) {
    require_same_field(value, modulus);
    if (modulus.is_zero()) {
        throw std::domain_error("polynomial modulus is zero");
    }
    if (modulus.degree() == 0 || value.is_zero()) {
        return Poly(value.field());
    }
    Poly storage(value.field());
    const Poly& reduced = reduced_operand(value, modulus, storage);
    if (reduced.is_zero()) {
        return Poly(value.field());
    }
    std::vector<mpz_class> output(
        2U * reduced.coefficients().size() - 1U, 0);
    for (std::size_t lhs_index = 0;
         lhs_index < reduced.coefficients().size(); ++lhs_index) {
        mpz_addmul(output[2U * lhs_index].get_mpz_t(),
                   reduced.coefficient(lhs_index).get_mpz_t(),
                   reduced.coefficient(lhs_index).get_mpz_t());
        for (std::size_t rhs_index = lhs_index + 1U;
             rhs_index < reduced.coefficients().size(); ++rhs_index) {
            mpz_addmul(output[lhs_index + rhs_index].get_mpz_t(),
                       reduced.coefficient(lhs_index).get_mpz_t(),
                       reduced.coefficient(rhs_index).get_mpz_t());
            mpz_addmul(output[lhs_index + rhs_index].get_mpz_t(),
                       reduced.coefficient(lhs_index).get_mpz_t(),
                       reduced.coefficient(rhs_index).get_mpz_t());
        }
    }
    for (mpz_class& coefficient : output) {
        coefficient = value.field().normalize(coefficient);
    }
    reduce_product_coefficients(output, modulus);
    return Poly(value.field(), std::move(output));
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
            result = mulmod(result, base, modulus);
        }
        exponent >>= 1;
        if (exponent > 0) {
            base = squaremod(base, modulus);
        }
    }
    return result;
}

int rational_root_count(const Poly& polynomial) {
    if (polynomial.is_zero()) {
        throw std::invalid_argument("zero polynomial has every field element as a root");
    }
    require_probable_prime_field(polynomial);
    const Poly xp = powmod(Poly::x(polynomial.field()), polynomial.field().modulus(), polynomial);
    const Poly split = gcd(polynomial, sub(xp, Poly::x(polynomial.field())));
    return split.degree();
}

std::vector<mpz_class> linear_roots(const Poly& polynomial) {
    if (polynomial.is_zero()) {
        throw std::invalid_argument("zero polynomial has every field element as a root");
    }
    require_probable_prime_field(polynomial);
    const Field& field = polynomial.field();
    const Poly x = Poly::x(field);
    const Poly xp = powmod(x, field.modulus(), polynomial);
    const Poly split = gcd(polynomial, sub(xp, x));
    std::vector<mpz_class> roots;
    roots.reserve(static_cast<std::size_t>(std::max(0, split.degree())));
    split_linear_factors(split, roots);
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());

    Poly reconstructed = Poly::constant(field, 1);
    for (const mpz_class& root : roots) {
        if (polynomial.evaluate(root) != 0) {
            throw std::logic_error("linear-root validation failed");
        }
        reconstructed = mul(reconstructed,
                            sub(x, Poly::constant(field, root)));
    }
    if (!equal(reconstructed.monic(), split.monic())) {
        throw std::logic_error("linear-root reconstruction failed");
    }
    return roots;
}

bool equal(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    return lhs.coefficients() == rhs.coefficients();
}

}  // namespace oneshotsea
