#include "oneshotsea/poly.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

mpz_class target_prime() {
    return oneshotsea::parse_integer(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237");
}

oneshotsea::Poly random_polynomial(const oneshotsea::Field& field,
                                   std::size_t coefficient_count,
                                   std::uint64_t domain) {
    std::vector<mpz_class> coefficients(coefficient_count);
    for (std::size_t index = 0; index < coefficient_count; ++index) {
        coefficients[index] = oneshotsea::deterministic_residue(
            field, UINT64_C(0xd1076a0d3e894517),
            static_cast<std::uint64_t>(index),
            domain ^ static_cast<std::uint64_t>(coefficient_count));
    }
    if (!coefficients.empty() && coefficients.back() == 0) {
        coefficients.back() = 1;
    }
    return oneshotsea::Poly(field, std::move(coefficients));
}

oneshotsea::Poly modulus_of_degree(const oneshotsea::Field& field,
                                   std::size_t degree,
                                   std::uint64_t domain, bool monic) {
    oneshotsea::Poly result = random_polynomial(field, degree + 1U, domain);
    std::vector<mpz_class> coefficients = result.coefficients();
    coefficients.back() = monic ? mpz_class(1) : mpz_class(7);
    return oneshotsea::Poly(field, std::move(coefficients));
}

void check_square(const oneshotsea::Poly& value,
                  const oneshotsea::Poly& modulus,
                  const std::string& label) {
    const oneshotsea::Poly specialized = oneshotsea::squaremod(value, modulus);
    const oneshotsea::Poly generic_modular =
        oneshotsea::mulmod(value, value, modulus);
    const oneshotsea::Poly generic_full =
        oneshotsea::mod(oneshotsea::mul(value, value), modulus);
    check(oneshotsea::equal(specialized, generic_modular),
          label + ": specialized != generic mulmod");
    check(oneshotsea::equal(specialized, generic_full),
          label + ": specialized != mod(generic mul)");

    const oneshotsea::PolyModContext context(modulus);
    check(oneshotsea::equal(context.square(value), generic_full),
          label + ": prepared square != mod(generic mul)");
    check(oneshotsea::equal(context.multiply(value, value), generic_full),
          label + ": prepared multiply != mod(generic mul)");
    check(oneshotsea::equal(context.reduce(value),
                            oneshotsea::mod(value, modulus)),
          label + ": prepared reduction != generic mod");
}

void run() {
    const oneshotsea::Field field(target_prime());
    std::uint64_t domain = 1;
    for (const std::size_t degree :
         {1U, 2U, 30U, 31U, 32U, 33U, 47U, 48U, 49U, 63U, 64U, 65U,
          79U, 80U, 81U, 89U, 90U, 91U, 95U, 96U, 97U, 128U, 129U,
          130U, 193U, 194U, 195U, 281U, 401U, 402U}) {
        for (const bool monic : {true, false}) {
            const oneshotsea::Poly modulus =
                modulus_of_degree(field, degree, domain++, monic);
            const oneshotsea::Poly value =
                random_polynomial(field, degree, domain++);
            check_square(value, modulus,
                         "threshold/production degree " +
                             std::to_string(degree));
        }
    }

    for (const std::size_t degree : {32U, 64U, 129U, 194U, 281U, 401U}) {
        const oneshotsea::Poly modulus =
            modulus_of_degree(field, degree, domain++, false);
        const oneshotsea::Poly high =
            random_polynomial(field, 2U * degree + 8U, domain++);
        check_square(high, modulus,
                     "high-degree pre-reduction " + std::to_string(degree));
    }

    for (const std::size_t factor_degree : {16U, 32U, 65U, 97U}) {
        const oneshotsea::Poly factor =
            modulus_of_degree(field, factor_degree, domain++, true);
        const oneshotsea::Poly repeated = oneshotsea::scalar_mul(
            oneshotsea::mul(factor, factor), 7);
        const oneshotsea::Poly value = random_polynomial(
            field, static_cast<std::size_t>(repeated.degree()), domain++);
        check_square(value, repeated,
                     "nonmonic repeated factor " +
                         std::to_string(repeated.degree()));
    }

    const oneshotsea::Poly modulus =
        modulus_of_degree(field, 194U, domain++, false);
    std::vector<mpz_class> adversarial(194U);
    for (std::size_t index = 0; index < adversarial.size(); ++index) {
        switch (index % 4U) {
            case 0U:
                adversarial[index] = 0;
                break;
            case 1U:
                adversarial[index] = 1;
                break;
            case 2U:
                adversarial[index] = field.modulus() - 1;
                break;
            default:
                adversarial[index] = field.modulus() / 2;
                break;
        }
    }
    check_square(oneshotsea::Poly(field, adversarial), modulus,
                 "zero/max/signed-recombination coefficients");

    check_square(oneshotsea::Poly(field), modulus, "zero value");
    check_square(modulus, modulus, "value aliases modulus object");
    check_square(random_polynomial(field, 64U, domain++),
                 oneshotsea::Poly::constant(field, 7),
                 "constant modulus");
}

}  // namespace

int main() {
    try {
        run();
        std::cout << "polynomial square differential: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "polynomial square differential failure: "
                  << error.what() << '\n';
        return 1;
    }
}
