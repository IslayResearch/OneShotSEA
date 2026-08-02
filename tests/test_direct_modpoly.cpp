#include "oneshotsea/direct_modpoly.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
bool rejects(Callable&& callable) {
    try {
        callable();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

mpz_class target_prime() {
    return oneshotsea::parse_integer(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237");
}

oneshotsea::CrtSpecializationResidue signed_residue(
    const mpz_class& prime, const std::vector<mpz_class>& value,
    const std::vector<mpz_class>& x_derivative) {
    const oneshotsea::Field field(prime);
    std::vector<mpz_class> value_residues(value.size());
    std::vector<mpz_class> derivative_residues(x_derivative.size());
    for (std::size_t index = 0U; index < value.size(); ++index) {
        value_residues[index] = field.normalize(value[index]);
    }
    for (std::size_t index = 0U; index < x_derivative.size(); ++index) {
        derivative_residues[index] = field.normalize(x_derivative[index]);
    }
    return {prime, std::move(value_residues),
            std::move(derivative_residues)};
}

void check_reconstruction(
    const oneshotsea::Field& target_field,
    const oneshotsea::CrtSpecializationResult& result,
    const std::vector<mpz_class>& value,
    const std::vector<mpz_class>& x_derivative,
    const std::string& label) {
    check(result.specialization.value().coefficients().size() == value.size(),
          label + " value size");
    for (std::size_t index = 0U; index < value.size(); ++index) {
        check(result.specialization.value().coefficient(index) ==
                      target_field.normalize(value[index]) &&
                  result.specialization.x_derivative().coefficient(index) ==
                      target_field.normalize(x_derivative[index]),
              label + " coefficient " + std::to_string(index));
    }
}

void test_class_numbers_and_orders() {
    for (const auto& [discriminant, expected] :
         std::vector<std::pair<long, std::uint64_t>>{
             {-3, 1}, {-4, 1}, {-7, 1}, {-8, 1}, {-11, 1},
             {-15, 2}, {-20, 2}, {-23, 3}, {-31, 3}, {-39, 4},
             {-47, 5}, {-63, 4}, {-71, 7}, {-84, 4}}) {
        check(oneshotsea::negative_order_class_number(discriminant) ==
                  expected,
              "binary quadratic form class number D=" +
                  std::to_string(discriminant));
    }
    check(rejects([] {
              (void)oneshotsea::negative_order_class_number(-5);
          }) &&
              rejects([] {
                  (void)oneshotsea::negative_order_class_number(7);
              }),
          "class-number counter rejects non-discriminants");

    const auto order = oneshotsea::validate_sutherland_suitable_order(
        5, -71, 1);
    check(order.level() == 5U &&
              order.fundamental_discriminant() == -71 &&
              order.conductor() == 1 && order.discriminant() == -71 &&
              order.class_number() == 7U &&
              order.weber_f_order_congruences_hold(),
          "D=-71 is a checked Weber-compatible suitable order for ell=5");
    check(rejects([] {
              (void)oneshotsea::validate_sutherland_suitable_order(
                  5, -23, 1);
          }) &&
              rejects([] {
                  (void)oneshotsea::validate_sutherland_suitable_order(
                      5, -72, 1);
              }) &&
              rejects([] {
                  (void)oneshotsea::validate_sutherland_suitable_order(
                      5, -71, 5);
              }),
          "suitable-order validation rejects class, fundamental, and conductor failures");

    const auto selected = oneshotsea::select_sutherland_crt_primes(
        order, 1009, 10000, 10000);
    mpz_class product = 1;
    mpz_class previous = 0;
    for (const auto& record : selected) {
        check(record.prime > previous && record.prime != 1009 &&
                  mpz_probab_prime_p(record.prime.get_mpz_t(), 25) != 0 &&
                  mpz_divisible_p(order.discriminant().get_mpz_t(),
                                  record.prime.get_mpz_t()) == 0 &&
                  mpz_fdiv_ui(record.prime.get_mpz_t(), 5U) == 1U &&
                  4 * record.prime ==
                      record.trace * record.trace -
                          mpz_class(25) * record.volcano_parameter *
                              record.volcano_parameter *
                              order.discriminant() &&
                  record.volcano_parameter == 2,
              "selected prime satisfies the exact volcano equation");
        previous = record.prime;
        product *= record.prime;
    }
    check(!selected.empty() && product > 40000,
          "selected CRT primes cross the strict four-times height bound");
    const auto repeated = oneshotsea::select_sutherland_crt_primes(
        order, 1009, 10000, 10000);
    check(repeated.size() == selected.size(),
          "suitable-prime selection is deterministic");
    for (std::size_t index = 0U; index < selected.size(); ++index) {
        check(repeated[index].prime == selected[index].prime &&
                  repeated[index].trace == selected[index].trace &&
                  repeated[index].volcano_parameter ==
                      selected[index].volcano_parameter,
              "suitable-prime selection preserves every witness");
    }
    check(rejects([&order] {
              (void)oneshotsea::select_sutherland_crt_primes(
                  order, 1009, 10000, 1);
          }),
          "suitable-prime search fails closed at its candidate cap");

    const auto odd_trace_order =
        oneshotsea::validate_sutherland_suitable_order(5, -251, 1);
    const auto odd_trace_selected =
        oneshotsea::select_sutherland_crt_primes(
            odd_trace_order, 1009, 100, 10000);
    const mpz_class odd_trace_numerator =
        odd_trace_selected.front().trace * odd_trace_selected.front().trace -
        mpz_class(25) * odd_trace_selected.front().volcano_parameter *
            odd_trace_selected.front().volcano_parameter *
            odd_trace_order.discriminant();
    check(!odd_trace_selected.empty() &&
              mpz_odd_p(odd_trace_selected.front().trace.get_mpz_t()) != 0 &&
              mpz_divisible_ui_p(odd_trace_numerator.get_mpz_t(), 4U) != 0,
          "prime selector chooses the required odd trace parity when D is odd and v=1");
}

void test_explicit_crt_synthetic() {
    constexpr unsigned level = 5U;
    const std::vector<mpz_class> value =
        {-360, 80, 0, 0, 0, -60, 1};
    const std::vector<mpz_class> x_derivative =
        {360, 4, 0, 0, 0, -15, 0};
    const std::vector<mpz_class> primes = {11, 31, 41};
    const auto provider = [&value, &x_derivative](const mpz_class& prime) {
        return signed_residue(prime, value, x_derivative);
    };

    const oneshotsea::Field small_target(1009);
    const auto small = oneshotsea::reconstruct_specialization_explicit_crt(
        level, small_target, 20, 360, primes, provider);
    check(small.crt_product == 13981 && small.prime_count == 3U &&
              small.coefficient_abs_bound == 360,
          "explicit CRT reports its exact coverage evidence");
    check_reconstruction(
        small_target, small, value, x_derivative, "small-field CRT");

    const std::vector<mpz_class> shuffled_primes = {41, 11, 31};
    const auto shuffled =
        oneshotsea::reconstruct_specialization_explicit_crt(
            level, small_target, 20, 360, shuffled_primes, provider);
    check(oneshotsea::equal(small.specialization.value(),
                            shuffled.specialization.value()) &&
              oneshotsea::equal(small.specialization.x_derivative(),
                                shuffled.specialization.x_derivative()),
          "explicit CRT is invariant under auxiliary-prime order");

    const oneshotsea::Field large_target(target_prime());
    const auto large = oneshotsea::reconstruct_specialization_explicit_crt(
        level, large_target, 20, 360, primes, provider);
    check_reconstruction(
        large_target, large, value, x_derivative, "416-bit CRT");

    check(rejects([&] {
              (void)oneshotsea::reconstruct_specialization_explicit_crt(
                  level, small_target, 20, 360, {11, 31}, provider);
          }),
          "explicit CRT rejects insufficient height coverage");
    check(rejects([&] {
              (void)oneshotsea::reconstruct_specialization_explicit_crt(
                  level, small_target, 20, 360, {11, 31, 31}, provider);
          }) &&
              rejects([&] {
                  (void)oneshotsea::reconstruct_specialization_explicit_crt(
                      level, small_target, 20, 360, {11, 31, 35}, provider);
              }) &&
              rejects([&] {
                  (void)oneshotsea::reconstruct_specialization_explicit_crt(
                      level, small_target, 1029, 360, primes, provider);
              }) &&
              rejects([&] {
                  (void)oneshotsea::reconstruct_specialization_explicit_crt(
                      level, small_target, 20, 360,
                      {11, 31, mpz_class("341550071728321")}, provider);
              }) &&
              rejects([&] {
                  (void)oneshotsea::reconstruct_specialization_explicit_crt(
                      level, small_target, 20, 360,
                      {11, 31, target_prime()}, provider);
              }),
          "explicit CRT rejects duplicate, pseudoprime, oversized, and noncanonical inputs");

    check(rejects([&] {
              (void)oneshotsea::reconstruct_specialization_explicit_crt(
                  level, small_target, 20, 360, primes,
                  [&value, &x_derivative](const mpz_class& prime) {
                      auto residue =
                          signed_residue(prime, value, x_derivative);
                      if (prime == 31) {
                          residue.value_coefficients[0] =
                              (residue.value_coefficients[0] + 1) % prime;
                      }
                      return residue;
                  });
          }),
          "centered CRT validation rejects a corrupted residue beyond the bound");
    check(rejects([&] {
              (void)oneshotsea::reconstruct_specialization_explicit_crt(
                  level, small_target, 20, 360, primes,
                  [&value, &x_derivative](const mpz_class& prime) {
                      auto residue =
                          signed_residue(prime, value, x_derivative);
                      residue.value_coefficients.back() = 2;
                      return residue;
                  });
          }),
          "explicit CRT rejects a nonmonic per-prime specialization");
    check(rejects([&] {
              (void)oneshotsea::reconstruct_specialization_explicit_crt(
                  level, small_target, 20, 360, primes,
                  [&value, &x_derivative](const mpz_class& prime) {
                      auto residue =
                          signed_residue(prime, value, x_derivative);
                      residue.prime = 13;
                      return residue;
                  });
          }) &&
              rejects([&] {
                  (void)oneshotsea::reconstruct_specialization_explicit_crt(
                      level, small_target, 20, 360, primes,
                      [&value, &x_derivative](const mpz_class& prime) {
                          auto residue =
                              signed_residue(prime, value, x_derivative);
                          residue.value_coefficients[0] = prime;
                          return residue;
                      });
              }) &&
              rejects([&] {
                  (void)oneshotsea::reconstruct_specialization_explicit_crt(
                      level, small_target, 20, 360, primes,
                      [&value, &x_derivative](const mpz_class& prime) {
                          auto residue =
                              signed_residue(prime, value, x_derivative);
                          residue.x_derivative_coefficients.pop_back();
                          return residue;
                      });
              }),
          "explicit CRT rejects wrong-prime, noncanonical, and wrong-sized residues");
}

void test_algorithm1_table_differential() {
    const auto modular_polynomial =
        oneshotsea::SparseModularPolynomial::load(
            5, "data/modpoly/weber_f/phi_5.txt");
    const oneshotsea::Field target_field(193);
    const mpz_class source = 20;
    const std::vector<mpz_class> powers =
        oneshotsea::lifted_target_powers(target_field, source, 6);
    check(powers == std::vector<mpz_class>({1, 20, 14, 87, 3, 60, 42}),
          "Algorithm 1 lifts powers after target-field multiplication");
    check(powers[2] % 11 != ((source % 11) * (source % 11)) % 11,
          "lift-then-reduce differs from powering in an auxiliary field");

    std::vector<mpz_class> integer_value(7U, 0);
    std::vector<mpz_class> integer_x_derivative(7U, 0);
    for (const auto& term : modular_polynomial.terms()) {
        integer_value[term.y_degree] +=
            term.coefficient * powers[term.x_degree];
        if (term.x_degree != 0U) {
            integer_x_derivative[term.y_degree] +=
                term.coefficient * term.x_degree *
                powers[term.x_degree - 1U];
        }
    }
    mpz_class bound = 1;
    for (const mpz_class& coefficient : integer_value) {
        bound = std::max(bound,
                         coefficient < 0 ? -coefficient : coefficient);
    }
    for (const mpz_class& coefficient : integer_x_derivative) {
        bound = std::max(bound,
                         coefficient < 0 ? -coefficient : coefficient);
    }
    check(bound == 360, "fixture derives its exact integer height bound");

    const std::vector<mpz_class> primes = {11, 31, 41};
    std::size_t provider_calls = 0U;
    const auto reconstructed =
        oneshotsea::reconstruct_specialization_explicit_crt(
            5, target_field, source, bound, primes,
            [&modular_polynomial, &powers, &provider_calls](
                const mpz_class& prime) {
                ++provider_calls;
                return oneshotsea::specialize_sparse_modpoly_for_crt_reference(
                    modular_polynomial, powers, prime);
            });
    const auto expected =
        modular_polynomial.specialize_x_with_derivative(target_field, source);
    check(provider_calls == primes.size() &&
              oneshotsea::equal(reconstructed.specialization.value(),
                                expected.value()) &&
              oneshotsea::equal(
                  reconstructed.specialization.x_derivative(),
                  expected.x_derivative()) &&
              oneshotsea::equal(
                  reconstructed.specialization.y_derivative(),
                  expected.y_derivative()),
          "Algorithm 1 CRT reconstruction matches direct target-field specialization");

    const auto order = oneshotsea::validate_sutherland_suitable_order(
        5, -71, 1);
    std::size_t volcano_calls = 0U;
    const auto orchestrated =
        oneshotsea::reconstruct_weber_specialization_algorithm1(
            order, target_field, source, bound, 10000,
            [&modular_polynomial, &powers, &volcano_calls, &order](
                const oneshotsea::SutherlandCrtPrime& record,
                const std::vector<mpz_class>& supplied_powers) {
                ++volcano_calls;
                check(supplied_powers == powers,
                      "Algorithm 1 wrapper supplies target-field power lifts");
                check(4 * record.prime ==
                          record.trace * record.trace -
                              mpz_class(25) * record.volcano_parameter *
                                  record.volcano_parameter *
                                  order.discriminant(),
                      "Algorithm 1 provider receives a checked (p,t,v) witness");
                return oneshotsea::specialize_sparse_modpoly_for_crt_reference(
                    modular_polynomial, supplied_powers, record.prime);
            });
    check(volcano_calls == orchestrated.prime_count &&
              orchestrated.crt_product > 4 * bound &&
              oneshotsea::equal(orchestrated.specialization.value(),
                                expected.value()) &&
              oneshotsea::equal(
                  orchestrated.specialization.x_derivative(),
                  expected.x_derivative()) &&
              oneshotsea::equal(
                  orchestrated.specialization.y_derivative(),
                  expected.y_derivative()),
          "checked suitable-order/prime-selection/provider/CRT path matches the table oracle");

    const auto incompatible_order =
        oneshotsea::validate_sutherland_suitable_order(5, -251, 1);
    check(!incompatible_order.weber_f_order_congruences_hold() &&
              rejects([&] {
                  (void)oneshotsea::
                      reconstruct_weber_specialization_algorithm1(
                          incompatible_order, target_field, source, bound,
                          10000,
                          [&modular_polynomial](
                              const oneshotsea::SutherlandCrtPrime& record,
                              const std::vector<mpz_class>& supplied_powers) {
                              return oneshotsea::
                                  specialize_sparse_modpoly_for_crt_reference(
                                      modular_polynomial, supplied_powers,
                                      record.prime);
                          });
              }),
          "Weber Algorithm 1 rejects a suitable order with invalid Weber congruences");
}

}  // namespace

int main() {
    try {
        test_class_numbers_and_orders();
        test_explicit_crt_synthetic();
        test_algorithm1_table_differential();
        std::cout << "direct modular specialization tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "direct modular specialization tests: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
