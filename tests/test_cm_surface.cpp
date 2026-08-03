#include "oneshotsea/atkin.hpp"
#include "oneshotsea/cm_surface.hpp"
#include "oneshotsea/early_abort.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/schoof.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/weber.hpp"
#include "oneshotsea/weber_cm_surface.hpp"

#include <algorithm>
#include <cstdlib>
#include <future>
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

template <class Function>
bool rejects(Function&& function) {
    try {
        function();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

mpz_class target_prime() {
    return oneshotsea::parse_integer(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237");
}

std::vector<mpz_class> hilbert_minus_71_coefficients() {
    return {
        mpz_class("737707086760731113357714241006081263"),
        mpz_class("-425319473946139603274605151187659"),
        mpz_class("5138800366453976780323726329446"),
        mpz_class("-823534263439730779968091389"),
        mpz_class("98394038810047812049302"),
        mpz_class("-3091990138604570"),
        mpz_class("313645809715"),
        1};
}

std::vector<mpz_class> weber_class_minus_71_coefficients() {
    // x^7+x^6-x^5-x^4-x^3+x^2+2x-1, in ascending order.
    return {-1, 2, 1, -1, -1, -1, 1, 1};
}

std::vector<mpz_class> hilbert_minus_567_coefficients() {
    // Independent PARI polclass(-567) fixture, in ascending order.  PARI is
    // not called by the native producer or test.
    return {
        mpz_class("94939827859226638699860333760064580014012314613444064975795197294661495884252335192286409437656402587890625"),
        mpz_class("-136351242616971469089714435801649521116139577391138661488843079813096166275700624566525220870971679687500"),
        mpz_class("240465238298468556087654667700270270240230655372881479188875214855447931845069753006100654602050781250"),
        mpz_class("-73626788879580702656099674756535616555593945585488278614260314081638644091947942972183227539062500"),
        mpz_class("14310581161051907350069149682581166370429155482109353136032876236615487056880891323089599609375"),
        mpz_class("-800043754571980290560208886060150540542476890667232606307634222648153448523521423339843750"),
        mpz_class("44166492901213444728376505206637577274121936753006174243699437199037017761230468750000"),
        mpz_class("1662002466792826922222915127833186081228900308110597434224630438200227600097656250"),
        mpz_class("17743278050524501433710352182113369817897329333856490925962578368896484375"),
        mpz_class("94712976528872464608415668346435129318235725555672703216994140625"),
        mpz_class("57653498811281323356204033749720913703125"),
        mpz_class("307754734372799631847595891663625"),
        1};
}

void test_three_power_class_polynomial() {
    std::size_t family_levels = 0U;
    for (std::uint64_t level = 5U; level <= 997U; level += 2U) {
        if (!oneshotsea::is_prime_u64(level)) {
            continue;
        }
        const auto family_order =
            oneshotsea::derive_three_power_suitable_order(
                static_cast<unsigned>(level));
        check(family_order.fundamental_discriminant() == -7 &&
                  family_order.discriminant() ==
                      -7 * family_order.conductor() *
                          family_order.conductor() &&
                  family_order.class_number() >= level + 2U &&
                  family_order.class_number() <= 4U * level,
              "three-power family satisfies every tested suitable-order interval");
        ++family_levels;
    }
    check(family_levels == 166U,
          "three-power family covers every odd prime level through 997");

    const auto order = oneshotsea::derive_three_power_suitable_order(7);
    check(order.fundamental_discriminant() == -7 &&
              order.conductor() == 9 && order.discriminant() == -567 &&
              order.class_number() == 12U,
          "ell=7 selects the cited D=-7*3^(2n) suitable order");
    const auto selected = oneshotsea::select_sutherland_crt_primes(
        order, 1009, 1, 1000);
    check(selected.size() == 1U && selected.front().prime == 27847 &&
              selected.front().trace == 16 &&
              selected.front().volcano_parameter == 2,
          "D=-567 selector produces the retained p=27847 witness");

    const auto class_polynomial =
        oneshotsea::derive_three_power_class_polynomial_mod_prime(
            order, selected.front());
    const oneshotsea::Field field(selected.front().prime);
    std::vector<mpz_class> expected;
    for (const mpz_class& coefficient :
         hilbert_minus_567_coefficients()) {
        expected.push_back(field.normalize(coefficient));
    }
    check(class_polynomial.discriminant() == order.discriminant() &&
              class_polynomial.auxiliary_prime() == selected.front().prime &&
              class_polynomial.polynomial().coefficients() == expected &&
              class_polynomial.polynomial().degree() == 12 &&
              oneshotsea::gcd(
                  class_polynomial.polynomial(),
                  class_polynomial.polynomial().derivative()).degree() == 0 &&
              oneshotsea::linear_roots(class_polynomial.polynomial()).size() ==
                  12U,
          "fixed-Phi_3 ring-class tower exactly reproduces H_-567 mod p");

    const auto surfaces = oneshotsea::enumerate_cm_interpolation_surfaces(
        order, selected.front(), class_polynomial, 27847);
    check(surfaces.level() == 7U &&
              surfaces.auxiliary_prime() == 27847 &&
              surfaces.surface_curves().size() == 12U &&
              surfaces.exact_group_order() == 27832 &&
              surfaces.horizontal_edges_per_surface() == 1U,
          "ramified ell=7 CM surface admits one horizontal edge per root");
    for (const auto& surface : surfaces.surface_curves()) {
        std::size_t horizontal = 0U;
        for (const auto& edge : surface.edges) {
            horizontal += edge.codomain_on_surface ? 1U : 0U;
        }
        check(surface.edges.size() == 8U && horizontal == 1U,
              "ramified ell=7 surface has one horizontal and seven descending edges");
    }
    const oneshotsea::Field target_field(193);
    const std::vector<mpz_class> target_powers =
        oneshotsea::lifted_target_powers(target_field, 20, 8);
    const auto native_residue =
        oneshotsea::specialize_classical_from_cm_surfaces(
            surfaces, target_powers);
    const auto phi7 = oneshotsea::SparseModularPolynomial::load(
        7, "data/modpoly/j/phi_7.txt");
    const auto table_residue =
        oneshotsea::specialize_sparse_modpoly_for_crt_reference(
            phi7, target_powers, selected.front().prime);
    check(native_residue.value_coefficients ==
                  table_residue.value_coefficients &&
              native_residue.x_derivative_coefficients ==
                  table_residue.x_derivative_coefficients,
          "authenticated ramified ell=7 producer matches both Phi_7 channels");

    const auto reconstructed =
        oneshotsea::reconstruct_classical_specialization_from_cm(
            order, target_field, 20, 100000, 1000000);
    const auto expected_specialization =
        phi7.specialize_x_with_derivative(target_field, 20);
    check(reconstructed.prime_count > 1U &&
              reconstructed.crt_product >
                  4 * reconstructed.coefficient_abs_bound &&
              oneshotsea::equal(
                  reconstructed.specialization.value(),
                  expected_specialization.value()) &&
              oneshotsea::equal(
                  reconstructed.specialization.x_derivative(),
                  expected_specialization.x_derivative()),
          "full authenticated ell=7 CM/CRT path matches the target-field Phi_7 oracle");

    const oneshotsea::Curve target_curve =
        oneshotsea::short_weierstrass_curve_from_j(target_field, 20);
    const auto table_trace =
        oneshotsea::elkies_trace_residue_bmss_reference(target_curve, phi7);
    const auto direct_trace =
        oneshotsea::elkies_trace_residue_bmss_specialized_reference(
            target_curve, reconstructed.specialization);
    const auto direct_kernels =
        oneshotsea::elkies_kernels_bmss_specialized_reference(
            target_curve, reconstructed.specialization);
    check(direct_trace.has_value() && direct_trace == table_trace &&
              direct_trace.has_value() == !direct_kernels.empty(),
          "direct classical specialization feeds a positive BMSS/Frobenius trace path");
    const oneshotsea::Curve wrong_source =
        oneshotsea::short_weierstrass_curve_from_j(target_field, 21);
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::elkies_kernels_bmss_specialized_reference(
                      wrong_source, reconstructed.specialization));
          }),
          "classical specialization consumer rejects a different source curve");

    const oneshotsea::Curve atkin_curve =
        oneshotsea::short_weierstrass_curve_from_j(target_field, 4);
    const auto atkin_reconstructed =
        oneshotsea::reconstruct_classical_specialization_from_cm(
            order, target_field, 4, 100000, 1000000);
    const auto direct_atkin =
        oneshotsea::classical_atkin_constraint_reference(
            atkin_curve, atkin_reconstructed.specialization);
    const auto table_atkin =
        oneshotsea::classical_atkin_constraint_reference(atkin_curve, phi7);
    check(!oneshotsea::elkies_trace_residue_bmss_specialized_reference(
               atkin_curve, atkin_reconstructed.specialization).has_value() &&
              direct_atkin.has_value() && table_atkin.has_value() &&
              direct_atkin->projective_order ==
                  table_atkin->projective_order &&
              direct_atkin->trace_residues == table_atkin->trace_residues,
          "direct no-root specialization yields the same certified Atkin constraint");
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::classical_atkin_constraint_reference(
                      target_curve, atkin_reconstructed.specialization));
          }),
          "direct Atkin consumer rejects a mismatched source curve");

    const auto unrelated_order =
        oneshotsea::validate_sutherland_suitable_order(5, -71, 1);
    const auto unrelated_witness =
        oneshotsea::select_sutherland_crt_primes(
            unrelated_order, 1009, 1, 1000).front();
    auto malformed_witness = selected.front();
    malformed_witness.trace += 14;
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::derive_three_power_class_polynomial_mod_prime(
                      unrelated_order, unrelated_witness));
          }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::derive_three_power_class_polynomial_mod_prime(
                          order, malformed_witness));
              }) &&
              rejects([] {
                  static_cast<void>(
                      oneshotsea::derive_three_power_suitable_order(3));
              }),
          "three-power HCP producer rejects unrelated orders and levels");
}

