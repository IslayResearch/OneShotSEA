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

mpz_class integer_power(unsigned long base, unsigned long exponent) {
    mpz_class result;
    mpz_ui_pow_ui(result.get_mpz_t(), base, exponent);
    return result;
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
    const auto weber_selected =
        oneshotsea::select_sutherland_weber_crt_primes(
            order, 1009, 10000, 10000);
    check(!weber_selected.empty() &&
              std::all_of(
                  weber_selected.begin(), weber_selected.end(),
                  [](const auto& record) {
                      return mpz_fdiv_ui(record.prime.get_mpz_t(), 12U) ==
                             11U;
                  }),
          "Weber CRT selector retains only two-lift p=11 mod 12 primes");
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

    const auto discovered =
        oneshotsea::discover_sutherland_suitable_order(5);
    check(discovered.order.fundamental_discriminant() == -71 &&
              discovered.order.conductor() == 1 &&
              discovered.order.class_number() == 7U &&
              discovered.order.weber_f_order_congruences_hold() &&
              discovered.fundamental_discriminants_tested > 0U &&
              discovered.conductor_candidates_tested > 0U,
          "bounded discovery finds and validates the deterministic ell=5 Weber order");
    const auto discovered_again =
        oneshotsea::discover_sutherland_suitable_order(5);
    check(discovered_again.order.discriminant() ==
                  discovered.order.discriminant() &&
              discovered_again.fundamental_discriminants_tested ==
                  discovered.fundamental_discriminants_tested &&
              discovered_again.conductor_candidates_tested ==
                  discovered.conductor_candidates_tested,
          "suitable-order discovery is deterministic with stable evidence counts");
    const auto medium_discovered =
        oneshotsea::discover_sutherland_suitable_order(401);
    check(medium_discovered.order.level() == 401U &&
              medium_discovered.order.discriminant() <= -mpz_class(401 * 401) &&
              medium_discovered.order.class_number() >= 403U &&
              medium_discovered.order.class_number() <= 601U &&
              medium_discovered.order.weber_f_order_congruences_hold() &&
              medium_discovered.conductor_candidates_tested < 1000000U,
          "bounded discovery reaches the current 400-level SEA range");
    std::size_t catalog_order_count = 0U;
    for (unsigned current_level = 5U; current_level <= 997U;
         current_level += 2U) {
        const mpz_class encoded_level(std::to_string(current_level));
        if (mpz_probab_prime_p(encoded_level.get_mpz_t(), 25) == 0) {
            continue;
        }
        const auto current =
            oneshotsea::discover_sutherland_suitable_order(current_level);
        check(current.order.level() == current_level &&
                  current.order.class_number() >= current_level + 2U &&
                  2U * current.order.class_number() <= 3U * current_level &&
                  current.order.weber_f_order_congruences_hold(),
              "suitable-order discovery covers authenticated Weber level " +
                  std::to_string(current_level));
        ++catalog_order_count;
    }
    check(catalog_order_count == 166U,
          "suitable-order discovery covers all 166 catalog levels through 997");
    check(rejects([] {
              oneshotsea::SutherlandOrderSearchOptions options;
              options.maximum_conductor_candidates = 1U;
              (void)oneshotsea::discover_sutherland_suitable_order(5, options);
          }) &&
              rejects([] {
                  oneshotsea::SutherlandOrderSearchOptions options;
                  options.maximum_fundamental_abs = 70U;
                  (void)oneshotsea::discover_sutherland_suitable_order(
                      5, options);
              }) &&
              rejects([] {
                  (void)oneshotsea::discover_sutherland_suitable_order(3);
              }),
          "suitable-order discovery fails closed on caps, search bounds, and empty intervals");

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
    const auto coefficient_bound =
        oneshotsea::derive_exact_crt_coefficient_bound_from_table_reference(
            modular_polynomial, powers);
    check(coefficient_bound.absolute_bound() == bound &&
              coefficient_bound.evidence() ==
                  oneshotsea::CrtCoefficientBoundEvidence::
                      exact_table_reference,
          "opaque CRT height evidence agrees with independent coefficient accumulation");
    const auto proved_classical_bound =
        oneshotsea::derive_proved_classical_algorithm1_coefficient_bound(
            5, target_field.modulus());
    const mpz_class proved_numerator =
        target_field.modulus() * integer_power(7, 3) *
        integer_power(5, 30) * integer_power(11, 90);
    const mpz_class proved_denominator = integer_power(4, 90);
    const mpz_class expected_proved_bound =
        (proved_numerator + proved_denominator - 1) /
        proved_denominator;
    check(proved_classical_bound.evidence() ==
                  oneshotsea::CrtCoefficientBoundEvidence::
                      proved_classical_algorithm1 &&
              proved_classical_bound.absolute_bound() ==
                  expected_proved_bound &&
              proved_classical_bound.absolute_bound() > bound &&
              rejects([] {
                  static_cast<void>(oneshotsea::
                      derive_proved_classical_algorithm1_coefficient_bound(
                          4, 193));
              }) &&
              rejects([] {
                  static_cast<void>(oneshotsea::
                      derive_proved_classical_algorithm1_coefficient_bound(
                          5, 195));
              }),
          "classical Algorithm 1 bound is exact-integer, theorem-derived, and fail-closed");
    check(rejects([&] {
              (void)oneshotsea::
                  derive_exact_crt_coefficient_bound_from_table_reference(
                      modular_polynomial, {1, 2});
          }) &&
              rejects([&] {
                  std::vector<mpz_class> invalid_powers = powers;
                  invalid_powers[2] = -1;
                  (void)oneshotsea::
                      derive_exact_crt_coefficient_bound_from_table_reference(
                          modular_polynomial, invalid_powers);
              }),
          "exact height derivation rejects missing and negative lifts");

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
            order, target_field, source, coefficient_bound, 10000,
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
                check(mpz_fdiv_ui(record.prime.get_mpz_t(), 12U) == 11U,
                      "Weber Algorithm 1 supplies only two-lift CRT primes");
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
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::reconstruct_weber_specialization_algorithm1(
                      order, target_field, source, proved_classical_bound,
                      10000,
                      [&modular_polynomial](
                          const oneshotsea::SutherlandCrtPrime& record,
                          const std::vector<mpz_class>& supplied_powers) {
                          return oneshotsea::
                              specialize_sparse_modpoly_for_crt_reference(
                                  modular_polynomial, supplied_powers,
                                  record.prime);
                      }));
          }),
          "Weber wrapper rejects the unrelated proved classical height evidence");

    const auto classical_modular_polynomial =
        oneshotsea::SparseModularPolynomial::load(
            5, "data/modpoly/j/phi_5.txt");
    const auto classical_expected =
        classical_modular_polynomial.specialize_x_with_derivative(
            target_field, source);
    std::size_t classical_calls = 0U;
    const auto classical_orchestrated =
        oneshotsea::reconstruct_classical_specialization_algorithm1(
            order, target_field, source, 10000,
            [&classical_modular_polynomial, &powers, &classical_calls](
                const oneshotsea::SutherlandCrtPrime& record,
                const std::vector<mpz_class>& supplied_powers) {
                ++classical_calls;
                check(supplied_powers == powers,
                      "classical Algorithm 1 preserves target-field lifts");
                return oneshotsea::specialize_sparse_modpoly_for_crt_reference(
                    classical_modular_polynomial, supplied_powers,
                    record.prime);
            });
    check(classical_calls == classical_orchestrated.prime_count &&
              classical_orchestrated.coefficient_abs_bound ==
                  expected_proved_bound &&
              classical_orchestrated.crt_product >
                  4 * expected_proved_bound &&
              oneshotsea::equal(
                  classical_orchestrated.specialization.value(),
                  classical_expected.value()) &&
              oneshotsea::equal(
                  classical_orchestrated.specialization.x_derivative(),
                  classical_expected.x_derivative()),
          "proved-bound classical Algorithm 1 reconstructs both target channels");

    const auto incompatible_order =
        oneshotsea::validate_sutherland_suitable_order(5, -251, 1);
    check(!incompatible_order.weber_f_order_congruences_hold() &&
              rejects([&] {
                  (void)oneshotsea::
                      reconstruct_weber_specialization_algorithm1(
                          incompatible_order, target_field, source,
                          coefficient_bound,
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
