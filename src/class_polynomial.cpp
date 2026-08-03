#include "oneshotsea/class_polynomial.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oneshotsea {
namespace {

using PolynomialMatrix = std::vector<std::vector<Poly>>;

void validate_three_power_witness(
    const SutherlandSuitableOrder& order,
    const SutherlandCrtPrime& witness) {
    std::uint64_t encoded_prime = 0U;
    if (!export_u64(witness.prime, encoded_prime) ||
        !is_prime_u64(encoded_prime)) {
        throw std::invalid_argument(
            "class-polynomial producer requires a proven 64-bit prime");
    }
    const mpz_class ell(static_cast<unsigned long>(order.level()));
    if (witness.trace <= 0 || witness.volcano_parameter <= 0 ||
        4 * witness.prime !=
            witness.trace * witness.trace - ell * ell *
                witness.volcano_parameter * witness.volcano_parameter *
                order.discriminant() ||
        mpz_fdiv_ui(witness.prime.get_mpz_t(), order.level()) != 1U ||
        mpz_fdiv_ui(witness.trace.get_mpz_t(), order.level()) != 2U ||
        mpz_divisible_ui_p(witness.volcano_parameter.get_mpz_t(),
                           order.level()) != 0 ||
        mpz_divisible_p(order.discriminant().get_mpz_t(),
                        witness.prime.get_mpz_t()) != 0) {
        throw std::invalid_argument(
            "class-polynomial producer received an invalid witness");
    }
}

unsigned three_power_exponent(const mpz_class& conductor) {
    if (conductor < 1 || !mpz_fits_ulong_p(conductor.get_mpz_t())) {
        throw std::invalid_argument(
            "three-power class-polynomial conductor is outside range");
    }
    unsigned long remaining = conductor.get_ui();
    unsigned exponent = 0U;
    while (remaining > 1UL && remaining % 3UL == 0UL) {
        remaining /= 3UL;
        ++exponent;
    }
    if (remaining != 1UL) {
        throw std::invalid_argument(
            "class-polynomial conductor is not a power of three");
    }
    return exponent;
}

std::uint64_t three_power_class_number(unsigned exponent) {
    if (exponent == 0U) {
        return 1U;
    }
    std::uint64_t result = 4U;
    for (unsigned index = 1U; index < exponent; ++index) {
        if (result > std::numeric_limits<std::uint64_t>::max() / 3U) {
            throw std::overflow_error("three-power class number overflows");
        }
        result *= 3U;
    }
    return result;
}

Poly exact_divide(const Poly& numerator, const Poly& denominator,
                  const char* context) {
    if (denominator.is_zero()) {
        throw std::logic_error(std::string(context) +
                               " has a zero exact divisor");
    }
    auto quotient_and_remainder = divmod(numerator, denominator);
    if (!quotient_and_remainder.second.is_zero()) {
        throw std::logic_error(std::string(context) +
                               " is not exactly divisible");
    }
    return std::move(quotient_and_remainder.first);
}

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

// Coefficients in ascending X degree, each a polynomial in Y.  This is the
// complete exact classical Phi_3, kept in code so the authenticated producer
// does not depend on a mutable data-file path.
std::array<Poly, 5U> fixed_classical_phi3(const Field& field) {
    return {
        Poly(field, {
            0,
            mpz_class("1855425871872000000000"),
            mpz_class("452984832000000"),
            mpz_class("36864000"), 1}),
        Poly(field, {
            mpz_class("1855425871872000000000"),
            mpz_class("-770845966336000000"),
            mpz_class("8900222976000"),
            mpz_class("-1069956")}),
        Poly(field, {
            mpz_class("452984832000000"),
            mpz_class("8900222976000"),
            mpz_class("2587918086"), mpz_class("2232")}),
        Poly(field, {mpz_class("36864000"), mpz_class("-1069956"),
                     mpz_class("2232"), -1}),
        Poly::constant(field, 1)};
}

std::vector<Poly> trim_x_coefficients(std::vector<Poly> coefficients) {
    while (coefficients.size() > 1U && coefficients.back().is_zero()) {
        coefficients.pop_back();
    }
    return coefficients;
}

Poly bareiss_determinant(PolynomialMatrix matrix) {
    if (matrix.empty() || matrix.size() != matrix.front().size()) {
        throw std::invalid_argument(
            "resultant requires a nonempty square Sylvester matrix");
    }
    const std::size_t size = matrix.size();
    for (const auto& row : matrix) {
        if (row.size() != size ||
            row.front().field().modulus() !=
                matrix.front().front().field().modulus()) {
            throw std::invalid_argument(
                "resultant matrix has inconsistent rows or fields");
        }
    }
    if (size == 1U) {
        return matrix.front().front();
    }

    const Field& field = matrix.front().front().field();
    Poly previous_pivot = Poly::constant(field, 1);
    bool negate_result = false;
    for (std::size_t pivot_index = 0U;
         pivot_index + 1U < size; ++pivot_index) {
        std::size_t pivot_row = pivot_index;
        while (pivot_row < size &&
               matrix[pivot_row][pivot_index].is_zero()) {
            ++pivot_row;
        }
        if (pivot_row == size) {
            return Poly(field);
        }
        if (pivot_row != pivot_index) {
            std::swap(matrix[pivot_row], matrix[pivot_index]);
            negate_result = !negate_result;
        }
        const Poly pivot = matrix[pivot_index][pivot_index];
        for (std::size_t row = pivot_index + 1U; row < size; ++row) {
            for (std::size_t column = pivot_index + 1U;
                 column < size; ++column) {
                const Poly numerator = sub(
                    mul(matrix[row][column], pivot),
                    mul(matrix[row][pivot_index],
                        matrix[pivot_index][column]));
                matrix[row][column] = pivot_index == 0U
                    ? numerator
                    : exact_divide(numerator, previous_pivot,
                                   "Bareiss determinant step");
            }
            matrix[row][pivot_index] = Poly(field);
        }
        previous_pivot = pivot;
    }
    Poly result = matrix.back().back();
    return negate_result ? neg(result) : result;
}

Poly resultant_in_x(std::vector<Poly> lhs, std::vector<Poly> rhs) {
    lhs = trim_x_coefficients(std::move(lhs));
    rhs = trim_x_coefficients(std::move(rhs));
    if (lhs.empty() || rhs.empty() || lhs.back().is_zero() ||
        rhs.back().is_zero()) {
        throw std::invalid_argument(
            "resultant received a zero or malformed polynomial");
    }
    const Field& field = lhs.front().field();
    for (const Poly& coefficient : lhs) {
        if (coefficient.field().modulus() != field.modulus()) {
            throw std::invalid_argument("resultant lhs changes fields");
        }
    }
    for (const Poly& coefficient : rhs) {
        if (coefficient.field().modulus() != field.modulus()) {
            throw std::invalid_argument("resultant rhs changes fields");
        }
    }
    const std::size_t lhs_degree = lhs.size() - 1U;
    const std::size_t rhs_degree = rhs.size() - 1U;
    if (lhs_degree == 0U) {
        return polynomial_power(lhs.front(),
                                static_cast<unsigned>(rhs_degree));
    }
    if (rhs_degree == 0U) {
        return polynomial_power(rhs.front(),
                                static_cast<unsigned>(lhs_degree));
    }
    const std::size_t size = lhs_degree + rhs_degree;
    PolynomialMatrix matrix(
        size, std::vector<Poly>(size, Poly(field)));
    for (std::size_t row = 0U; row < rhs_degree; ++row) {
        for (std::size_t degree = 0U; degree <= lhs_degree; ++degree) {
            matrix[row][row + degree] =
                lhs[lhs_degree - degree];
        }
    }
    for (std::size_t row = 0U; row < lhs_degree; ++row) {
        for (std::size_t degree = 0U; degree <= rhs_degree; ++degree) {
            matrix[rhs_degree + row][row + degree] =
                rhs[rhs_degree - degree];
        }
    }
    return bareiss_determinant(std::move(matrix));
}

Poly phi3_resultant(const Poly& class_polynomial) {
    const Field& field = class_polynomial.field();
    const std::array<Poly, 5U> phi3 = fixed_classical_phi3(field);

    // Compute H(X) modulo the monic quartic Phi_3(X,Y) by Horner's
    // algorithm.  This reduces the Sylvester determinant from degree h+4 to
    // at most 7 while preserving the resultant.
    std::array<Poly, 4U> remainder = {
        Poly(field), Poly(field), Poly(field), Poly(field)};
    for (int degree = class_polynomial.degree(); degree >= 0; --degree) {
        const Poly old_high = remainder[3U];
        std::array<Poly, 4U> next = {
            neg(mul(old_high, phi3[0U])),
            sub(remainder[0U], mul(old_high, phi3[1U])),
            sub(remainder[1U], mul(old_high, phi3[2U])),
            sub(remainder[2U], mul(old_high, phi3[3U]))};
        next[0U] = add(
            next[0U],
            Poly::constant(field, class_polynomial.coefficient(
                                      static_cast<std::size_t>(degree))));
        remainder = std::move(next);
    }
    std::vector<Poly> remainder_vector(remainder.begin(), remainder.end());
    return resultant_in_x(
        std::vector<Poly>(phi3.begin(), phi3.end()),
        std::move(remainder_vector));
}

void validate_three_power_order(const SutherlandSuitableOrder& order) {
    if (order.level() <= 3U || order.fundamental_discriminant() != -7) {
        throw std::invalid_argument(
            "class-polynomial order is outside D=-7*3^(2n)");
    }
    const unsigned exponent = three_power_exponent(order.conductor());
    const std::uint64_t expected_class_number =
        three_power_class_number(exponent);
    if (order.class_number() != expected_class_number ||
        order.discriminant() !=
            -7 * order.conductor() * order.conductor()) {
        throw std::invalid_argument(
            "class-polynomial order has inconsistent family invariants");
    }
}

}  // namespace

