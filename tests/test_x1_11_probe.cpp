#include "oneshotsea/poly.hpp"
#include "oneshotsea/torsion.hpp"
#include "oneshotsea/weber.hpp"
#include "oneshotsea/x1_11_probe.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("test failure: " + message);
    }
}

void check_counter_partition(
    const oneshotsea::X111ProbeRejections& counters) {
    const std::uint64_t terminal_points =
        counters.singular_curves + counters.exceptional_j +
        counters.exact_order_11_failures +
        counters.points_without_weber_lifts +
        counters.points_without_explicit_montgomery_model +
        counters.full_two_torsion_failures + counters.point_four_rejections +
        counters.accepted;
    check(counters.x1_points == terminal_points,
          "every visited X1(11) point has exactly one terminal outcome");
}

void check_sample(const oneshotsea::X111ProbeSample& sample) {
    const oneshotsea::Field& field = sample.tate_curve.field();
    const mpz_class p = field.modulus();
    check(field.add(
              field.add(field.square(sample.x1_y),
                        field.mul(field.add(field.square(sample.x1_x), 1),
                                  sample.x1_y)),
              sample.x1_x) == 0,
          "sample lies on the pinned X1(11) plane equation");
    check(sample.tate_r == field.add(field.mul(sample.x1_x, sample.x1_y), 1) &&
              sample.tate_s == field.sub(1, sample.x1_x) &&
              sample.tate_c ==
                  field.mul(sample.tate_s, field.sub(sample.tate_r, 1)) &&
              sample.tate_b == field.mul(sample.tate_r, sample.tate_c),
          "sample records the pinned X1(11)-to-Tate map");
    check(!sample.tate_curve.is_singular(), "Tate short curve is nonsingular");
    check(field.square(sample.tate_point_y) ==
              field.add(
                  field.add(field.mul(field.square(sample.tate_point_x),
                                      sample.tate_point_x),
                            field.mul(sample.tate_curve.a(),
                                      sample.tate_point_x)),
                  sample.tate_curve.b()),
          "distinguished Tate point lies on the short curve");
    check(oneshotsea::division_polynomial_reference(sample.tate_curve, 11)
                  .evaluate(sample.tate_point_x) == 0,
          "distinguished nonidentity point has exact order eleven");
    check(sample.tate_curve.j_invariant() == sample.pair.j_invariant,
          "Tate and Weber models have equal j-invariant");
    check(oneshotsea::j_from_weber_f(field, sample.pair.weber_f) ==
              sample.pair.j_invariant,
          "retained Weber lift maps back to j");

    const oneshotsea::MontgomeryCurve montgomery(
        field, sample.explicit_montgomery_coefficient);
    const mpz_class weber_f_24 = field.pow(sample.pair.weber_f, 24);
    const mpz_class explicit_u =
        field.sub(4, field.divide(weber_f_24, 16));
    check(field.square(sample.explicit_montgomery_coefficient) == explicit_u,
          "explicit Montgomery coefficient passes the Weber square gate");
    check(!montgomery.is_singular() &&
              montgomery.j_invariant() == sample.pair.j_invariant,
          "explicit Weber square gives a valid Montgomery model");
    check(field.legendre(field.sub(
              field.square(sample.explicit_montgomery_coefficient), 4)) == 1,
          "explicit Montgomery model has full rational 2-torsion");
    check(oneshotsea::linear_roots(oneshotsea::Poly(
              field, {sample.tate_curve.b(), sample.tate_curve.a(), 0, 1}))
              .size() == 3U,
          "Tate twist class has full rational 2-torsion");

    const mpz_class tate_order =
        oneshotsea::count_points_bruteforce(sample.tate_curve);
    const mpz_class canonical_curve_order =
        oneshotsea::count_points_bruteforce(sample.pair.curve);
    const mpz_class canonical_twist_order =
        oneshotsea::count_points_bruteforce(sample.pair.twist);
    check(canonical_curve_order + canonical_twist_order == 2 * p + 2,
          "canonical curve/twist orders have the correct sum");
    const mpz_class selected_order =
        sample.selected_side == oneshotsea::X111CanonicalSide::curve
            ? canonical_curve_order
            : canonical_twist_order;
    const mpz_class opposite_order =
        sample.selected_side == oneshotsea::X111CanonicalSide::curve
            ? canonical_twist_order
            : canonical_curve_order;
    const mpz_class group_divisor =
        static_cast<unsigned long>(sample.group_divisor);
    check(selected_order == tate_order,
          "selected canonical side is the Tate isomorphism class");
    check((tate_order % 8 == 0) == sample.has_point_order_four,
          "point-four predicate agrees with the independently counted order");
    check(selected_order % group_divisor == 0,
          "selected order contains the promised group divisor");
    check(opposite_order % group_divisor == sample.opposite_order_residue,
          "opposite order has the promised twist residue");
    check(sample.has_full_rational_two_torsion,
          "sample reports mandatory full rational 2-torsion");
    check(sample.cyclic_divisor == (sample.has_point_order_four ? 44U : 22U),
          "cyclic divisor records the point-four decision");
    const std::uint64_t expected_group_divisor =
        sample.has_point_order_four && mpz_fdiv_ui(p.get_mpz_t(), 8U) == 5U
            ? 176U
            : (sample.has_point_order_four ? 88U : 44U);
    check(sample.group_divisor == expected_group_divisor,
          "group divisor records the strongest validated 2-primary branch");
}

