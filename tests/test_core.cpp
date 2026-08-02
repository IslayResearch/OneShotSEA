#include "oneshotsea/curve.hpp"
#include "oneshotsea/early_abort.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/isogeny.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/poly.hpp"
#include "oneshotsea/schoof.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/trace.hpp"
#include "oneshotsea/torsion.hpp"
#include "oneshotsea/weber.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int roots_by_evaluation(const oneshotsea::Poly& polynomial) {
    const unsigned long p = polynomial.field().modulus().get_ui();
    int roots = 0;
    for (unsigned long value = 0; value < p; ++value) {
        roots += polynomial.evaluate(value) == 0 ? 1 : 0;
    }
    return roots;
}

std::uint64_t small_pow_mod(std::uint64_t base, std::uint64_t exponent,
                            std::uint64_t modulus) {
    std::uint64_t result = 1;
    while (exponent != 0) {
        if ((exponent & 1U) != 0U) {
            result = (result * base) % modulus;
        }
        base = (base * base) % modulus;
        exponent >>= 1U;
    }
    return result;
}

mpz_class target_prime() {
    return oneshotsea::parse_integer(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237");
}

oneshotsea::Poly reference_schoolbook_product(
    const oneshotsea::Poly& lhs, const oneshotsea::Poly& rhs) {
    check(lhs.field().modulus() == rhs.field().modulus(),
          "reference product field match");
    if (lhs.is_zero() || rhs.is_zero()) {
        return oneshotsea::Poly(lhs.field());
    }
    const oneshotsea::Field& field = lhs.field();
    std::vector<mpz_class> output(
        lhs.coefficients().size() + rhs.coefficients().size() - 1U, 0);
    for (std::size_t left = 0; left < lhs.coefficients().size(); ++left) {
        for (std::size_t right = 0; right < rhs.coefficients().size(); ++right) {
            output[left + right] = field.add(
                output[left + right],
                field.mul(lhs.coefficients()[left],
                          rhs.coefficients()[right]));
        }
    }
    return oneshotsea::Poly(field, std::move(output));
}

oneshotsea::Poly dense_polynomial(const oneshotsea::Field& field,
                                  std::size_t size,
                                  std::uint64_t domain) {
    std::vector<mpz_class> coefficients(size);
    for (std::size_t index = 0; index < size; ++index) {
        mpz_class value = oneshotsea::deterministic_residue(
            field, UINT64_C(0x6b61726174737562), index, domain);
        coefficients[index] = index % 3U == 0U ? -value : value;
    }
    if (!coefficients.empty()) {
        coefficients.back() = field.modulus() - 1;
    }
    return oneshotsea::Poly(field, std::move(coefficients));
}

void check_thresholded_products(const oneshotsea::Field& field,
                                std::size_t size) {
    const oneshotsea::Poly lhs = dense_polynomial(field, size, size);
    const std::size_t rhs_size = size <= 2U ? size : size - 1U;
    const oneshotsea::Poly rhs =
        dense_polynomial(field, rhs_size, size + 1U);
    std::vector<mpz_class> modulus_coefficients =
        dense_polynomial(field, size + 1U, size + 2U).coefficients();
    modulus_coefficients.back() = size % 2U == 0U
                                      ? mpz_class(1)
                                      : field.modulus() - 2;
    const oneshotsea::Poly modulus(field, std::move(modulus_coefficients));

    const oneshotsea::Poly product = reference_schoolbook_product(lhs, rhs);
    const oneshotsea::Poly square = reference_schoolbook_product(lhs, lhs);
    check(oneshotsea::equal(oneshotsea::mul(lhs, rhs), product),
          "thresholded exact convolution matches independent schoolbook");
    check(oneshotsea::equal(oneshotsea::mulmod(lhs, rhs, modulus),
                           oneshotsea::mod(product, modulus)),
          "thresholded quotient-ring multiply matches independent schoolbook");
    check(oneshotsea::equal(oneshotsea::squaremod(lhs, modulus),
                           oneshotsea::mod(square, modulus)),
          "thresholded quotient-ring square matches independent schoolbook");
}

void test_thresholded_polynomial_products() {
    const oneshotsea::Field small_field(1009);
    for (const std::size_t size :
         {1U, 2U, 17U, 31U, 32U, 33U, 47U, 64U, 65U, 97U}) {
        check_thresholded_products(small_field, size);
    }
    const oneshotsea::Field large_field(target_prime());
    for (const std::size_t size : {31U, 32U, 33U, 47U, 64U, 97U, 129U,
                                   194U}) {
        check_thresholded_products(large_field, size);
    }
}

void test_field() {
    const oneshotsea::Field field(101);
    check(field.normalize(-1) == 100, "normalization");
    check(field.mul(37, 71) == (37 * 71) % 101, "multiplication");
    check(field.mul(17, field.inverse(17)) == 1, "inverse");
    check(field.pow(3, 100) == 1, "Fermat exponentiation");
    check(field.legendre(0) == 0, "zero Legendre symbol");
}

void test_polynomial() {
    const oneshotsea::Field field(101);
    const oneshotsea::Poly x = oneshotsea::Poly::x(field);
    const oneshotsea::Poly f = oneshotsea::sub(
        oneshotsea::mul(oneshotsea::sub(x, oneshotsea::Poly::constant(field, 2)),
                        oneshotsea::sub(x, oneshotsea::Poly::constant(field, 3))),
        oneshotsea::Poly::constant(field, 0));
    check(f.degree() == 2, "polynomial degree");
    check(f.evaluate(2) == 0 && f.evaluate(3) == 0, "polynomial evaluation");
    check(oneshotsea::rational_root_count(f) == 2, "rational root count");
    check(oneshotsea::linear_roots(f) == std::vector<mpz_class>({2, 3}),
          "linear root extraction");
    check(oneshotsea::linear_roots(oneshotsea::mul(f, f)) ==
              std::vector<mpz_class>({2, 3}),
          "linear root extraction removes multiplicity");
    const auto division = oneshotsea::divmod(f, oneshotsea::sub(
        x, oneshotsea::Poly::constant(field, 2)));
    check(division.second.is_zero(), "polynomial exact division");
    check(division.first.evaluate(3) == 0, "polynomial quotient");

    const oneshotsea::Poly modulus(field, {7, 5, 0, 1});
    const oneshotsea::Poly left(field, {91, 17, 42, 9, 3});
    const oneshotsea::Poly right(field, {6, 88, 19, 4});
    check(oneshotsea::equal(
              oneshotsea::mulmod(left, right, modulus),
              oneshotsea::mod(oneshotsea::mul(left, right), modulus)),
          "direct modular polynomial multiplication");
    check(oneshotsea::equal(
              oneshotsea::squaremod(left, modulus),
              oneshotsea::mod(oneshotsea::mul(left, left), modulus)),
          "direct modular polynomial squaring");

    const oneshotsea::Poly temporary_field_poly(oneshotsea::Field(101), {1, 2});
    check(temporary_field_poly.evaluate(3) == 7,
          "polynomial owns a temporary field context");

    const oneshotsea::Field target_field(target_prime());
    const oneshotsea::Poly target_x = oneshotsea::Poly::x(target_field);
    oneshotsea::Poly target_split = oneshotsea::Poly::constant(target_field, 1);
    for (const unsigned long root : {2UL, 3UL, 5UL, 7UL}) {
        target_split = oneshotsea::mul(
            target_split,
            oneshotsea::sub(target_x, oneshotsea::Poly::constant(target_field, root)));
    }
    check(oneshotsea::linear_roots(target_split) ==
              std::vector<mpz_class>({2, 3, 5, 7}),
          "linear root extraction over the 416-bit target field");

    // Exercise the batched modular-product reducer with a non-monic modulus
    // and dense coefficients near p.  Its exact mpz_submul intermediates are
    // deliberately much larger (and negative) before final normalization.
    std::vector<mpz_class> target_modulus_coefficients(18, target_prime() - 1);
    target_modulus_coefficients.back() = target_prime() - 2;
    const oneshotsea::Poly target_modulus(
        target_field, std::move(target_modulus_coefficients));
    std::vector<mpz_class> target_left_coefficients(17);
    std::vector<mpz_class> target_right_coefficients(17);
    for (std::size_t index = 0; index < 17; ++index) {
        target_left_coefficients[index] = target_prime() - 3 - index;
        target_right_coefficients[index] = target_prime() - 41 - 2 * index;
    }
    const oneshotsea::Poly target_left(
        target_field, std::move(target_left_coefficients));
    const oneshotsea::Poly target_right(
        target_field, std::move(target_right_coefficients));
    check(oneshotsea::equal(
              oneshotsea::mulmod(target_left, target_right, target_modulus),
              oneshotsea::mod(oneshotsea::mul(target_left, target_right),
                              target_modulus)),
          "batched modular reduction matches generic non-monic division");

    bool rejected_composite_root_field = false;
    try {
        static_cast<void>(oneshotsea::linear_roots(
            oneshotsea::Poly(oneshotsea::Field(15), {-1, 0, 1})));
    } catch (const std::invalid_argument&) {
        rejected_composite_root_field = true;
    }
    check(rejected_composite_root_field,
          "polynomial root extraction rejects composite moduli");
    test_thresholded_polynomial_products();
}

void test_curves() {
    const oneshotsea::Curve curve(oneshotsea::Field(101), 2, 3);
    check(!curve.is_singular(), "nonsingular curve");
    const mpz_class order = oneshotsea::count_points_bruteforce(curve);
    check(order == 96, "known point count");
    mpz_class nonsquare = 2;
    while (curve.field().legendre(nonsquare) != -1) {
        ++nonsquare;
    }
    const auto twist = curve.quadratic_twist(nonsquare);
    check(order + oneshotsea::count_points_bruteforce(twist) == 2 * 101 + 2,
          "curve/twist order sum");
    const auto c0 = oneshotsea::deterministic_curve(101, 7, 11);
    const auto c1 = oneshotsea::deterministic_curve(101, 7, 11);
    check(c0.a() == c1.a() && c0.b() == c1.b(), "deterministic curve mapping");

    const oneshotsea::MontgomeryCurve montgomery(oneshotsea::Field(101), 7);
    check(!montgomery.is_singular(), "nonsingular Montgomery curve");
    const auto short_curve = montgomery.short_weierstrass();
    check(short_curve.j_invariant() == montgomery.j_invariant(),
          "Montgomery/short j-invariant");
    for (unsigned long x_montgomery = 0; x_montgomery < 20; ++x_montgomery) {
        const mpz_class x_short = montgomery.short_x(x_montgomery);
        check(montgomery.montgomery_x(x_short) == x_montgomery,
              "Montgomery x-coordinate roundtrip");
        const mpz_class montgomery_rhs = montgomery.field().add(
            montgomery.field().add(
                montgomery.field().mul(montgomery.field().square(x_montgomery), x_montgomery),
                montgomery.field().mul(
                    montgomery.coefficient(), montgomery.field().square(x_montgomery))),
            x_montgomery);
        const mpz_class short_rhs = short_curve.field().add(
            short_curve.field().add(
                short_curve.field().mul(short_curve.field().square(x_short), x_short),
                short_curve.field().mul(short_curve.a(), x_short)),
            short_curve.b());
        check(montgomery_rhs == short_rhs, "Montgomery/short curve equation");
    }
    const auto m0 = oneshotsea::deterministic_montgomery_curve(101, 19, 23);
    const auto m1 = oneshotsea::deterministic_montgomery_curve(101, 19, 23);
    check(m0.coefficient() == m1.coefficient(), "deterministic Montgomery mapping");

    const oneshotsea::Curve singular(oneshotsea::Field(7), 0, 0);
    check(singular.is_singular(), "singular short-Weierstrass fixture");
    bool rejected_singular_j = false;
    bool rejected_singular_bruteforce = false;
    bool rejected_singular_schoof = false;
    bool rejected_singular_sea = false;
    try {
        static_cast<void>(singular.j_invariant());
    } catch (const std::domain_error&) {
        rejected_singular_j = true;
    }
    try {
        static_cast<void>(oneshotsea::count_points_bruteforce(singular));
    } catch (const std::invalid_argument&) {
        rejected_singular_bruteforce = true;
    }
    try {
        static_cast<void>(oneshotsea::schoof_count_reference(singular, 7));
    } catch (const std::invalid_argument&) {
        rejected_singular_schoof = true;
    }
    try {
        static_cast<void>(oneshotsea::run_weber_sea_reference(
            singular, "data/modpoly/weber_f", 7, 1));
    } catch (const std::invalid_argument&) {
        rejected_singular_sea = true;
    }
    check(rejected_singular_j && rejected_singular_bruteforce &&
              rejected_singular_schoof && rejected_singular_sea,
          "all native point-count entry points reject singular curves");
}

void test_modular_polynomials() {
    const auto phi2 = oneshotsea::SparseModularPolynomial::load(
        2, "data/modpoly/j/phi_2.txt");
    const auto phi3 = oneshotsea::SparseModularPolynomial::load(
        3, "data/modpoly/j/phi_3.txt");
    const mpz_class p = 101;
    const oneshotsea::Field field(p);
    unsigned checked_three = 0;
    for (unsigned long a = 1; a < 12; ++a) {
        for (unsigned long b = 1; b < 12; ++b) {
            const oneshotsea::Curve curve(field, a, b);
            if (curve.is_singular()) {
                continue;
            }
            const mpz_class j = curve.j_invariant();
            if (j == 0 || j == 1728 % p) {
                continue;
            }
            const auto specialized2 = phi2.evaluate_x(field, j);
            check(oneshotsea::rational_root_count(specialized2) ==
                      roots_by_evaluation(specialized2),
                  "Phi_2 root count agrees with evaluation");

            const mpz_class order = oneshotsea::count_points_bruteforce(curve);
            const mpz_class trace = p + 1 - order;
            if (trace == 0) {
                continue;  // supersingular factorization is not the ordinary SEA 0/2-root case
            }
            const long discriminant_mod_3 = mpz_class(trace * trace - 4 * p).get_si() % 3;
            if (discriminant_mod_3 == 0) {
                continue;
            }
            const bool expected_elkies = discriminant_mod_3 == 1 || discriminant_mod_3 == -2;
            const auto specialized3 = phi3.evaluate_x(field, j);
            const int roots = oneshotsea::rational_root_count(specialized3);
            check(roots == roots_by_evaluation(specialized3),
                  "Phi_3 root count agrees with evaluation");
            if ((roots > 0) != expected_elkies) {
                throw std::runtime_error(
                    "Phi_3 Elkies/Atkin classification: a=" + std::to_string(a) +
                    " b=" + std::to_string(b) + " trace=" + trace.get_str() +
                    " disc_mod_3=" + std::to_string(discriminant_mod_3) +
                    " roots=" + std::to_string(roots) +
                    " expected_elkies=" + std::to_string(expected_elkies));
            }
            ++checked_three;
        }
    }
    check(checked_three > 20, "enough Phi_3 classification cases");
}

void test_trace_constraints() {
    const oneshotsea::ExactTracePrior composite_prior(101, 20, 0);
    check(composite_prior.prime() == 101 &&
              composite_prior.modulus() == 20U &&
              composite_prior.residue() == 0U,
          "validated exact trace prior accepts a composite modulus");
    const auto invalid_prior_rejected = [](const auto& factory) {
        try {
            (void)factory();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    check(invalid_prior_rejected(
              []() { return oneshotsea::ExactTracePrior(101, 1, 0); }) &&
              invalid_prior_rejected(
                  []() { return oneshotsea::ExactTracePrior(101, 4, 4); }) &&
              invalid_prior_rejected([]() {
                  return oneshotsea::ExactTracePrior(101, 101, 0);
              }),
          "invalid or characteristic-sharing trace prior is rejected");
    bool rejected_hasse_inconsistent_prior = false;
    try {
        (void)oneshotsea::ExactTracePrior(101, 1000, 500);
    } catch (const std::runtime_error&) {
        rejected_hasse_inconsistent_prior = true;
    }
    check(rejected_hasse_inconsistent_prior,
          "Hasse-inconsistent exact trace prior is rejected");

    oneshotsea::TraceConstraints constraints(101);
    check(constraints.hasse_radius() == 20, "Hasse radius");
    const mpz_class initial_count = constraints.candidate_count();
    check(initial_count == 41, "initial trace count: " + initial_count.get_str());
    constraints.refine(3, {1});
    const auto modulo_three = constraints.enumerate(100);
    check(modulo_three.has_value(), "trace enumeration cap");
    check(modulo_three->size() == 14, "trace count after one residue");
    for (const mpz_class& trace : *modulo_three) {
        mpz_class residue;
        mpz_mod_ui(residue.get_mpz_t(), trace.get_mpz_t(), 3);
        check(residue == 1, "enumerated trace residue modulo 3");
    }
    constraints.refine(5, {2});
    const auto modulo_fifteen = constraints.enumerate(100);
    check(modulo_fifteen.has_value(), "CRT trace enumeration");
    for (const mpz_class& trace : *modulo_fifteen) {
        check(mpz_fdiv_ui(trace.get_mpz_t(), 3) == 1, "CRT residue modulo 3");
        check(mpz_fdiv_ui(trace.get_mpz_t(), 5) == 2, "CRT residue modulo 5");
    }

    const auto elkies = oneshotsea::trace_residues_from_classification(3, 101, true);
    const auto atkin = oneshotsea::trace_residues_from_classification(3, 101, false);
    check(!elkies.empty() && !atkin.empty(), "classification trace partitions");
    for (const auto residue : elkies) {
        check(std::find(atkin.begin(), atkin.end(), residue) == atkin.end(),
              "classification partitions are disjoint");
    }
    check(elkies.size() + atkin.size() == 3, "classification covers all residues");

    oneshotsea::TraceConstraints exact(1009);
    exact.refine(5, {0});
    exact.refine(11, {0});
    exact.refine(19, {0});
    check(exact.modulus() == 1045, "exact CRT modulus");
    check(exact.candidate_count() == 1, "exact CRT candidate count");
    const auto exact_traces = exact.enumerate(1);
    check(exact_traces.has_value() &&
              *exact_traces == std::vector<mpz_class>{mpz_class(0)},
          "exact CRT candidate enumeration");

    // The Hasse interval for p=89 is [-18,18].  A modulus one below its
    // width admits the endpoint pair -18 and 17 in the same residue class;
    // one more coprime exact residue must cross the uniqueness threshold.
    oneshotsea::TraceConstraints boundary(89);
    boundary.refine_exact(5, 2);
    boundary.refine_exact(7, 3);
    check(boundary.modulus() == 35 && boundary.candidate_count() == 2,
          "CRT immediately below the Hasse-width threshold has two traces");
    const auto boundary_pair = boundary.enumerate(2);
    check(boundary_pair.has_value() &&
              *boundary_pair == std::vector<mpz_class>({-18, 17}),
          "CRT retains both Hasse endpoint-collision traces");
    boundary.refine_exact(3, 2);
    const auto boundary_unique = boundary.enumerate(1);
    check(boundary.modulus() == 105 && boundary.candidate_count() == 1 &&
              boundary_unique.has_value() &&
              *boundary_unique == std::vector<mpz_class>{17},
          "CRT immediately above the threshold isolates the exact trace");

    const mpz_class prior_modulus = exact.modulus();
    const auto prior_residues = exact.residues();
    bool rejected_corrupt_exact_residue = false;
    try {
        exact.refine_exact(43, 1);
    } catch (const std::runtime_error&) {
        rejected_corrupt_exact_residue = true;
    }
    check(rejected_corrupt_exact_residue && exact.modulus() == prior_modulus &&
              exact.residues() == prior_residues && exact.candidate_count() == 1,
          "corrupt exact residue hard-fails transactionally");
}

void test_weber_sea_runner() {
    const std::vector<std::uint64_t> increasing_levels = {5, 7, 11};
    const std::vector<oneshotsea::WeberSeaLevelEstimate> estimates = {
        {5, 4, 10}, {7, 7, 28}, {11, 6, 10}};
    check(oneshotsea::expected_information_per_cost_order(
              increasing_levels, estimates) ==
              std::vector<std::uint64_t>({11, 5, 7}),
          "measured information-per-cost schedule uses exact ratio ordering");
    check(oneshotsea::expected_information_per_cost_order(
              {5, 7}, {{5, 2, 4}, {7, 3, 6}}) ==
              std::vector<std::uint64_t>({5, 7}),
          "equal scheduling scores retain increasing-prime order");
    for (const std::vector<oneshotsea::WeberSeaLevelEstimate>& invalid : {
             std::vector<oneshotsea::WeberSeaLevelEstimate>{{5, 1, 1},
                                                            {5, 2, 1},
                                                            {11, 1, 1}},
             std::vector<oneshotsea::WeberSeaLevelEstimate>{{5, 1, 1},
                                                            {7, 1, 1}},
             std::vector<oneshotsea::WeberSeaLevelEstimate>{{5, 1, 1},
                                                            {7, 1, 1},
                                                            {13, 1, 1}},
             std::vector<oneshotsea::WeberSeaLevelEstimate>{{5, 1, 1},
                                                            {7, 1, 0},
                                                            {11, 1, 1}},
         }) {
        bool rejected = false;
        try {
            (void)oneshotsea::expected_information_per_cost_order(
                increasing_levels, invalid);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "invalid scheduling profile is rejected");
    }

    const auto phi5 = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/weber_f/phi_5.txt");
    const oneshotsea::Curve curve(oneshotsea::Field(193), 148, 168);
    const auto serial_level = oneshotsea::compute_weber_elkies_level_reference(
        curve, phi5, nullptr, 1);
    const auto parallel_level =
        oneshotsea::compute_weber_elkies_level_reference(
            curve, phi5, nullptr, 2);
    check(!serial_level.kernels.empty() &&
              serial_level.kernels.front().trace_residue == 4,
          "Weber level returns an exact residue");
    check(!serial_level.compatible_source_lifts.empty(),
          "positive Weber level identifies compatible source lifts");
    check(serial_level.timings.modular_root_workers == 1U &&
              parallel_level.timings.modular_root_workers == 1U &&
              serial_level.timings.modular_root_orbits == 1U &&
              parallel_level.timings.modular_root_orbits == 1U &&
              serial_level.timings.modular_root_reused_lifts == 23U &&
              parallel_level.timings.modular_root_reused_lifts == 23U &&
              serial_level.timings.modular_root_orbit_reuse &&
              parallel_level.timings.modular_root_orbit_reuse,
          "Weber modular roots reuse a verified 24th-root source orbit");
    check(parallel_level.compatible_source_lifts ==
                  serial_level.compatible_source_lifts &&
              parallel_level.kernels.size() == serial_level.kernels.size(),
          "bounded parallel root extraction preserves deterministic results");
    for (std::size_t index = 0; index < serial_level.kernels.size(); ++index) {
        check(parallel_level.kernels[index].trace_residue ==
                      serial_level.kernels[index].trace_residue &&
                  parallel_level.kernels[index].neighbor_j ==
                      serial_level.kernels[index].neighbor_j &&
                  oneshotsea::equal(parallel_level.kernels[index].kernel,
                                    serial_level.kernels[index].kernel),
              "bounded parallel root extraction preserves kernel order");
    }

    // Add a term whose coefficient vanishes in F_193 but whose exponent does
    // not have the verified Weber weight.  This represents the same polynomial
    // over the fixture field while forcing the exact per-lift fallback.
    std::vector<oneshotsea::BivariateTerm> fallback_terms = phi5.terms();
    fallback_terms.push_back({0, 0, 193});
    const oneshotsea::SparseModularPolynomial fallback_phi5(
        5, std::move(fallback_terms));
    const auto fallback_level =
        oneshotsea::compute_weber_elkies_level_reference(
            curve, fallback_phi5, nullptr, 2);
    check(!fallback_level.timings.modular_root_orbit_reuse &&
              fallback_level.timings.modular_root_orbits == 24U &&
              fallback_level.timings.modular_root_reused_lifts == 0U &&
              fallback_level.timings.modular_root_workers == 2U,
          "unverified Weber covariance preserves the per-lift root fallback");
    check(fallback_level.compatible_source_lifts ==
                  serial_level.compatible_source_lifts &&
              fallback_level.kernels.size() == serial_level.kernels.size(),
          "orbit reuse and the exact per-lift fallback agree");
    for (std::size_t index = 0; index < serial_level.kernels.size(); ++index) {
        check(fallback_level.kernels[index].trace_residue ==
                      serial_level.kernels[index].trace_residue &&
                  fallback_level.kernels[index].neighbor_j ==
                      serial_level.kernels[index].neighbor_j &&
                  oneshotsea::equal(fallback_level.kernels[index].kernel,
                                    serial_level.kernels[index].kernel),
              "orbit reuse preserves the per-lift kernel and residue");
    }

    const auto disabled_level =
        oneshotsea::compute_weber_elkies_level_reference(
            curve, phi5, nullptr, 2, false);
    check(!disabled_level.timings.modular_root_orbit_reuse &&
              disabled_level.timings.modular_root_orbits == 24U &&
              disabled_level.timings.modular_root_reused_lifts == 0U &&
              disabled_level.timings.modular_root_workers == 2U,
          "the explicit orbit-reuse ablation selects the per-lift path");
    check(disabled_level.compatible_source_lifts ==
                  serial_level.compatible_source_lifts &&
              disabled_level.kernels.size() == serial_level.kernels.size(),
          "the explicit orbit-reuse ablation preserves Weber level results");

    const std::vector<mpz_class> mixed_lifts = {
        serial_level.compatible_source_lifts.front(), 0};
    const auto narrowed = oneshotsea::compute_weber_elkies_level_reference(
        curve, phi5, &mixed_lifts, 64);
    check(narrowed.compatible_source_lifts ==
              std::vector<mpz_class>({mixed_lifts.front()}),
          "Weber level safely narrows a source-lift state");
    check(narrowed.timings.modular_root_workers == mixed_lifts.size(),
          "Weber modular-root workers are capped by available work");

    const auto serial_result = oneshotsea::run_weber_sea_reference(
        curve, "data/modpoly/weber_f", 11, 1, {}, 1);
    const auto result = oneshotsea::run_weber_sea_reference(
        curve, "data/modpoly/weber_f", 11, 1, {}, 2);
    const auto scheduled_result = oneshotsea::run_weber_sea_reference(
        curve, "data/modpoly/weber_f", 11, 1, {}, 1, true, true,
        estimates);
    check(result.traces.has_value() &&
              *result.traces ==
                  std::vector<mpz_class>{oneshotsea::parse_integer("-6")},
          "stateful Weber SEA runner recovers the exact trace");
    check(serial_result.traces == result.traces &&
              serial_result.compatible_source_lifts ==
                  result.compatible_source_lifts,
          "SEA result is independent of its modular-root thread limit");
    check(scheduled_result.traces == result.traces &&
              scheduled_result.levels.size() == 2U &&
              scheduled_result.levels[0].ell == 11U &&
              scheduled_result.levels[1].ell == 5U,
          "alternate prime schedule preserves the exact trace and can stop earlier");

    const mpz_class brute_force_trace =
        curve.field().modulus() + 1 -
        oneshotsea::count_points_bruteforce(curve);
    check(brute_force_trace == -6,
          "SEA fixture trace agrees with exhaustive point counting");
    const oneshotsea::ExactTracePrior exact_prior(193, 22, 16);
    const std::vector<oneshotsea::WeberSeaLevelEstimate> prior_estimates = {
        {5, 1, 10}, {7, 10, 1}, {11, 9, 1}};
    const auto prior_result = oneshotsea::run_weber_sea_reference(
        curve, "data/modpoly/weber_f", 11, 1, {}, 1, true, true,
        prior_estimates, exact_prior);
    check(prior_result.traces == result.traces &&
              prior_result.traces ==
                  std::optional<std::vector<mpz_class>>(
                      std::vector<mpz_class>{brute_force_trace}),
          "prior-constrained SEA matches unprioritized SEA and brute force");
    check(prior_result.levels.size() == 2U &&
              prior_result.levels[0].ell == 7U &&
              prior_result.levels[1].ell == 5U &&
              std::none_of(
                  prior_result.levels.begin(), prior_result.levels.end(),
                  [](const oneshotsea::WeberSeaLevelRecord& level) {
                      return level.ell == 11U;
                  }),
          "composite exact prior skips every table prime sharing its modulus");
    check(prior_result.levels[0].exact_modulus == 22 &&
              prior_result.levels[0].constraint_modulus == 154 &&
              prior_result.levels[1].exact_modulus == 110 &&
              prior_result.levels[1].constraint_modulus == 770,
          "SEA level telemetry composes from identical prior constraints");

    bool rejected_foreign_prior = false;
    try {
        (void)oneshotsea::run_weber_sea_reference(
            curve, "data/modpoly/weber_f", 11, 1, {}, 1, true, true, {},
            oneshotsea::ExactTracePrior(197, 22, 0));
    } catch (const std::invalid_argument&) {
        rejected_foreign_prior = true;
    }
    check(rejected_foreign_prior,
          "SEA runner rejects an exact prior from a different field");
    check(std::all_of(
              result.levels.begin(), result.levels.end(),
              [](const oneshotsea::WeberSeaLevelRecord& record) {
                  return record.timings.modular_root_workers >= 1U &&
                         record.timings.modular_root_workers <= 2U;
              }),
          "every SEA level reports bounded modular-root workers");
    check(result.constraints.modulus() == 55 && result.levels.size() == 3,
          "stateful Weber SEA runner accumulates exact levels only");
    check(result.levels[1].ell == 7 && !result.levels[1].exact &&
              result.levels[1].atkin_projective_order == 4U &&
              result.levels[1].exact_trace_candidate_count == 11 &&
              result.levels[1].trace_candidate_count == 2 &&
              result.effective_constraints.modulus() == 385,
          "trusted classical factor degree safely constrains an empty Weber level");
    check(result.atkin_constraints.size() == 1U &&
              result.atkin_constraints.front().trace_residues ==
                  std::vector<std::uint64_t>({1U, 6U}),
          "SEA result retains auditable Atkin evidence");
    const mpz_class final_trace = -6;
    for (const auto& level : result.levels) {
        if (level.exact) {
            check(level.trace_residue.has_value() &&
                      mpz_fdiv_ui(final_trace.get_mpz_t(), level.ell) ==
                          *level.trace_residue,
                  "every stateful Weber exact residue matches the final trace");
        }
    }
    for (const auto& constraint : result.atkin_constraints) {
        const std::uint64_t final_residue =
            mpz_fdiv_ui(final_trace.get_mpz_t(), constraint.ell);
        check(std::binary_search(constraint.trace_residues.begin(),
                                 constraint.trace_residues.end(),
                                 final_residue),
              "every stateful Atkin residue set retains the final trace");
    }

    const auto atkin_screen = oneshotsea::run_weber_sea_reference(
        curve, "data/modpoly/weber_f", 7, 2, {}, 1);
    check(atkin_screen.traces ==
              std::optional<std::vector<mpz_class>>(
                  std::vector<mpz_class>{-6, -1}) &&
              atkin_screen.constraints.candidate_count() == 11,
          "Atkin evidence supplies a complete bounded early-screen set");
    const auto exact_gate = oneshotsea::run_weber_sea_reference(
        curve, "data/modpoly/weber_f", 7, 1, {}, 1);
    check(!exact_gate.traces.has_value() &&
              exact_gate.effective_constraints.candidate_count() == 2,
          "Atkin evidence cannot satisfy the exact unique-trace gate");
}

void test_early_abort() {
    check(oneshotsea::trial_smooth_part(2 * 2 * 3 * 7 * 11, 7) == 2 * 2 * 3 * 7,
          "trial smooth part");
    check(oneshotsea::certificate_lower_bound(101) == 17,
          "integer certificate lower bound");

    oneshotsea::TraceConstraints constraints(101);
    constraints.refine(41, {0});  // only trace zero lies in [-20,20]
    const auto partial = oneshotsea::screen_order_candidates(
        constraints, 4, 17, [](const mpz_class& order) {
            return oneshotsea::PartialN4SmoothPart{
                oneshotsea::trial_smooth_part(order, 3)};
        });
    check(partial.has_value() && partial->trace_count == 1,
          "early-abort exhaustive trace set");
    check(partial->survivors.size() == 2 && !partial->rejects_curve(),
          "partial smoothness cannot reject candidates");
    check(std::get<oneshotsea::PartialN4SmoothPart>(
              partial->survivors.front().smooth_part)
              .value == 6,
          "partial smoothness records the known divisor");

    const auto partial_sufficient = oneshotsea::screen_order_candidates(
        constraints, 4, 5, [](const mpz_class& order) {
            return oneshotsea::PartialN4SmoothPart{
                oneshotsea::trial_smooth_part(order, 3)};
        });
    check(partial_sufficient.has_value() &&
              partial_sufficient->survivors.size() == 2,
          "partial smoothness above the bound retains both sides");

    // Adversarial regression: N=102 has partial 3-smooth part 6 <= L=17,
    // but its exact smooth part through B=7^4=2401 is 102 > L.
    const auto completed = oneshotsea::screen_order_candidates(
        constraints, 4, 17, [](const mpz_class& order) {
            return oneshotsea::ExactN4SmoothPart{
                oneshotsea::trial_smooth_part(order, 2401)};
        });
    check(completed.has_value() && completed->survivors.size() == 2,
          "exact full-bound smoothness retains adversarial candidates");
    check(std::get<oneshotsea::ExactN4SmoothPart>(
              completed->survivors.front().smooth_part)
              .value == 102,
          "exact full-bound smoothness includes the missing factor");

    const auto rejected = oneshotsea::screen_order_candidates(
        constraints, 4, 200, [](const mpz_class& order) {
            return oneshotsea::ExactN4SmoothPart{
                oneshotsea::trial_smooth_part(order, 2401)};
        });
    check(rejected.has_value() && rejected->rejects_curve(),
          "exact full-bound evidence can reject both sides");

    oneshotsea::TraceConstraints many(101);
    check(!oneshotsea::screen_order_candidates(
               many, 4, 1, [](const mpz_class&) {
                   return oneshotsea::PartialN4SmoothPart{1};
               })
               .has_value(),
          "early-abort cap prevents incomplete enumeration");
}

void test_schoof_residues() {
    for (const unsigned long p : {11UL, 17UL, 23UL, 29UL}) {
        for (unsigned long index = 0; index < 3; ++index) {
            const auto curve = oneshotsea::deterministic_curve(p, 0x5c400f, index);
            if (curve.is_singular()) {
                continue;
            }
            const mpz_class trace = p + 1 - oneshotsea::count_points_bruteforce(curve);
            for (const std::uint64_t ell : {3U, 5U, 7U}) {
                if (ell == p) {
                    continue;
                }
                check(oneshotsea::schoof_trace_mod_ell(curve, ell) ==
                          mpz_fdiv_ui(trace.get_mpz_t(), ell),
                      "native Schoof trace residue");
            }
        }
    }

    for (const unsigned long p : {101UL, 1009UL}) {
        for (unsigned long index = 0; index < 2; ++index) {
            const auto curve = oneshotsea::deterministic_curve(p, 0xc0ffee, index);
            if (curve.is_singular()) {
                continue;
            }
            const mpz_class expected_order = oneshotsea::count_points_bruteforce(curve);
            const auto count = oneshotsea::schoof_count_reference(curve, 13);
            check(count.order == expected_order, "complete native Schoof point count");
            check(count.trace == mpz_class(p) + 1 - expected_order,
                  "complete native Schoof trace");
            check(!count.levels.empty(), "complete native Schoof levels");
        }
    }

    const auto curve = oneshotsea::deterministic_curve(101, 0xc0ffee, 0);
    bool rejected_large_ell = false;
    try {
        static_cast<void>(oneshotsea::schoof_trace_mod_ell(curve, 37));
    } catch (const std::invalid_argument&) {
        rejected_large_ell = true;
    }
    check(rejected_large_ell, "reference Schoof rejects impractical ell");
}

void test_elkies_residues() {
    const auto phi3 = oneshotsea::SparseModularPolynomial::load(
        3, "data/modpoly/j/phi_3.txt");

    const auto rejects_eigenvalue_input = [](const mpz_class& prime,
                                              std::uint64_t ell,
                                              std::uint64_t eigenvalue) {
        try {
            static_cast<void>(oneshotsea::trace_residue_from_eigenvalue(
                prime, ell, eigenvalue));
            return false;
        } catch (const std::invalid_argument&) {
            return true;
        }
    };
    check(rejects_eigenvalue_input(101, 4, 1),
          "eigenvalue helper rejects composite ell");
    check(rejects_eigenvalue_input(91, 5, 1),
          "eigenvalue helper rejects composite characteristic");
    check(rejects_eigenvalue_input(-101, 5, 1),
          "eigenvalue helper rejects negative characteristic");
    check(rejects_eigenvalue_input(5, 5, 1),
          "eigenvalue helper rejects ell equal to characteristic");
    check(rejects_eigenvalue_input(101, 5, 0),
          "eigenvalue helper rejects zero eigenvalue");

    // A coarse Phi_3 root is not by itself a sound Elkies classification in
    // supersingular/collision cases.  This curve has two rational modular
    // neighbors but no rational degree-one factor of psi_3.
    const oneshotsea::Curve collision(oneshotsea::Field(19), 8, 14);
    const mpz_class collision_order =
        oneshotsea::count_points_bruteforce(collision);
    check(collision_order == 20, "supersingular collision fixture point count");
    check(mpz_class(20) - collision_order == 0,
          "collision fixture has trace zero");
    check(oneshotsea::schoof_trace_mod_ell(collision, 3) == 0,
          "native Schoof recovers the supersingular trace residue");
    check(!oneshotsea::linear_roots(
               phi3.evaluate_x(collision.field(), collision.j_invariant()))
               .empty(),
          "collision fixture has rational Phi_3 roots");
    check(oneshotsea::elkies_kernels_reference(collision, phi3).empty(),
          "stable kernel factors override coarse Phi_3 roots");

    std::size_t elkies_cases = 0;
    std::size_t atkin_cases = 0;
    for (const unsigned long p :
         {5UL, 7UL, 11UL, 13UL, 17UL, 19UL, 23UL, 29UL, 31UL, 37UL, 101UL}) {
        for (unsigned long index = 0; index < 10; ++index) {
            const auto curve = oneshotsea::deterministic_curve(p, 0xe1c1e5, index);
            if (curve.is_singular()) {
                continue;
            }
            const mpz_class order = oneshotsea::count_points_bruteforce(curve);
            const mpz_class trace = mpz_class(p) + 1 - order;
            const std::uint64_t residue = mpz_fdiv_ui(trace.get_mpz_t(), 3);
            const std::uint64_t p_mod_3 = p % 3;
            const std::uint64_t discriminant =
                (residue * residue + 3 - (4 * p_mod_3) % 3) % 3;
            const bool expected_elkies = discriminant != 2;

            const auto kernels = oneshotsea::elkies_kernels_reference(curve, phi3);
            check(!kernels.empty() == expected_elkies,
                  "ell=3 Elkies/Atkin classification");
            const auto exact = oneshotsea::elkies_trace_residue_reference(curve, phi3);
            check(exact.has_value() == expected_elkies,
                  "ell=3 exact residue availability");
            if (!expected_elkies) {
                ++atkin_cases;
                continue;
            }
            ++elkies_cases;
            check(*exact == residue, "ell=3 exact Elkies trace residue");
            for (const auto& kernel : kernels) {
                check(kernel.ell == 3 && kernel.kernel.degree() == 1,
                      "ell=3 kernel degree");
                check(kernel.trace_residue == residue,
                      "ell=3 per-kernel trace residue");
                check(kernel.neighbor_j == kernel.codomain.j_invariant(),
                      "ell=3 normalized codomain j-invariant");
                check(oneshotsea::count_points_bruteforce(kernel.codomain) == order,
                      "ell=3 Velu codomain order");
            }
        }
    }
    check(elkies_cases >= 30 && atkin_cases >= 20,
          "ell=3 differential coverage includes Elkies and Atkin cases");

    const auto check_general_level = [](std::uint64_t ell,
                                        const std::vector<unsigned long>& primes) {
        std::size_t checked = 0;
        std::size_t elkies = 0;
        std::size_t atkin = 0;
        for (const unsigned long p : primes) {
            if (p == ell) {
                continue;
            }
            for (unsigned long a = 0; a < p; ++a) {
                for (unsigned long b = 0; b < p; ++b) {
                    const oneshotsea::Curve curve(oneshotsea::Field(p), a, b);
                    if (curve.is_singular()) {
                        continue;
                    }
                    const mpz_class order = oneshotsea::count_points_bruteforce(curve);
                    const mpz_class trace = mpz_class(p) + 1 - order;
                    const std::uint64_t residue = mpz_fdiv_ui(trace.get_mpz_t(), ell);
                    const std::uint64_t discriminant =
                        (residue * residue + ell - (4 * (p % ell)) % ell) % ell;
                    const bool expected_elkies =
                        discriminant == 0 ||
                        small_pow_mod(discriminant, (ell - 1U) / 2U, ell) == 1;
                    const auto kernels =
                        oneshotsea::elkies_kernels_division_reference(curve, ell);
                    check(!kernels.empty() == expected_elkies,
                          "general Elkies/Atkin classification");
                    const auto exact =
                        oneshotsea::elkies_trace_residue_division_reference(curve, ell);
                    check(exact.has_value() == expected_elkies,
                          "general exact residue availability");
                    if (!expected_elkies) {
                        ++atkin;
                        ++checked;
                        continue;
                    }
                    ++elkies;
                    check(*exact == residue, "general exact Elkies trace residue");
                    for (const auto& kernel : kernels) {
                        check(kernel.kernel.degree() ==
                                  static_cast<int>((ell - 1U) / 2U),
                              "general Elkies kernel degree");
                        check(kernel.trace_residue == residue,
                              "general per-kernel trace residue");
                        check(kernel.neighbor_j == kernel.codomain.j_invariant(),
                              "general normalized codomain j-invariant");
                        check(oneshotsea::count_points_bruteforce(kernel.codomain) == order,
                              "general Velu codomain order");
                    }
                    ++checked;
                }
            }
        }
        check(checked >= 100 && elkies >= 30 && atkin >= 30,
              "general Elkies differential coverage");
    };
    check_general_level(5, {7, 11, 13});
    check_general_level(7, {5, 11});

    const auto phi5 = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/j/phi_5.txt");
    const auto phi7 = oneshotsea::SparseModularPolynomial::load(
        7, "data/modpoly/j/phi_7.txt");
    const auto weber_phi5 = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/weber_f/phi_5.txt");
    const auto weber_phi7 = oneshotsea::SparseModularPolynomial::load(
        7, "data/modpoly/weber_f/phi_7.txt");
    const auto weber_phi11 = oneshotsea::SparseModularPolynomial::load(
        11, "data/modpoly/weber_f/phi_11.txt");
    std::size_t normalized_level5_checks = 0;
    std::size_t normalized_level7_checks = 0;
    std::size_t bmss_level5_checks = 0;
    std::size_t bmss_level7_checks = 0;
    for (unsigned long index = 0; index < 12; ++index) {
        const auto curve = oneshotsea::deterministic_curve(101, 0xe1c1e5, index);
        if (curve.is_singular()) {
            continue;
        }
        const auto kernels = oneshotsea::elkies_kernels_reference(curve, phi5);
        const mpz_class trace =
            102 - oneshotsea::count_points_bruteforce(curve);
        if (!kernels.empty()) {
            check(kernels.front().trace_residue ==
                      mpz_fdiv_ui(trace.get_mpz_t(), 5),
                  "Phi_5-validated exact residue");
            for (const auto& kernel : kernels) {
                try {
                    const auto codomain =
                        oneshotsea::normalized_codomain_from_classical_modpoly(
                            curve, phi5, kernel.neighbor_j);
                    check(codomain.a() == kernel.codomain.a() &&
                              codomain.b() == kernel.codomain.b(),
                          "modular derivatives recover normalized level-5 codomain");
                    ++normalized_level5_checks;
                    if (bmss_level5_checks < 2) {
                        const auto reconstructed =
                            oneshotsea::bmss_isogeny_reference(curve, codomain, 5);
                        check(oneshotsea::equal(reconstructed.kernel, kernel.kernel),
                              "BMSS reconstructs the level-5 kernel");
                        ++bmss_level5_checks;
                    }
                } catch (const std::domain_error&) {
                    // Exceptional invariants and repeated modular roots are
                    // explicitly outside the derivative formula's domain.
                }
            }
        }
        const auto kernels7 = oneshotsea::elkies_kernels_reference(curve, phi7);
        if (!kernels7.empty()) {
            check(kernels7.front().trace_residue ==
                      mpz_fdiv_ui(trace.get_mpz_t(), 7),
                  "Phi_7-validated exact residue");
            for (const auto& kernel : kernels7) {
                try {
                    const auto codomain =
                        oneshotsea::normalized_codomain_from_classical_modpoly(
                            curve, phi7, kernel.neighbor_j);
                    check(codomain.a() == kernel.codomain.a() &&
                              codomain.b() == kernel.codomain.b(),
                          "modular derivatives recover normalized level-7 codomain");
                    ++normalized_level7_checks;
                    if (bmss_level7_checks < 2) {
                        const auto reconstructed =
                            oneshotsea::bmss_isogeny_reference(curve, codomain, 7);
                        check(oneshotsea::equal(reconstructed.kernel, kernel.kernel),
                              "BMSS reconstructs the level-7 kernel");
                        ++bmss_level7_checks;
                    }
                } catch (const std::domain_error&) {
                    // See the corresponding level-5 exceptional cases above.
                }
            }
        }
    }
    check(normalized_level5_checks >= 2 && normalized_level7_checks >= 2,
          "classical modular derivatives recover level-5 and level-7 codomains");
    check(bmss_level5_checks >= 2 && bmss_level7_checks >= 2,
          "BMSS reconstructs level-5 and level-7 kernels");

    const auto compare_weber_path = [](const oneshotsea::Curve& curve,
                                       const auto& classical,
                                       const auto& weber) {
        const auto classical_kernels =
            oneshotsea::elkies_kernels_bmss_reference(curve, classical);
        const auto weber_kernels =
            oneshotsea::elkies_kernels_weber_bmss_reference(curve, weber);
        check(weber_kernels.size() == classical_kernels.size(),
              "Weber and classical BMSS kernel counts agree");
        for (const auto& expected : classical_kernels) {
            check(std::any_of(
                      weber_kernels.begin(), weber_kernels.end(),
                      [&expected](const auto& candidate) {
                          return oneshotsea::equal(
                              expected.kernel, candidate.kernel);
                      }),
                  "Weber and classical BMSS kernels agree");
        }
    };
    compare_weber_path(
        oneshotsea::Curve(oneshotsea::Field(109), 82, 45),
        phi5, weber_phi5);
    compare_weber_path(
        oneshotsea::Curve(oneshotsea::Field(157), 37, 13),
        phi7, weber_phi7);

    // This Weber level has two distinct normalized codomains: one reconstructs
    // the unique valid kernel and one is an incompatible class-invariant lift.
    // The latter must be skipped through the narrow BMSS exception without
    // suppressing unrelated reconstruction failures.
    const auto mixed_weber_level =
        oneshotsea::compute_weber_elkies_level_reference(
            oneshotsea::Curve(oneshotsea::Field(277), 6, 10),
            weber_phi11);
    check(mixed_weber_level.kernels.size() == 1 &&
              mixed_weber_level.timings.distinct_codomains == 2 &&
              mixed_weber_level.timings.eigenvalue_attempts == 1,
          "Weber skips only the typed incompatible BMSS neighbor");

    // BMSS, Section 5.1: the published worked fastElkies example over F_101.
    const oneshotsea::Curve worked_source(oneshotsea::Field(101), 1, 1);
    const oneshotsea::Curve worked_codomain(oneshotsea::Field(101), 75, 16);
    const auto worked =
        oneshotsea::bmss_isogeny_reference(worked_source, worked_codomain, 11);
    check(oneshotsea::equal(
              worked.kernel,
              oneshotsea::Poly(oneshotsea::Field(101), {5, 97, 24, 89, 76, 1})),
          "BMSS worked-example kernel polynomial");
    oneshotsea::validate_rational_isogeny_reference(
        worked_source, worked_codomain, 11, worked);

    // Multiplying the identity map x/1 by h^2/h^2 leaves the cross-multiplied
    // isogeny equation unchanged.  With deg(h)=5, the unreduced numerator has
    // claimed degree 11, the denominator is h^2, and h is monic square-free:
    // coprimality is the one condition that exposes this counterfeit.
    const oneshotsea::Poly worked_x =
        oneshotsea::Poly::x(worked_source.field());
    oneshotsea::Poly counterfeit_kernel =
        oneshotsea::Poly::constant(worked_source.field(), 1);
    for (unsigned long root = 1; root <= 5; ++root) {
        counterfeit_kernel = oneshotsea::mul(
            counterfeit_kernel,
            oneshotsea::sub(
                worked_x,
                oneshotsea::Poly::constant(worked_source.field(), root)));
    }
    const oneshotsea::Poly counterfeit_denominator =
        oneshotsea::mul(counterfeit_kernel, counterfeit_kernel);
    const oneshotsea::BmssIsogenyResult counterfeit{
        counterfeit_kernel,
        oneshotsea::mul(worked_x, counterfeit_denominator),
        counterfeit_denominator};
    bool rejected_common_factor = false;
    try {
        static_cast<void>(oneshotsea::try_frobenius_eigenvalue_from_isogeny(
            worked_source, worked_source, counterfeit, 11));
    } catch (const oneshotsea::BmssIncompatibleNeighborError&) {
        rejected_common_factor = false;
    } catch (const std::runtime_error& error) {
        rejected_common_factor =
            std::string(error.what()).find("not coprime") != std::string::npos;
    }
    check(rejected_common_factor,
          "unexpected proof-object validation is not BMSS incompatibility");

    bool generic_runtime_propagated = false;
    try {
        try {
            throw std::runtime_error("simulated internal validation failure");
        } catch (const oneshotsea::BmssIncompatibleNeighborError&) {
            check(false, "generic runtime error was mistaken for BMSS incompatibility");
        }
    } catch (const std::runtime_error&) {
        generic_runtime_propagated = true;
    }
    check(generic_runtime_propagated,
          "narrow BMSS catch propagates unexpected runtime errors");

    check(oneshotsea::elkies_kernels_division_reference(
              oneshotsea::Curve(oneshotsea::Field(19), 0, 4), 5)
              .size() == 6,
          "scalar Frobenius recovers all level-5 kernels");
    check(oneshotsea::elkies_kernels_division_reference(
              oneshotsea::Curve(oneshotsea::Field(37), 0, 3), 7)
              .size() == 8,
          "scalar Frobenius recovers all level-7 kernels");

    const oneshotsea::Curve target_curve(oneshotsea::Field(target_prime()), 2, 3);
    for (const std::uint64_t ell : {5U, 7U}) {
        const auto exact =
            oneshotsea::elkies_trace_residue_division_reference(target_curve, ell);
        check(exact.has_value(), "target-field curve has an Elkies kernel");
        check(*exact == oneshotsea::schoof_trace_mod_ell(target_curve, ell),
              "target-field exact Elkies residue agrees with Schoof");
    }

    const oneshotsea::Curve target_weber_curve =
        oneshotsea::deterministic_curve(target_prime(), 45077, 0);
    const auto target_weber =
        oneshotsea::elkies_trace_residue_weber_bmss_reference(
            target_weber_curve, weber_phi7);
    check(target_weber.has_value() &&
              *target_weber ==
                  oneshotsea::schoof_trace_mod_ell(target_weber_curve, 7),
          "target-field Weber residue agrees with Schoof");

    const auto weber_phi37 = oneshotsea::SparseModularPolynomial::load(
        37, "data/modpoly/weber_f/phi_37.txt");
    const auto level37 =
        oneshotsea::elkies_trace_residue_weber_bmss_reference(
            oneshotsea::Curve(oneshotsea::Field(1009), 799, 474),
            weber_phi37);
    check(level37.has_value() && *level37 == 0,
          "level-37 Weber residue works above the old division-polynomial cap");

    const auto target_level37 =
        oneshotsea::elkies_trace_residue_weber_bmss_reference(
            oneshotsea::deterministic_curve(target_prime(), 45077, 2),
            weber_phi37);
    check(target_level37.has_value() && *target_level37 == 29,
          "416-bit target level-37 Weber residue matches the Magma oracle");
}

}  // namespace

int main() {
    try {
        test_field();
        test_polynomial();
        test_curves();
        test_modular_polynomials();
        test_trace_constraints();
        test_weber_sea_runner();
        test_early_abort();
        test_schoof_residues();
        test_elkies_residues();
        std::cout << "core tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