ClassicalCmClassPolynomial::ClassicalCmClassPolynomial(
    mpz_class discriminant, mpz_class auxiliary_prime, Poly polynomial)
    : discriminant_(std::move(discriminant)),
      auxiliary_prime_(std::move(auxiliary_prime)),
      polynomial_(std::move(polynomial)) {
    if (polynomial_.field().modulus() != auxiliary_prime_ ||
        polynomial_.degree() < 1 ||
        polynomial_.leading_coefficient() != 1) {
        throw std::invalid_argument(
            "authenticated class polynomial has invalid normalization");
    }
}

SutherlandSuitableOrder derive_three_power_suitable_order(unsigned level) {
    if (level <= 3U) {
        throw std::invalid_argument(
            "three-power suitable-order family requires ell>3");
    }
    mpz_class conductor = 1;
    std::uint64_t class_number = 1U;
    while (class_number < static_cast<std::uint64_t>(level) + 2U) {
        conductor *= 3;
        if (class_number == 1U) {
            class_number = 4U;
        } else {
            if (class_number >
                std::numeric_limits<std::uint64_t>::max() / 3U) {
                throw std::overflow_error(
                    "three-power suitable-order class number overflows");
            }
            class_number *= 3U;
        }
    }
    return validate_sutherland_suitable_order(
        level, -7, conductor, 4U, 1U, 16U);
}