void test_formula_identity_and_input_bounds() {
    check(std::string(oneshotsea::kX111ProbeSchema) ==
              "oneshotsea.x1-11-probe.v1",
          "probe schema is pinned");
    check(std::string(oneshotsea::kX111ProbeGeneratorVersion) ==
              "x1-11-tate-weber-montgomery-v2",
          "probe generator version is pinned");
    check(std::string(oneshotsea::kX111FormulaSourceSha256) ==
              "19f76aef352cea9a6e1d3347977eb9286b03e70fa6b4afb8daea013ebbd6bd4c",
          "X1(11) source digest is pinned");
    check(std::string(oneshotsea::kX111FormulaSourceUrl) ==
              "https://math.mit.edu/~drew/X1/X1opt11.txt",
          "X1(11) source URL is pinned");

    for (const mpz_class& invalid : {mpz_class(3), mpz_class(5),
                                     mpz_class(11), mpz_class(15),
                                     mpz_class(103)}) {
        bool rejected = false;
        try {
            (void)oneshotsea::deterministic_x1_11_probe(
                invalid, 1, 0, {1, false});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "invalid probe characteristic is rejected");
    }
    bool rejected_zero_bound = false;
    try {
        (void)oneshotsea::deterministic_x1_11_probe(
            101, 1, 0, {0, false});
    } catch (const std::invalid_argument&) {
        rejected_zero_bound = true;
    }
    check(rejected_zero_bound, "zero x-sample bound is rejected");
}

void test_determinism_divisibility_and_counters() {
    struct Fixture {
        unsigned long prime;
        std::uint64_t index;
        std::uint64_t max_x_samples;
        bool point_four;
    };
    constexpr std::uint64_t seed = UINT64_C(0x7821058d55e0f265);
    for (const Fixture fixture : {
             Fixture{157UL, 7U, 2U, true},
             Fixture{157UL, 2U, 4U, true},
             Fixture{397UL, 0U, 5U, false},
         }) {
        const oneshotsea::X111ProbeResult first =
            oneshotsea::deterministic_x1_11_probe(
                fixture.prime, seed, fixture.index,
                {fixture.max_x_samples, false});
        const oneshotsea::X111ProbeResult repeat =
            oneshotsea::deterministic_x1_11_probe(
                fixture.prime, seed, fixture.index,
                {fixture.max_x_samples, false});
        check(first.counters.x_samples == repeat.counters.x_samples &&
                  first.counters.x1_points == repeat.counters.x1_points &&
                  first.counters.weber_lifts == repeat.counters.weber_lifts &&
                  first.counters.accepted == repeat.counters.accepted &&
                  first.sample.has_value() == repeat.sample.has_value(),
              "probe retry stream and counters are deterministic");
        check(first.counters.x_samples >= 1U &&
                  first.counters.x_samples <= fixture.max_x_samples,
              "probe honors the x-coordinate bound");
        check(first.counters.accepted == 1U && first.sample.has_value(),
              "pinned fixture is accepted exactly once");
        check(first.counters.exact_order_11_failures == 0U,
              "pinned X1(11) map never produces a wrong point order");
        check(first.counters.full_two_torsion_failures == 0U,
              "explicit p=1 mod 4 square gate always gives full E[2]");
        check_counter_partition(first.counters);
        const auto& sample = *first.sample;
        const auto& repeated_sample = *repeat.sample;
        check(sample.x1_x == repeated_sample.x1_x &&
                  sample.x1_y == repeated_sample.x1_y &&
                  sample.pair.weber_f == repeated_sample.pair.weber_f &&
                  sample.explicit_montgomery_coefficient ==
                      repeated_sample.explicit_montgomery_coefficient &&
                  sample.selected_side == repeated_sample.selected_side,
              "accepted X1(11) sample is deterministic");
        check(sample.has_point_order_four == fixture.point_four,
              "pinned fixtures exercise both point-four outcomes");
        check_sample(sample);
    }

    const oneshotsea::X111ProbeResult bounded_miss =
        oneshotsea::deterministic_x1_11_probe(109, seed, 0, {3, false});
    check(!bounded_miss.sample.has_value() &&
              bounded_miss.counters.x_samples == 3U &&
              bounded_miss.counters.accepted == 0U,
          "bounded miss exhausts exactly the requested x-coordinate budget");
    check_counter_partition(bounded_miss.counters);
}

