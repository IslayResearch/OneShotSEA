#include "oneshotsea/poly.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using oneshotsea::Field;
using oneshotsea::Poly;

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Poly deterministic_poly(const Field& field, std::size_t coefficient_count,
                        std::uint64_t seed, bool nonzero_lead = true) {
    std::vector<mpz_class> coefficients(coefficient_count, 0);
    for (std::size_t index = 0; index < coefficient_count; ++index) {
        coefficients[index] = oneshotsea::deterministic_residue(
            field, seed, static_cast<std::uint64_t>(index),
            UINT64_C(0x706f6c7974657374));
    }
    if (nonzero_lead && !coefficients.empty() && coefficients.back() == 0) {
        coefficients.back() = 1;
    }
    return Poly(field, std::move(coefficients));
}

void check_modular_products(const Field& field, std::size_t modulus_degree,
                            std::uint64_t seed) {
    std::vector<mpz_class> monic_coefficients(modulus_degree + 1U, 0);
    for (std::size_t index = 0; index < modulus_degree; ++index) {
        monic_coefficients[index] = oneshotsea::deterministic_residue(
            field, seed, static_cast<std::uint64_t>(index),
            UINT64_C(0x6d6f6e69636d6f64));
    }
    monic_coefficients.back() = 1;
    const Poly monic_modulus(field, monic_coefficients);

    std::vector<mpz_class> nonmonic_coefficients = monic_coefficients;
    nonmonic_coefficients.back() = 7;
    if (field.normalize(nonmonic_coefficients.back()) == 0) {
        nonmonic_coefficients.back() = 3;
    }
    const Poly nonmonic_modulus(field, nonmonic_coefficients);

    const Poly lhs = deterministic_poly(
        field, modulus_degree + 5U, seed ^ UINT64_C(0x1111111111111111));
    const Poly rhs = deterministic_poly(
        field, modulus_degree + 3U, seed ^ UINT64_C(0x2222222222222222));

    for (const Poly* modulus : {&monic_modulus, &nonmonic_modulus}) {
        const Poly reference_product = oneshotsea::mod(
            oneshotsea::mul(lhs, rhs), *modulus);
        const Poly optimized_product = oneshotsea::mulmod(lhs, rhs, *modulus);
        check(oneshotsea::equal(reference_product, optimized_product),
              "mulmod disagrees with generic multiply/divide");

        const Poly reference_square = oneshotsea::mod(
            oneshotsea::mul(lhs, lhs), *modulus);
        const Poly optimized_square = oneshotsea::squaremod(lhs, *modulus);
        check(oneshotsea::equal(reference_square, optimized_square),
              "squaremod disagrees with generic multiply/divide");

        const mpz_class exponent = 257;
        Poly generic_power = Poly::constant(field, 1);
        Poly generic_base = oneshotsea::mod(lhs, *modulus);
        mpz_class remaining = exponent;
        while (remaining > 0) {
            if (mpz_odd_p(remaining.get_mpz_t()) != 0) {
                generic_power = oneshotsea::mod(
                    oneshotsea::mul(generic_power, generic_base), *modulus);
            }
            remaining >>= 1;
            if (remaining > 0) {
                generic_base = oneshotsea::mod(
                    oneshotsea::mul(generic_base, generic_base), *modulus);
            }
        }
        const Poly optimized_power = oneshotsea::powmod(lhs, exponent, *modulus);
        check(oneshotsea::equal(generic_power, optimized_power),
              "powmod disagrees with generic multiply/divide");
    }
}

void test_delayed_reduction_differential() {
    const Field small(1019);
    for (std::size_t degree = 1; degree <= 80; degree += 3U) {
        for (std::uint64_t sample = 0; sample < 6; ++sample) {
            check_modular_products(small, degree,
                                   UINT64_C(0x100000000) * degree + sample);
        }
    }

    const Field p125(oneshotsea::parse_integer(
        "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237"));
    for (std::size_t degree : {1U, 2U, 3U, 7U, 16U, 31U, 64U, 96U, 128U}) {
        for (std::uint64_t sample = 0; sample < 3; ++sample) {
            check_modular_products(
                p125, degree,
                UINT64_C(0x4160000000000000) ^
                    (static_cast<std::uint64_t>(degree) << 16U) ^ sample);
        }
    }
}

void test_root_extraction_regression() {
    const Field field(1019);  // 1019 == 3 mod 4, so x^2 + 1 is irreducible.
    const Poly x = Poly::x(field);
    Poly polynomial = Poly::constant(field, 1);
    const std::vector<mpz_class> expected = {2, 17, 29, 777};
    for (const mpz_class& root : expected) {
        polynomial = oneshotsea::mul(
            polynomial, oneshotsea::sub(x, Poly::constant(field, root)));
    }
    polynomial = oneshotsea::mul(polynomial, Poly(field, {1, 0, 1}));

    const std::vector<mpz_class> roots = oneshotsea::linear_roots(polynomial);
    check(roots == expected, "linear_roots changed under delayed reduction");
    check(oneshotsea::rational_root_count(polynomial) ==
              static_cast<int>(expected.size()),
          "rational_root_count changed under delayed reduction");
}

void test_edge_cases() {
    const Field field(1019);
    const Poly zero(field);
    const Poly x = Poly::x(field);
    const Poly modulus(field, {3, 5, 1});
    check(oneshotsea::mulmod(zero, x, modulus).is_zero(),
          "zero mulmod result");
    check(oneshotsea::squaremod(zero, modulus).is_zero(),
          "zero squaremod result");

    const Poly linear_modulus(field, {7, 11});
    const Poly value(field, {8, 9, 10, 11, 12});
    check(oneshotsea::equal(
              oneshotsea::mulmod(value, value, linear_modulus),
              oneshotsea::mod(oneshotsea::mul(value, value), linear_modulus)),
          "degree-one nonmonic modulus");
}

void benchmark() {
    const Field p125(oneshotsea::parse_integer(
        "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237"));
    constexpr std::size_t degree = 256;
    std::vector<mpz_class> modulus_coefficients(degree + 1U, 0);
    for (std::size_t index = 0; index < degree; ++index) {
        modulus_coefficients[index] = oneshotsea::deterministic_residue(
            p125, UINT64_C(0x62656e63686d6f64),
            static_cast<std::uint64_t>(index), UINT64_C(0x706f6c79323536));
    }
    modulus_coefficients.back() = 1;
    const Poly modulus(p125, std::move(modulus_coefficients));
    Poly value = deterministic_poly(p125, degree, UINT64_C(0x62656e636876616c));

    constexpr std::size_t iterations = 30;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        value = oneshotsea::squaremod(value, modulus);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);
    std::cout << "benchmark_us=" << elapsed.count()
              << " checksum=" << value.coefficient(0) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--bench") {
            benchmark();
            return EXIT_SUCCESS;
        }
        test_edge_cases();
        test_delayed_reduction_differential();
        test_root_extraction_regression();
        std::cout << "poly reduction tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "poly reduction tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