void test_p125_classical_direct_path() {
    const oneshotsea::Field field(target_prime());
    const oneshotsea::Curve curve(field, 2, 3);
    const auto order = oneshotsea::derive_three_power_suitable_order(7);
    const auto context = oneshotsea::prepare_classical_direct_level_context(
        order, field, 1000000, 1000000);
    const auto reconstructed =
        oneshotsea::reconstruct_classical_specialization_from_prepared_context(
            context, field, curve.j_invariant());
    const auto phi7 = oneshotsea::SparseModularPolynomial::load(
        7, "data/modpoly/j/phi_7.txt");
    const auto expected = phi7.specialize_x_with_derivative(
        field, curve.j_invariant());
    check(reconstructed.prime_count == 37U &&
              reconstructed.crt_product >
                  4 * reconstructed.coefficient_abs_bound &&
              oneshotsea::equal(
                  reconstructed.specialization.value(), expected.value()) &&
              oneshotsea::equal(
                  reconstructed.specialization.x_derivative(),
                  expected.x_derivative()),
          "p125 direct classical path reconstructs both Phi_7 channels from 37 primes");

    const auto direct_trace =
        oneshotsea::elkies_trace_residue_bmss_specialized_reference(
            curve, reconstructed.specialization);
    const auto table_trace =
        oneshotsea::elkies_trace_residue_bmss_reference(curve, phi7);
    check(direct_trace.has_value() && *direct_trace == 5U &&
              direct_trace == table_trace &&
              oneshotsea::linear_roots(
                  reconstructed.specialization.value()).size() == 2U,
          "p125 direct Phi_7 specialization yields the independently validated trace residue 5");

    oneshotsea::TraceConstraints initial(field.modulus());
    oneshotsea::WeberSeaResult direct_sea{
        initial, initial, {}, {}, {}, {}, std::nullopt, {}};
    oneshotsea::extend_sea_with_classical_direct(
        curve, direct_sea, {5U, 7U, 11U}, 64U, 1000000U, 1000000U);
    const auto phi5 = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/j/phi_5.txt");
    const auto table_trace5 =
        oneshotsea::elkies_trace_residue_bmss_reference(curve, phi5);
    const auto schoof_trace11 =
        oneshotsea::schoof_trace_mod_ell(curve, 11U);
    check(direct_sea.classical_direct_levels.size() == 3U &&
              direct_sea.classical_direct_levels[0].ell == 5U &&
              direct_sea.classical_direct_levels[0].exact &&
              direct_sea.classical_direct_levels[0].trace_residue == 3U &&
              direct_sea.classical_direct_levels[0].auxiliary_prime_count ==
                  34U &&
              direct_sea.classical_direct_levels[1].ell == 7U &&
              direct_sea.classical_direct_levels[1].exact &&
              direct_sea.classical_direct_levels[1].trace_residue == 5U &&
              direct_sea.classical_direct_levels[1].auxiliary_prime_count ==
                  37U &&
              direct_sea.classical_direct_levels[2].ell == 11U &&
              direct_sea.classical_direct_levels[2].exact &&
              direct_sea.classical_direct_levels[2].trace_residue == 10U &&
              direct_sea.classical_direct_levels[2].auxiliary_prime_count ==
                  43U &&
              direct_sea.constraints.modulus() == 385 &&
              direct_sea.effective_constraints.modulus() == 385 &&
              table_trace5 == 3U && schoof_trace11 == 10U &&
              !direct_sea.traces.has_value(),
          "p125 direct SEA runner retains three exact levels without claiming completion");
}

