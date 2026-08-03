#include "oneshotsea/x1_27_probe.hpp"
#include "oneshotsea/schoof.hpp"
#include "oneshotsea/sea.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

mpz_class brute_force_order(const oneshotsea::Curve& curve) {
    const oneshotsea::Field& field = curve.field();
    const unsigned long prime = field.modulus().get_ui();
    mpz_class order = 1;
    for (unsigned long x = 0; x < prime; ++x) {
        const mpz_class field_x = x;
        const mpz_class rhs = field.add(
            field.add(field.mul(field.square(field_x), field_x),
                      field.mul(curve.a(), field_x)),
            curve.b());
        order += 1 + field.legendre(rhs);
    }
    return order;
}

}  // namespace

int main() {
    check(std::string(oneshotsea::kX127ProbeSchema) ==
              "oneshotsea.x1-27-probe.v1",
          "schema identity");
    check(std::string(oneshotsea::kX127ProbeGeneratorVersion) ==
              "x1-27-sutherland-tate-weber-montgomery-v1",
          "generator identity");
    check(std::string(oneshotsea::kX127FormulaSourceSha256) ==
              "b63a2527b1778acce2fa7d003655d929c1687eec9902b03982e729e11a571250",
          "formula identity");
    check(std::string(oneshotsea::kX127FormulaSourceUrl) ==
              "https://math.mit.edu/~drew/X1/X1opt27new.txt",
          "formula source URL");

    bool rejected_characteristic = false;
    try {
        (void)oneshotsea::deterministic_x1_27_probe(3, 1, 0, {1, false});
    } catch (const std::invalid_argument&) {
        rejected_characteristic = true;
    }
    check(rejected_characteristic, "characteristic three rejected");
    bool rejected_congruence = false;
    try {
        (void)oneshotsea::deterministic_x1_27_probe(103, 1, 0, {1, false});
    } catch (const std::invalid_argument&) {
        rejected_congruence = true;
    }
    check(rejected_congruence, "p=3 mod 4 rejected");
    bool rejected_zero_bound = false;
    try {
        (void)oneshotsea::deterministic_x1_27_probe(461, 1, 0, {0, false});
    } catch (const std::invalid_argument&) {
        rejected_zero_bound = true;
    }
    check(rejected_zero_bound, "zero u-sample bound rejected");

    constexpr std::uint64_t seed = UINT64_C(202607300000);
    const mpz_class small_prime = 461;
    const auto small_start = std::chrono::steady_clock::now();
    const oneshotsea::X127ProbeResult small =
        oneshotsea::deterministic_x1_27_probe(
            small_prime, seed, 0, {256, true});
    const auto small_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - small_start).count();
    if (!small.sample.has_value()) {
        std::cerr << "small miss p=" << small_prime
                  << " u=" << small.counters.u_samples
                  << " rootless=" << small.counters.u_polynomials_without_roots
                  << " points=" << small.counters.x1_points
                  << " map=" << small.counters.exceptional_map_points
                  << " singular=" << small.counters.singular_curves
                  << " order=" << small.counters.exact_order_27_failures
                  << " weber=" << small.counters.points_without_weber_lifts
                  << " model="
                  << small.counters.points_without_explicit_montgomery_model
                  << " e2=" << small.counters.full_two_torsion_failures
                  << " p4=" << small.counters.point_four_rejections << '\n';
    }
    check(small.sample.has_value(), "small-field admitted sample");
    check(small.sample->has_full_rational_two_torsion,
          "small-field full E[2]");
    check(small.sample->has_point_order_four,
          "small-field point four");
    check(small.sample->cyclic_divisor == 108U,
          "small-field cyclic divisor");
    check(small.sample->group_divisor == 432U,
          "small-field group divisor");
    check(small.sample->opposite_order_residue ==
              (2 * (small_prime + 1)) % 432,
          "small-field opposite residue");
    const mpz_class small_order = brute_force_order(small.sample->tate_curve);
    check(mpz_divisible_ui_p(small_order.get_mpz_t(), 432U) != 0,
          "brute-force order divisible by 432");
    const mpz_class small_curve_order =
        brute_force_order(small.sample->pair.curve);
    const mpz_class small_twist_order =
        brute_force_order(small.sample->pair.twist);
    const mpz_class selected_order =
        small.sample->selected_side == oneshotsea::X127CanonicalSide::curve
            ? small_curve_order
            : small_twist_order;
    const mpz_class canonical_trace =
        small_prime + 1 - small_curve_order;
    const mpz_class positive_trace_residue = (small_prime + 1) % 432;
    const mpz_class expected_trace_residue =
        small.sample->selected_side == oneshotsea::X127CanonicalSide::curve
            ? positive_trace_residue
            : mpz_class((432 - positive_trace_residue) % 432);
    check(small_curve_order + small_twist_order == 2 * (small_prime + 1) &&
              selected_order == small_order &&
              mpz_fdiv_ui(canonical_trace.get_mpz_t(), 432U) ==
                  expected_trace_residue.get_ui(),
          "canonical-side label induces the sound signed trace prior");
    check(small.sample->pair.j_invariant ==
              small.sample->tate_curve.j_invariant(),
          "small-field Tate/Weber j identity");

    const auto no_point_four =
        oneshotsea::deterministic_x1_27_search_curve(509, seed, 0, false);
    const mpz_class no_point_four_order =
        brute_force_order(no_point_four.sample->tate_curve);
    check(!no_point_four.sample->has_point_order_four &&
              no_point_four.sample->cyclic_divisor == 54U &&
              no_point_four.sample->group_divisor == 108U &&
              no_point_four_order % 108 == 0 &&
              no_point_four_order % 216 != 0,
          "p=509 sharply exercises the no-point-four divisor");

    const auto p1mod8_point_four =
        oneshotsea::deterministic_x1_27_search_curve(641, seed, 0, true);
    const mpz_class p1mod8_order =
        brute_force_order(p1mod8_point_four.sample->tate_curve);
    check(p1mod8_point_four.sample->has_point_order_four &&
              p1mod8_point_four.sample->cyclic_divisor == 108U &&
              p1mod8_point_four.sample->group_divisor == 216U &&
              p1mod8_order % 216 == 0 && p1mod8_order % 432 != 0,
          "p=641 sharply prevents unconditional promotion to 432");

    const std::string p125 =
        "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237";
    const mpz_class prime(p125);
    const auto target_start = std::chrono::steady_clock::now();
    const oneshotsea::X127ProbeResult target =
        oneshotsea::deterministic_x1_27_search_curve(prime, seed, 0, true);
    const auto target_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - target_start).count();
    const oneshotsea::X127ProbeResult repeat =
        oneshotsea::deterministic_x1_27_search_curve(prime, seed, 0, true);
    const oneshotsea::X127ProbeResult target_twist =
        oneshotsea::deterministic_x1_27_search_curve(prime, seed, 1, true);
    check(target.sample.has_value() && repeat.sample.has_value(),
          "p125 admitted sample");
    check(target.sample->x1_u == repeat.sample->x1_u &&
              target.sample->x1_v == repeat.sample->x1_v &&
              target.sample->pair.weber_f == repeat.sample->pair.weber_f,
          "p125 deterministic sample");
    check(target.sample->cyclic_divisor == 108U &&
              target.sample->group_divisor == 432U,
          "p125 exact divisor metadata");
    check(target.sample->opposite_order_residue == 28,
          "p125 opposite order residue");
    check(target.sample->selected_side == oneshotsea::X127CanonicalSide::curve &&
              target_twist.sample->selected_side ==
                  oneshotsea::X127CanonicalSide::twist &&
              (mpz_fdiv_ui(prime.get_mpz_t(), 432U) + 1U) % 432U == 14U,
          "p125 pinned indices cover trace residues 14 and 418");
    check(target.sample->pair.j_invariant ==
              target.sample->tate_curve.j_invariant(),
          "p125 Tate/Weber j identity");

    oneshotsea::TraceConstraints direct_initial(prime);
    direct_initial.refine_exact(432U, 14U);
    oneshotsea::WeberSeaResult direct_state{
        direct_initial, direct_initial, {}, {}, {}, {}, std::nullopt, {}};
    oneshotsea::extend_sea_with_classical_direct(
        target.sample->pair.curve, direct_state, {7U, 11U}, 64U,
        1000000U, 1000000U);
    check(direct_state.classical_direct_levels.size() == 2U &&
              direct_state.classical_direct_levels[0].ell == 7U &&
              direct_state.classical_direct_levels[0].exact &&
              direct_state.classical_direct_levels[0].trace_residue == 3U &&
              direct_state.classical_direct_levels[0]
                      .auxiliary_prime_count == 37U &&
              direct_state.classical_direct_levels[1].ell == 11U &&
              direct_state.classical_direct_levels[1].exact &&
              direct_state.classical_direct_levels[1].trace_residue == 5U &&
              direct_state.classical_direct_levels[1]
                      .auxiliary_prime_count == 43U &&
              direct_state.constraints.modulus() == 33264 &&
              !direct_state.traces.has_value(),
          "p125 production X1(27) curve retains direct levels 7 and 11 without false completion");
    check(oneshotsea::schoof_trace_mod_ell(
              target.sample->pair.curve, 7U) == 3U &&
              oneshotsea::schoof_trace_mod_ell(
                  target.sample->pair.curve, 11U) == 5U,
          "independent Schoof validates both p125 production direct residues");

    std::cout << "ok x1-27 probe small_p=" << small_prime
              << " small_us=" << small_us
              << " small_u_samples=" << small.counters.u_samples
              << " small_order=" << small_order
              << " p125_us=" << target_us
              << " p125_u_samples=" << target.counters.u_samples
              << " p125_x1_points=" << target.counters.x1_points << '\n';
    return 0;
}