ClassicalCmClassPolynomial derive_three_power_class_polynomial_mod_prime(
    const SutherlandSuitableOrder& order,
    const SutherlandCrtPrime& prime_witness) {
    validate_three_power_order(order);
    validate_three_power_witness(order, prime_witness);
    const unsigned target_exponent = three_power_exponent(order.conductor());
    const Field field(prime_witness.prime);

    Poly previous(field);
    Poly current(field, {3375, 1});  // H_-7(X)=X+3375.
    for (unsigned exponent = 0U; exponent < target_exponent; ++exponent) {
        Poly next = phi3_resultant(current);
        if (!previous.is_zero()) {
            if (current.degree() <= 0 || previous.degree() <= 0 ||
                current.degree() % previous.degree() != 0) {
                throw std::logic_error(
                    "ring-class tower has an inconsistent degree ratio");
            }
            const unsigned ascending_multiplicity =
                static_cast<unsigned>(current.degree() / previous.degree());
            const Poly ascending_factor =
                polynomial_power(previous, ascending_multiplicity);
            next = exact_divide(next, ascending_factor,
                                "ring-class tower resultant");
        }
        const std::uint64_t expected_degree =
            three_power_class_number(exponent + 1U);
        if (expected_degree >
                static_cast<std::uint64_t>(
                    std::numeric_limits<int>::max()) ||
            next.degree() != static_cast<int>(expected_degree) ||
            next.leading_coefficient() != 1) {
            throw std::logic_error(
                "ring-class tower produced the wrong monic degree");
        }
        previous = std::move(current);
        current = std::move(next);
    }
    if (current.degree() != static_cast<int>(order.class_number())) {
        throw std::logic_error(
            "class polynomial disagrees with the validated class number");
    }
    return ClassicalCmClassPolynomial(
        order.discriminant(), prime_witness.prime, std::move(current));
}

}  // namespace oneshotsea