void test_prepared_classical_context_equivalence() {
    const oneshotsea::Field field(193);
    const auto order = oneshotsea::derive_three_power_suitable_order(7);
    const auto context = oneshotsea::prepare_classical_direct_level_context(
        order, field, 100000U, 1000000U);
    const auto parallel_context =
        oneshotsea::prepare_classical_direct_level_context(
            order, field, 100000U, 1000000U, 4U);
    check(context.level() == 7U && context.target_modulus() == 193 &&
              context.order_discriminant() == order.discriminant() &&
              context.class_number() == order.class_number() &&
              context.auxiliary_prime_count() != 0U,
          "prepared direct context retains its checked target and CM metadata");

    const std::array<mpz_class, 2> invariants = {20, 4};
    for (const mpz_class& invariant : invariants) {
        const auto one_off =
            oneshotsea::reconstruct_classical_specialization_from_cm(
                order, field, invariant, 100000U, 1000000U);
        const auto prepared =
            oneshotsea::reconstruct_classical_specialization_from_prepared_context(
                context, field, invariant);
        const auto parallel =
            oneshotsea::reconstruct_classical_specialization_from_prepared_context(
                parallel_context, field, invariant);
        check(prepared.prime_count == one_off.prime_count &&
                  prepared.crt_product == one_off.crt_product &&
                  prepared.coefficient_abs_bound ==
                      one_off.coefficient_abs_bound &&
                  oneshotsea::equal(prepared.specialization.value(),
                                    one_off.specialization.value()) &&
                  oneshotsea::equal(
                      prepared.specialization.x_derivative(),
                      one_off.specialization.x_derivative()),
              "prepared context reproduces both one-off specialization channels for each target j");
        check(parallel.prime_count == prepared.prime_count &&
                  parallel.crt_product == prepared.crt_product &&
                  parallel.coefficient_abs_bound ==
                      prepared.coefficient_abs_bound &&
                  oneshotsea::equal(parallel.specialization.value(),
                                    prepared.specialization.value()) &&
                  oneshotsea::equal(
                      parallel.specialization.x_derivative(),
                      prepared.specialization.x_derivative()),
              "parallel preparation preserves deterministic CRT metadata and both specialization channels");
    }

    const oneshotsea::Curve elkies_curve =
        oneshotsea::short_weierstrass_curve_from_j(field, 20);
    const oneshotsea::Curve atkin_curve =
        oneshotsea::short_weierstrass_curve_from_j(field, 4);
    const auto sea_context = oneshotsea::make_classical_direct_sea_context(
        field, {7U}, 100000U, 1000000U);
    check(sea_context.target_modulus() == field.modulus() &&
              sea_context.levels() == std::vector<std::uint64_t>{7U} &&
              sea_context.maximum_prime_candidates() == 100000U &&
              sea_context.maximum_x_candidates_per_surface() == 1000000U &&
              sea_context.prepared_context_count() == 0U &&
              sea_context.preparation_us() == 0U,
          "prepared SEA schedule context binds target, levels, and execution caps");

    const mpz_class elkies_trace =
        field.modulus() + 1 -
        oneshotsea::count_points_bruteforce(elkies_curve);
    oneshotsea::TraceConstraints complete_initial(field.modulus());
    complete_initial.refine_exact(
        101U, mpz_fdiv_ui(elkies_trace.get_mpz_t(), 101U));
    oneshotsea::WeberSeaResult already_complete{
        complete_initial, complete_initial, {}, {}, {}, {}, std::nullopt, {}};
    oneshotsea::extend_sea_with_prepared_classical_direct(
        elkies_curve, already_complete, sea_context, 1U);
    check(already_complete.traces.has_value() &&
              already_complete.traces->size() == 1U &&
              sea_context.prepared_context_count() == 0U,
          "an already-complete retained state never prepares an unused direct level");

    for (const oneshotsea::Curve* curve : {&elkies_curve, &atkin_curve}) {
        oneshotsea::TraceConstraints initial(field.modulus());
        oneshotsea::WeberSeaResult one_off{
            initial, initial, {}, {}, {}, {}, std::nullopt, {}};
        oneshotsea::WeberSeaResult prepared{
            initial, initial, {}, {}, {}, {}, std::nullopt, {}};
        oneshotsea::extend_sea_with_classical_direct(
            *curve, one_off, {7U}, 16U, 100000U, 1000000U);
        oneshotsea::extend_sea_with_prepared_classical_direct(
            *curve, prepared, sea_context, 16U);
        const bool same_atkin =
            prepared.atkin_constraints.size() ==
                one_off.atkin_constraints.size() &&
            (prepared.atkin_constraints.empty() ||
             (prepared.atkin_constraints.front().ell ==
                  one_off.atkin_constraints.front().ell &&
              prepared.atkin_constraints.front().projective_order ==
                  one_off.atkin_constraints.front().projective_order &&
              prepared.atkin_constraints.front().trace_residues ==
                  one_off.atkin_constraints.front().trace_residues));
        check(prepared.constraints.modulus() ==
                  one_off.constraints.modulus() &&
                  prepared.constraints.residues() ==
                      one_off.constraints.residues() &&
                  prepared.effective_constraints.modulus() ==
                      one_off.effective_constraints.modulus() &&
                  prepared.effective_constraints.residues() ==
                      one_off.effective_constraints.residues() &&
                  same_atkin &&
                  prepared.traces == one_off.traces &&
                  prepared.classical_direct_levels.size() == 1U &&
                  prepared.classical_direct_levels.front().exact ==
                      one_off.classical_direct_levels.front().exact &&
                  prepared.classical_direct_levels.front().trace_residue ==
                      one_off.classical_direct_levels.front().trace_residue &&
                  prepared.classical_direct_levels.front()
                          .atkin_projective_order ==
                      one_off.classical_direct_levels.front()
                          .atkin_projective_order,
              "prepared SEA runner preserves exact and Atkin retained-state semantics");
    }
    check(sea_context.prepared_context_count() == 1U &&
              sea_context.preparation_us() != 0U,
          "prepared SEA schedule constructs one level lazily and reuses it across curves");

    const auto concurrent_context =
        oneshotsea::make_classical_direct_sea_context(
            field, {7U}, 100000U, 1000000U, 4U);
    std::vector<std::future<bool>> concurrent;
    for (std::size_t index = 0U; index < 4U; ++index) {
        concurrent.push_back(std::async(
            std::launch::async, [&, index] {
                oneshotsea::TraceConstraints thread_initial(
                    field.modulus());
                oneshotsea::WeberSeaResult thread_state{
                    thread_initial, thread_initial, {}, {}, {}, {},
                    std::nullopt, {}};
                const oneshotsea::Curve& curve =
                    index % 2U == 0U ? elkies_curve : atkin_curve;
                oneshotsea::extend_sea_with_prepared_classical_direct(
                    curve, thread_state, concurrent_context, 16U);
                return thread_state.classical_direct_levels.size() == 1U &&
                       thread_state.classical_direct_levels.front().ell ==
                           7U;
            }));
    }
    for (std::future<bool>& future : concurrent) {
        check(future.get(),
              "concurrent prepared direct SEA reconstruction stays complete");
    }
    check(concurrent_context.prepared_context_count() == 1U &&
              concurrent_context.preparation_threads() == 4U &&
              concurrent_context.preparation_us() != 0U,
          "concurrent workers share one sticky direct-level preparation");

    const auto preparation_failure = [&](std::size_t worker_threads) {
        try {
            static_cast<void>(
                oneshotsea::prepare_classical_direct_level_context(
                    order, field, 100000U, 1U, worker_threads));
        } catch (const std::exception& error) {
            return std::string(error.what());
        }
        return std::string();
    };
    const std::string serial_failure = preparation_failure(1U);
    const std::string parallel_failure = preparation_failure(4U);
    check(!serial_failure.empty() && parallel_failure == serial_failure,
          "parallel preparation reports the same lowest-index surface failure as serial preparation");

    const oneshotsea::Field wrong_field(197);
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::reconstruct_classical_specialization_from_prepared_context(
                      context, wrong_field, 20));
          }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::reconstruct_classical_specialization_from_prepared_context(
                          context, field, field.modulus() + 20));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::prepare_classical_direct_level_context(
                          order, field, 0U, 1000000U));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::make_classical_direct_sea_context(
                          field, {11U, 7U}, 100000U, 1000000U));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::make_classical_direct_sea_context(
                          field, {7U}, 100000U, 0U));
              }),
          "prepared direct contexts reject target substitution, noncanonical j, invalid schedules, and zero caps");

    oneshotsea::TraceConstraints wrong_initial(wrong_field.modulus());
    oneshotsea::WeberSeaResult wrong_state{
        wrong_initial, wrong_initial, {}, {}, {}, {}, std::nullopt, {}};
    const oneshotsea::Curve wrong_curve =
        oneshotsea::short_weierstrass_curve_from_j(wrong_field, 20);
    check(rejects([&] {
              oneshotsea::extend_sea_with_prepared_classical_direct(
                  wrong_curve, wrong_state, sea_context, 16U);
          }) &&
              wrong_state.classical_direct_levels.empty() &&
              wrong_state.constraints.modulus() == 1,
          "prepared SEA target mismatch fails before mutating retained state");
}

