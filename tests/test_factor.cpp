#include "oneshotsea/factor.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using oneshotsea::Field;
using oneshotsea::IrreducibleFactor;
using oneshotsea::Poly;

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool poly_less(const Poly& lhs, const Poly& rhs) {
    if (lhs.degree() != rhs.degree()) {
        return lhs.degree() < rhs.degree();
    }
    return std::lexicographical_compare(
        lhs.coefficients().begin(), lhs.coefficients().end(),
        rhs.coefficients().begin(), rhs.coefficients().end());
}

std::uint64_t integer_power(std::uint64_t base, unsigned int exponent) {
    std::uint64_t result = 1;
    for (unsigned int i = 0; i < exponent; ++i) {
        result *= base;
    }
    return result;
}

Poly monic_from_code(const Field& field, unsigned int degree,
                     std::uint64_t code) {
    const std::uint64_t characteristic = field.modulus().get_ui();
    std::vector<mpz_class> coefficients(
        static_cast<std::size_t>(degree) + 1U, 0);
    for (unsigned int index = 0; index < degree; ++index) {
        coefficients[index] =
            static_cast<unsigned long>(code % characteristic);
        code /= characteristic;
    }
    coefficients[degree] = 1;
    return Poly(field, std::move(coefficients));
}

std::vector<Poly> brute_irreducibles(const Field& field,
                                     unsigned int maximum_degree) {
    const std::uint64_t characteristic = field.modulus().get_ui();
    std::vector<Poly> irreducibles;
    for (unsigned int degree = 1; degree <= maximum_degree; ++degree) {
        const std::uint64_t count = integer_power(characteristic, degree);
        for (std::uint64_t code = 0; code < count; ++code) {
            const Poly candidate = monic_from_code(field, degree, code);
            bool reducible = false;
            for (const Poly& divisor : irreducibles) {
                if (2 * divisor.degree() > candidate.degree()) {
                    continue;
                }
                if (oneshotsea::mod(candidate, divisor).is_zero()) {
                    reducible = true;
                    break;
                }
            }
            if (!reducible) {
                irreducibles.push_back(candidate);
            }
        }
    }
    std::sort(irreducibles.begin(), irreducibles.end(), poly_less);
    return irreducibles;
}

std::vector<IrreducibleFactor> brute_factor(
    const Poly& polynomial, const std::vector<Poly>& irreducibles) {
    Poly remaining = polynomial.monic();
    std::vector<IrreducibleFactor> output;
    for (const Poly& irreducible : irreducibles) {
        if (irreducible.degree() > remaining.degree()) {
            break;
        }
        unsigned long multiplicity = 0;
        for (;;) {
            const auto [quotient, remainder] =
                oneshotsea::divmod(remaining, irreducible);
            if (!remainder.is_zero()) {
                break;
            }
            remaining = quotient;
            ++multiplicity;
            if (remaining.is_one()) {
                break;
            }
        }
        if (multiplicity != 0) {
            output.push_back({irreducible, multiplicity});
        }
        if (remaining.is_one()) {
            break;
        }
    }
    check(remaining.is_one(), "independent brute factorization was incomplete");
    return output;
}

Poly reconstruct(const Field& field,
                 const std::vector<IrreducibleFactor>& factors) {
    Poly result = Poly::constant(field, 1);
    for (const IrreducibleFactor& factor : factors) {
        for (unsigned long copy = 0; copy < factor.multiplicity; ++copy) {
            result = oneshotsea::mul(result, factor.polynomial);
        }
    }
    return result;
}

std::vector<mpz_class> brute_roots(const Poly& polynomial) {
    const unsigned long characteristic = polynomial.field().modulus().get_ui();
    std::vector<mpz_class> roots;
    for (unsigned long value = 0; value < characteristic; ++value) {
        if (polynomial.evaluate(value) == 0) {
            roots.emplace_back(value);
        }
    }
    return roots;
}