void test_required_point_four_filter() {
    const oneshotsea::X111ProbeResult result =
        oneshotsea::deterministic_x1_11_probe(
            157, UINT64_C(0x7821058d55e0f265), 7, {2, true});
    check(result.sample.has_value() && result.sample->has_point_order_four,
          "required point-four mode admits the pinned positive fixture");
    check(result.sample->cyclic_divisor == 44U &&
              result.sample->group_divisor == 176U,
          "p=5 mod 8 point-four mode advertises the exact 16-by-11 group divisor");
    check_counter_partition(result.counters);
    check_sample(*result.sample);

    const oneshotsea::X111ProbeResult p1mod8 =
        oneshotsea::deterministic_x1_11_probe(89, 12345, 0, {8, true});
    check(p1mod8.sample.has_value() &&
              p1mod8.sample->has_point_order_four &&
              p1mod8.sample->cyclic_divisor == 44U &&
              p1mod8.sample->group_divisor == 88U &&
              oneshotsea::count_points_bruteforce(
                  p1mod8.sample->tate_curve) == 88,
          "p=1 mod 8 admitted point-four fixture sharply retains divisor 88");
    check_counter_partition(p1mod8.counters);
    check_sample(*p1mod8.sample);
}

void test_unbounded_search_generator() {
    constexpr std::uint64_t seed = UINT64_C(0x7821058d55e0f265);
    const oneshotsea::X111ProbeResult first =
        oneshotsea::deterministic_x1_11_search_curve(157, seed, 7, true);
    const oneshotsea::X111ProbeResult repeat =
        oneshotsea::deterministic_x1_11_search_curve(157, seed, 7, true);
    check(first.sample.has_value() && repeat.sample.has_value(),
          "unbounded X1(11) search generator returns an admitted curve");
    check(first.counters.x_samples == repeat.counters.x_samples &&
              first.sample->x1_x == repeat.sample->x1_x &&
              first.sample->x1_y == repeat.sample->x1_y &&
              first.sample->pair.weber_f == repeat.sample->pair.weber_f,
          "unbounded X1(11) search generator is deterministic");
    check(first.sample->pair.rejected_samples ==
              first.counters.x_samples - 1U,
          "production pair records every prior rejected x-coordinate");
    check(first.sample->has_point_order_four &&
              first.sample->cyclic_divisor == 44U &&
              first.sample->group_divisor == 176U,
          "point-four search mode retains validated divisor metadata");
    check_counter_partition(first.counters);
    check_sample(*first.sample);
}

}  // namespace

int main() {
    try {
        test_formula_identity_and_input_bounds();
        test_determinism_divisibility_and_counters();
        test_required_point_four_filter();
        test_unbounded_search_generator();
        std::cout << "x1(11) probe tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