void test_classical_direct_sea_runner() {
    const oneshotsea::Field field(193);
    const oneshotsea::Curve elkies_curve =
        oneshotsea::short_weierstrass_curve_from_j(field, 20);
    const mpz_class elkies_trace =
        field.modulus() + 1 -
        oneshotsea::count_points_bruteforce(elkies_curve);
    oneshotsea::TraceConstraints initial(field.modulus());
    oneshotsea::WeberSeaResult elkies_state{
        initial, initial, {}, {}, {}, {}, std::nullopt, {}};
    oneshotsea::extend_sea_with_classical_direct(
        elkies_curve, elkies_state, {7U}, 16U, 100000U, 1000000U);
    check(elkies_state.classical_direct_levels.size() == 1U &&
              elkies_state.classical_direct_levels.front().exact &&
              elkies_state.classical_direct_levels.front().trace_residue ==
                  mpz_fdiv_ui(elkies_trace.get_mpz_t(), 7U) &&
              elkies_state.traces.has_value() &&
              elkies_state.traces->size() <= 16U &&
              std::find(elkies_state.traces->begin(),
                        elkies_state.traces->end(), elkies_trace) !=
                  elkies_state.traces->end(),
          "direct SEA exact level produces a complete bounded trace set containing the true trace");

    oneshotsea::extend_sea_with_classical_direct(
        elkies_curve, elkies_state, {7U}, 1U, 100000U, 1000000U);
    check(!elkies_state.traces.has_value() &&
              elkies_state.classical_direct_levels.size() == 1U,
          "no-op direct extension clears a stale cap-N trace enumeration at the exact cap-one gate");
    oneshotsea::extend_sea_with_classical_direct(
        elkies_curve, elkies_state, {7U, 11U}, 1U, 100000U, 1000000U);
    check(elkies_state.classical_direct_levels.size() == 2U &&
              elkies_state.classical_direct_levels.back().ell == 11U,
          "direct SEA extends retained state with only the next missing level");
    const auto& level11 = elkies_state.classical_direct_levels.back();
    if (level11.exact) {
        check(level11.trace_residue ==
                  mpz_fdiv_ui(elkies_trace.get_mpz_t(), 11U),
              "direct level-11 exact residue agrees with brute force");
    } else if (level11.atkin_projective_order.has_value()) {
        const auto found = std::find_if(
            elkies_state.atkin_constraints.begin(),
            elkies_state.atkin_constraints.end(),
            [](const oneshotsea::AtkinConstraint& constraint) {
                return constraint.ell == 11U;
            });
        check(found != elkies_state.atkin_constraints.end() &&
                  std::find(found->trace_residues.begin(),
                            found->trace_residues.end(),
                            mpz_fdiv_ui(elkies_trace.get_mpz_t(), 11U)) !=
                      found->trace_residues.end(),
              "direct level-11 Atkin set contains the brute-force trace residue");
    }

    const oneshotsea::Curve atkin_curve =
        oneshotsea::short_weierstrass_curve_from_j(field, 4);
    const mpz_class atkin_trace =
        field.modulus() + 1 -
        oneshotsea::count_points_bruteforce(atkin_curve);
    oneshotsea::TraceConstraints atkin_initial(field.modulus());
    oneshotsea::WeberSeaResult atkin_state{
        atkin_initial, atkin_initial, {}, {}, {}, {}, std::nullopt, {}};
    oneshotsea::extend_sea_with_classical_direct(
        atkin_curve, atkin_state, {7U}, 16U, 100000U, 1000000U);
    check(atkin_state.classical_direct_levels.size() == 1U &&
              !atkin_state.classical_direct_levels.front().exact,
          "direct SEA runner records the no-root level as nonexact");
    check(atkin_state.classical_direct_levels.front()
                  .atkin_projective_order.has_value() &&
              atkin_state.atkin_constraints.size() == 1U,
          "direct SEA runner retains certified Atkin evidence");
    check(std::find(atkin_state.atkin_constraints.front()
                        .trace_residues.begin(),
                    atkin_state.atkin_constraints.front()
                        .trace_residues.end(),
                    mpz_fdiv_ui(atkin_trace.get_mpz_t(), 7U)) !=
              atkin_state.atkin_constraints.front().trace_residues.end(),
          "direct SEA Atkin set contains the brute-force trace residue");
    check(atkin_state.traces.has_value(),
          "direct SEA Atkin constraint fits the requested complete trace cap");
    const auto screen = oneshotsea::screen_order_candidates(
        atkin_state.effective_constraints, 16U, 1000,
        [](const mpz_class& order) {
            return oneshotsea::ExactN4SmoothPart{
                oneshotsea::trial_smooth_part(order, 1000U)};
        });
    check(screen.has_value() && screen->rejects_curve(),
          "sound early-abort interface consumes the complete direct Atkin trace set");

    oneshotsea::TraceConstraints callback_initial(field.modulus());
    oneshotsea::WeberSeaResult callback_state{
        callback_initial, callback_initial, {}, {}, {}, {}, std::nullopt, {}};
    check(rejects([&] {
              oneshotsea::extend_sea_with_classical_direct(
                  elkies_curve, callback_state, {7U}, 16U, 100000U,
                  1000000U,
                  [](const oneshotsea::ClassicalDirectSeaLevelRecord&) {
                      throw std::runtime_error("forced callback failure");
                  });
          }) &&
              callback_state.constraints.modulus() == 1 &&
              callback_state.effective_constraints.modulus() == 1 &&
              callback_state.atkin_constraints.empty() &&
              callback_state.classical_direct_levels.empty() &&
              !callback_state.traces.has_value(),
          "direct SEA progress failure leaves retained state transactionally unchanged");
    const auto callback_context =
        oneshotsea::make_classical_direct_sea_context(
            field, {7U}, 100000U, 1000000U);
    check(rejects([&] {
              oneshotsea::extend_sea_with_prepared_classical_direct(
                  elkies_curve, callback_state, callback_context, 16U,
                  [](const oneshotsea::ClassicalDirectSeaLevelRecord&) {
                      throw std::runtime_error(
                          "forced prepared callback failure");
                  });
          }) &&
              callback_state.constraints.modulus() == 1 &&
              callback_state.classical_direct_levels.empty(),
          "prepared direct progress failure also leaves retained state unchanged");
    oneshotsea::extend_sea_with_prepared_classical_direct(
        elkies_curve, callback_state, callback_context, 16U);
    check(callback_state.classical_direct_levels.size() == 1U &&
              callback_context.prepared_context_count() == 1U,
          "the same prepared context remains reusable after callback failure");
    check(rejects([&] {
              oneshotsea::extend_sea_with_classical_direct(
                  elkies_curve, callback_state, {7U, 5U}, 16U,
                  100000U, 1000000U);
          }) &&
              rejects([&] {
                  oneshotsea::extend_sea_with_classical_direct(
                      elkies_curve, callback_state, {9U}, 16U,
                      100000U, 1000000U);
              }),
          "direct SEA rejects unsorted and composite level schedules");
}