std::vector<mpz_class> roots_from_factors(
    const std::vector<IrreducibleFactor>& factors) {
    std::vector<mpz_class> roots;
    for (const IrreducibleFactor& factor : factors) {
        if (factor.polynomial.degree() == 1) {
            roots.push_back(factor.polynomial.field().neg(
                factor.polynomial.coefficient(0)));
        }
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}

void compare_factorizations(const Poly& polynomial,
                            const std::vector<IrreducibleFactor>& actual,
                            const std::vector<IrreducibleFactor>& expected,
                            const std::string& context) {
    check(actual.size() == expected.size(), context + ": factor count");
    for (std::size_t index = 0; index < actual.size(); ++index) {
        check(oneshotsea::equal(actual[index].polynomial,
                                expected[index].polynomial),
              context + ": exact irreducible factor " + std::to_string(index));
        check(actual[index].multiplicity == expected[index].multiplicity,
              context + ": multiplicity " + std::to_string(index));
        check(actual[index].polynomial.leading_coefficient() == 1,
              context + ": monic factor");
        if (index != 0U) {
            check(poly_less(actual[index - 1U].polynomial,
                            actual[index].polynomial),
                  context + ": deterministic strict ordering");
        }
    }
    check(oneshotsea::equal(reconstruct(polynomial.field(), actual),
                            polynomial.monic()),
          context + ": exact reconstruction");
    int weighted_degree = 0;
    for (const IrreducibleFactor& factor : actual) {
        weighted_degree += factor.polynomial.degree() *
                           static_cast<int>(factor.multiplicity);
    }
    check(weighted_degree == polynomial.degree(),
          context + ": weighted factor degrees");
}

std::optional<unsigned int> expected_uniform_degree(
    const std::vector<IrreducibleFactor>& factors) {
    if (factors.empty() ||
        std::any_of(factors.begin(), factors.end(),
                    [](const IrreducibleFactor& factor) {
                        return factor.multiplicity != 1UL;
                    })) {
        return std::nullopt;
    }
    const unsigned int degree = static_cast<unsigned int>(
        factors.front().polynomial.degree());
    if (std::any_of(
            factors.begin(), factors.end(),
            [degree](const IrreducibleFactor& factor) {
                return factor.polynomial.degree() !=
                    static_cast<int>(degree);
            })) {
        return std::nullopt;
    }
    return degree;
}

void test_exhaustive_tiny_field(unsigned long characteristic,
                                unsigned int maximum_degree) {
    const Field field(characteristic);
    const auto irreducibles = brute_irreducibles(field, maximum_degree);
    std::uint64_t checked = 0;
    for (unsigned int degree = 1; degree <= maximum_degree; ++degree) {
        const std::uint64_t count = integer_power(characteristic, degree);
        for (std::uint64_t code = 0; code < count; ++code) {
            const Poly polynomial = monic_from_code(field, degree, code);
            const auto expected = brute_factor(polynomial, irreducibles);
            const auto actual = oneshotsea::factor_polynomial(polynomial);
            const std::string context =
                "F_" + std::to_string(characteristic) + " degree " +
                std::to_string(degree) + " code " + std::to_string(code);
            compare_factorizations(polynomial, actual, expected, context);
            check(oneshotsea::uniform_irreducible_factor_degree(polynomial) ==
                      expected_uniform_degree(expected),
                  context + ": certified uniform factor degree");
            check(roots_from_factors(actual) == brute_roots(polynomial),
                  context + ": brute-force roots");
            ++checked;
        }
    }
    check(checked ==
              (characteristic == 3 ? UINT64_C(1092) : UINT64_C(780)),
          "expected exhaustive tiny-field case count");
}

std::vector<Poly> large_irreducible_quadratics(const Field& field,
                                               std::size_t count) {
    std::vector<Poly> output;
    for (unsigned long constant = 1; output.size() < count; ++constant) {
        const mpz_class discriminant = field.sub(1, field.mul(4, constant));
        if (field.legendre(discriminant) == -1) {
            output.emplace_back(field,
                                std::vector<mpz_class>{constant, 1, 1});
        }
    }
    return output;
}

std::vector<IrreducibleFactor> sorted_factors(
    std::vector<IrreducibleFactor> factors) {
    std::sort(factors.begin(), factors.end(),
              [](const IrreducibleFactor& lhs,
                 const IrreducibleFactor& rhs) {
                  return poly_less(lhs.polynomial, rhs.polynomial);
              });
    return factors;
}

void test_416_bit_constructed_products() {
    const Field field(oneshotsea::parse_integer(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237"));
    check(mpz_sizeinbase(field.modulus().get_mpz_t(), 2) == 416,
          "target field bit length");
    check(mpz_probab_prime_p(field.modulus().get_mpz_t(), 25) != 0,
          "target field is probable prime");

    const Poly x = Poly::x(field);
    const Poly linear_two = oneshotsea::sub(x, Poly::constant(field, 2));
    const Poly linear_three = oneshotsea::sub(x, Poly::constant(field, 3));
    const auto quadratics = large_irreducible_quadratics(field, 3);

    auto square_free_expected = sorted_factors({
        {linear_two, 1},
        {linear_three, 1},
        {quadratics[0], 1},
        {quadratics[1], 1},
        {quadratics[2], 1},
    });
    Poly square_free_product = reconstruct(field, square_free_expected);
    square_free_product = oneshotsea::scalar_mul(square_free_product, 17);
    const auto square_free_actual =
        oneshotsea::factor_polynomial(square_free_product);
    compare_factorizations(square_free_product, square_free_actual,
                           square_free_expected,
                           "416-bit square-free equal-degree product");
    check(!oneshotsea::uniform_irreducible_factor_degree(
               square_free_product).has_value(),
          "416-bit mixed factor degrees are not uniform");

    const Poly quadratic_product = oneshotsea::mul(
        quadratics[0], oneshotsea::mul(quadratics[1], quadratics[2]));
    check(oneshotsea::uniform_irreducible_factor_degree(quadratic_product) ==
              std::optional<unsigned int>(2U),
          "416-bit distinct quadratics have certified uniform degree two");
    check(oneshotsea::uniform_irreducible_factor_degree(
              oneshotsea::mul(linear_two, linear_three)) ==
              std::optional<unsigned int>(1U),
          "416-bit distinct linear factors have certified uniform degree one");

    auto repeated_expected = sorted_factors({
        {linear_two, 4},
        {quadratics[0], 2},
        {quadratics[1], 3},
    });
    const Poly repeated_product = reconstruct(field, repeated_expected);
    const auto repeated_actual =
        oneshotsea::factor_polynomial(repeated_product);
    compare_factorizations(repeated_product, repeated_actual, repeated_expected,
                           "416-bit repeated-factor product");
    check(!oneshotsea::uniform_irreducible_factor_degree(
               repeated_product).has_value(),
          "416-bit repeated factors are not certified as square-free uniform");

    const auto repeated_again =
        oneshotsea::factor_polynomial(repeated_product);
    compare_factorizations(repeated_product, repeated_again, repeated_actual,
                           "416-bit deterministic repeat");
}

void test_input_contract() {
    const Field field(101);
    check(oneshotsea::factor_polynomial(Poly::constant(field, 37)).empty(),
          "nonzero constant has empty factorization");
    check(!oneshotsea::uniform_irreducible_factor_degree(
               Poly::constant(field, 37)).has_value(),
          "nonzero constant has no uniform factor degree");

    bool zero_rejected = false;
    try {
        static_cast<void>(oneshotsea::factor_polynomial(Poly(field)));
    } catch (const std::invalid_argument&) {
        zero_rejected = true;
    }
    check(zero_rejected, "zero polynomial rejected");

    bool uniform_zero_rejected = false;
    try {
        static_cast<void>(oneshotsea::uniform_irreducible_factor_degree(
            Poly(field)));
    } catch (const std::invalid_argument&) {
        uniform_zero_rejected = true;
    }
    check(uniform_zero_rejected,
          "zero polynomial uniform-degree query rejected");

    bool composite_rejected = false;
    try {
        static_cast<void>(oneshotsea::factor_polynomial(
            Poly(Field(15), {1, 0, 1})));
    } catch (const std::invalid_argument&) {
        composite_rejected = true;
    }
    check(composite_rejected, "composite field modulus rejected");

    bool uniform_composite_rejected = false;
    try {
        static_cast<void>(oneshotsea::uniform_irreducible_factor_degree(
            Poly(Field(15), {1, 0, 1})));
    } catch (const std::invalid_argument&) {
        uniform_composite_rejected = true;
    }
    check(uniform_composite_rejected,
          "uniform factor degree rejects a composite field modulus");
}

}  // namespace

int main() {
    try {
        test_input_contract();
        test_exhaustive_tiny_field(3, 6);
        test_exhaustive_tiny_field(5, 4);
        test_416_bit_constructed_products();
        std::cout << "factorization tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "factorization test failure: " << error.what() << '\n';
        return 1;
    }
}