void test_minus_71_surface() {
    const auto order = oneshotsea::validate_sutherland_suitable_order(
        5, -71, 1);
    const auto selected = oneshotsea::select_sutherland_crt_primes(
        order, 1009, 1, 1000);
    check(!selected.empty() && selected.front().prime == 1811 &&
              selected.front().trace == 12 &&
              selected.front().volcano_parameter == 2,
          "D=-71 selector produces the retained p=1811 witness");
    const oneshotsea::SutherlandCrtPrime witness = selected.front();
    const oneshotsea::Field field(witness.prime);
    const oneshotsea::Poly hilbert(
        field, hilbert_minus_71_coefficients());

    const auto surfaces = oneshotsea::enumerate_cm_interpolation_surfaces(
        order, witness, hilbert, 1811);
    const std::vector<mpz_class> expected_roots =
        {313, 1073, 1288, 1312, 1402, 1767, 1808};
    check(surfaces.level() == 5U &&
              surfaces.auxiliary_prime() == 1811 &&
              surfaces.all_surface_invariants() == expected_roots &&
              surfaces.surface_curves().size() == 7U &&
              surfaces.exact_group_order() == 1800 &&
              surfaces.horizontal_edges_per_surface() == 2U,
          "H_-71 splits into the seven expected CM surface invariants");

    const auto phi5 = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/j/phi_5.txt");
    for (const auto& surface : surfaces.surface_curves()) {
        check(surface.j_invariant == surface.curve.j_invariant() &&
                  hilbert.evaluate(surface.j_invariant) == 0 &&
                  oneshotsea::count_points_bruteforce(surface.curve) == 1800 &&
                  surface.edges.size() == 6U &&
                  surface.x_candidates_tested <= 1811U,
              "admitted surface curve has the HCP root and trace-sign order");
        const std::vector<mpz_class> weber_lifts =
            oneshotsea::weber_f_lifts(field, surface.j_invariant);
        check(weber_lifts.size() == 2U &&
                  field.add(weber_lifts.front(), weber_lifts.back()) == 0,
              "p=11 mod 12 CM surface has exactly the Weber +/- pair");

        const oneshotsea::Poly specialized =
            phi5.evaluate_x(field, surface.j_invariant);
        std::size_t horizontal = 0U;
        std::size_t descending = 0U;
        for (const auto& edge : surface.edges) {
            const mpz_class neighbor = edge.isogeny.codomain.j_invariant();
            check(specialized.evaluate(neighbor) == 0 &&
                      oneshotsea::count_points_bruteforce(
                          edge.isogeny.codomain) == 1800,
                  "classified CM edge is an authenticated Phi_5 neighbor with equal order");
            if (edge.codomain_on_surface) {
                ++horizontal;
                check(hilbert.evaluate(neighbor) == 0,
                      "horizontal edge lands on an H_-71 root");
            } else {
                ++descending;
                check(hilbert.evaluate(neighbor) != 0,
                      "descending edge leaves the H_-71 surface");
            }
        }
        check(horizontal == 2U && descending == 4U,
              "split level five yields two horizontal and four descending edges");
    }

    const oneshotsea::Field target_field(193);
    const std::vector<mpz_class> target_powers =
        oneshotsea::lifted_target_powers(target_field, 20, 6);
    const auto native_residue =
        oneshotsea::specialize_classical_from_cm_surfaces(
            surfaces, target_powers);
    const auto table_residue =
        oneshotsea::specialize_sparse_modpoly_for_crt_reference(
            phi5, target_powers, witness.prime);
    check(native_residue.prime == witness.prime &&
              native_residue.value_coefficients ==
                  table_residue.value_coefficients &&
              native_residue.x_derivative_coefficients ==
                  table_residue.x_derivative_coefficients,
          "table-free CM interpolation matches both Phi_5 specialization channels");

    const oneshotsea::Poly weber_class_polynomial(
        field, weber_class_minus_71_coefficients());
    const auto phi37_weber = oneshotsea::SparseModularPolynomial::load(
        37, "data/modpoly/weber_f/phi_37.txt");
    const auto weber_specialization =
        oneshotsea::specialize_weber_from_cm_surfaces(
            surfaces, weber_class_polynomial, {phi37_weber}, target_powers);
    const auto phi5_weber = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/weber_f/phi_5.txt");
    const auto weber_table_residue =
        oneshotsea::specialize_sparse_modpoly_for_crt_reference(
            phi5_weber, target_powers, witness.prime);
    check(weber_specialization.surface_invariant_count == 7U &&
              weber_specialization.floor_invariant_count == 28U &&
              weber_specialization.orientation_relation_count == 1U &&
              weber_specialization.relative_sign_coefficient == 1810 &&
              weber_specialization.residue.value_coefficients ==
                  weber_table_residue.value_coefficients &&
              weber_specialization.residue.x_derivative_coefficients ==
                  weber_table_residue.x_derivative_coefficients,
          "target-table-free Weber orientation matches both authenticated Phi_5 channels");

    oneshotsea::Poly negative_weber_class =
        oneshotsea::Poly::constant(field, 1);
    for (const mpz_class& root :
         std::vector<mpz_class>({11, 37, 491, 1028, 1091, 1188, 1586})) {
        negative_weber_class = oneshotsea::mul(
            negative_weber_class,
            oneshotsea::Poly(field, {root, 1}));
    }
    const auto globally_negated =
        oneshotsea::specialize_weber_from_cm_surfaces(
            surfaces, negative_weber_class, {phi37_weber}, target_powers);
    check(globally_negated.relative_sign_coefficient == 1810 &&
              globally_negated.residue.value_coefficients ==
                  weber_specialization.residue.value_coefficients &&
              globally_negated.residue.x_derivative_coefficients ==
                  weber_specialization.residue.x_derivative_coefficients,
          "global Weber sign choice leaves the normalized modular polynomial unchanged");

    for (std::size_t probe_degree = 0U; probe_degree < 7U;
         ++probe_degree) {
        std::vector<mpz_class> probe(7U, 0);
        probe.front() = 1;
        if (probe_degree != 0U) {
            probe[probe_degree] = 1;
        }
        const auto native_probe =
            oneshotsea::specialize_weber_from_cm_surfaces(
                surfaces, weber_class_polynomial, {phi37_weber}, probe);
        const auto table_probe =
            oneshotsea::specialize_sparse_modpoly_for_crt_reference(
                phi5_weber, probe, witness.prime);
        check(native_probe.residue.value_coefficients ==
                      table_probe.value_coefficients &&
                  native_probe.residue.x_derivative_coefficients ==
                      table_probe.x_derivative_coefficients,
              "Weber interpolation agrees on every lifted-power basis probe");
    }

    const auto phi19_weber = oneshotsea::SparseModularPolynomial::load(
        19, "data/modpoly/weber_f/phi_19.txt");
    const oneshotsea::SparseModularPolynomial malformed_phi37(
        37, {{38, 0, 1}, {0, 38, 1}, {37, 37, 1}});
    oneshotsea::Poly mixed_sign_class = oneshotsea::Poly::constant(field, 1);
    for (const mpz_class& root :
         std::vector<mpz_class>({1800, 37, 491, 1028, 1091, 1188, 1586})) {
        mixed_sign_class = oneshotsea::mul(
            mixed_sign_class,
            oneshotsea::Poly(field, {field.neg(root), 1}));
    }
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::specialize_weber_from_cm_surfaces(
                      surfaces, weber_class_polynomial, {}, target_powers));
          }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial, {phi19_weber},
                          target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial, {phi5_weber},
                          target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial,
                          {phi37_weber, phi37_weber}, target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial, {malformed_phi37},
                          target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, mixed_sign_class, {phi37_weber},
                          target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial, {phi37_weber},
                          {1, 2}));
              }),
          "Weber orientation rejects missing, insufficient, target-level, duplicate, malformed, mixed-sign, and malformed-power evidence");
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::specialize_classical_from_cm_surfaces(
                      surfaces, {1, 2}));
          }) &&
              rejects([&] {
                  std::vector<mpz_class> invalid = target_powers;
                  invalid.front() = 2;
                  static_cast<void>(
                      oneshotsea::specialize_classical_from_cm_surfaces(
                          surfaces, invalid));
              }) &&
              rejects([&] {
                  std::vector<mpz_class> invalid = target_powers;
                  invalid.back() = -1;
                  static_cast<void>(
                      oneshotsea::specialize_classical_from_cm_surfaces(
                          surfaces, invalid));
              }),
          "CM specialization rejects malformed target-power evidence");

    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::enumerate_cm_interpolation_surfaces(
                      order, witness, hilbert, 1));
          }) &&
              rejects([&] {
                  auto malformed_witness = witness;
                  ++malformed_witness.trace;
                  static_cast<void>(
                      oneshotsea::enumerate_cm_interpolation_surfaces(
                          order, malformed_witness, hilbert, 1811));
              }) &&
              rejects([&] {
                  const oneshotsea::Poly wrong_degree(field, {1, 0, 1});
                  static_cast<void>(
                      oneshotsea::enumerate_cm_interpolation_surfaces(
                          order, witness, wrong_degree, 1811));
              }),
          "CM surface admission fails closed on caps, witnesses, and HCP shape");

    oneshotsea::Poly repeated = oneshotsea::Poly::constant(field, 1);
    for (const mpz_class& root :
         std::vector<mpz_class>({1, 1, 2, 3, 4, 5, 6})) {
        repeated = oneshotsea::mul(
            repeated, oneshotsea::Poly(field, {field.neg(root), 1}));
    }
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::enumerate_cm_interpolation_surfaces(
                      order, witness, repeated, 1811));
          }),
          "CM surface admission rejects a repeated-root degree-seven impostor");
}

}  // namespace

int main() {
    try {
        test_three_power_class_polynomial();
        test_classical_direct_sea_runner();
        test_prepared_classical_context_equivalence();
        test_p125_classical_direct_path();
        test_minus_71_surface();
        std::cout << "CM interpolation surface tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "CM interpolation surface tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
