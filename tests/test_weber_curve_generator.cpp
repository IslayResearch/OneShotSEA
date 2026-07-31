#include "oneshotsea/weber.hpp"
#include "oneshotsea/weber_curve_generator.hpp"

#include <algorithm>
#include <cstdint>
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

template <typename Exception, typename Function>
void check_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void check_same_pair(const oneshotsea::WeberCurvePair& lhs,
                     const oneshotsea::WeberCurvePair& rhs,
                     const std::string& message) {
    check(lhs.weber_f == rhs.weber_f &&
              lhs.j_invariant == rhs.j_invariant &&
              lhs.twist_parameter == rhs.twist_parameter &&
              lhs.curve.a() == rhs.curve.a() &&
              lhs.curve.b() == rhs.curve.b() &&
              lhs.twist.a() == rhs.twist.a() &&
              lhs.twist.b() == rhs.twist.b() &&
              lhs.rejected_samples == rhs.rejected_samples,
          message);
}

void test_direct_weber_construction() {
    const std::vector<std::pair<unsigned long, unsigned long>> fixtures = {
        {11, 1}, {13, 1}, {37, 2}, {101, 2}};
    for (const auto& [prime, source_f] : fixtures) {
        const oneshotsea::Field field(prime);
        const oneshotsea::WeberCurvePair pair =
            oneshotsea::weber_curve_pair_from_f(field, source_f);

        check(pair.weber_f == source_f,
              "direct constructor retains its source lift");
        check(pair.j_invariant ==
                  oneshotsea::j_from_weber_f(field, pair.weber_f),
              "derived j agrees with the Weber relation");
        check(oneshotsea::j_derivative_from_weber_f(field, pair.weber_f) != 0,
              "admitted source lift is unramified");
        check(!pair.curve.is_singular() && !pair.twist.is_singular(),
              "derived curve classes are nonsingular");
        check(pair.curve.j_invariant() == pair.j_invariant &&
                  pair.twist.j_invariant() == pair.j_invariant,
              "curve and twist have the sampled j-invariant");
        check(field.legendre(pair.twist_parameter) == -1,
              "twist parameter is a quadratic nonsquare");

        const std::vector<mpz_class> lifts =
            oneshotsea::weber_f_lifts(field, pair.j_invariant);
        check(std::find(lifts.begin(), lifts.end(), pair.weber_f) != lifts.end(),
              "sampled f is admitted as a rational source lift");
    }
}

void test_exceptional_handling() {
    const oneshotsea::Field field101(101);
    check_throws<std::domain_error>(
        [&]() { (void)oneshotsea::weber_curve_pair_from_f(field101, 0); },
        "zero Weber-f value must be rejected");
    check(oneshotsea::j_from_weber_f(field101, 12) == 0,
          "j=0 exceptional fixture");
    check_throws<std::domain_error>(
        [&]() { (void)oneshotsea::weber_curve_pair_from_f(field101, 12); },
        "j=0 Weber image must be rejected");

    const oneshotsea::Field field11(11);
    check(oneshotsea::j_from_weber_f(field11, 4) == field11.normalize(1728),
          "j=1728 exceptional fixture");
    check_throws<std::domain_error>(
        [&]() { (void)oneshotsea::weber_curve_pair_from_f(field11, 4); },
        "j=1728 Weber image must be rejected");

    check_throws<std::invalid_argument>(
        []() { (void)oneshotsea::deterministic_weber_curve_pair(7, 0, 0); },
        "small field with no nonexceptional Weber image must be rejected");
    check_throws<std::invalid_argument>(
        []() { (void)oneshotsea::deterministic_weber_curve_pair(15, 0, 0); },
        "composite field modulus must be rejected");
}

void test_deterministic_replay_and_retry() {
    const auto original =
        oneshotsea::deterministic_weber_curve_pair(109, 9001, 37);
    const auto replay =
        oneshotsea::deterministic_weber_curve_pair(109, 9001, 37);
    check_same_pair(original, replay,
                    "p/seed/global-index tuple must replay exactly");

    bool exercised_retry = false;
    for (std::uint64_t index = 0; index < 256; ++index) {
        const auto pair =
            oneshotsea::deterministic_weber_curve_pair(101, 17, index);
        check(pair.weber_f != 0 && pair.j_invariant != 0 &&
                  pair.j_invariant != pair.curve.field().normalize(1728),
              "deterministic sampler returns only admitted nonexceptional values");
        check(oneshotsea::has_montgomery_model_from_j(
                  pair.curve.field(), pair.j_invariant),
              "deterministic sampler returns certificate-compatible models");
        if (pair.rejected_samples != 0) {
            const auto retry_replay =
                oneshotsea::deterministic_weber_curve_pair(101, 17, index);
            check_same_pair(pair, retry_replay,
                            "exceptional-value retry must replay exactly");
            exercised_retry = true;
            break;
        }
    }
    check(exercised_retry, "deterministic fixture exercises exceptional retry");
}

void test_certificate_model_prefilter() {
    const oneshotsea::Field field(101);
    bool found_compatible = false;
    bool found_incompatible = false;
    for (mpz_class source_f = 1; source_f < field.modulus(); ++source_f) {
        try {
            const auto pair =
                oneshotsea::weber_curve_pair_from_f(field, source_f);
            if (!oneshotsea::has_montgomery_model_from_j(
                    field, pair.j_invariant)) {
                found_incompatible = true;
                continue;
            }
            found_compatible = true;
            const mpz_class curve_order =
                oneshotsea::count_points_bruteforce(pair.curve);
            const mpz_class twist_order =
                oneshotsea::count_points_bruteforce(pair.twist);
            check(mpz_divisible_ui_p(curve_order.get_mpz_t(), 4) != 0 &&
                      mpz_divisible_ui_p(twist_order.get_mpz_t(), 4) != 0,
                  "compatible Weber models carry full rational 2-torsion");
        } catch (const std::domain_error&) {
            // The direct reference constructor rejects ramified images.
        }
    }
    check(found_compatible && found_incompatible,
          "Weber family contains compatible and wasted certificate models");

    for (std::uint64_t index = 0; index < 64; ++index) {
        const auto pair =
            oneshotsea::deterministic_weber_curve_pair(101, 71, index);
        check(oneshotsea::has_montgomery_model_from_j(
                  pair.curve.field(), pair.j_invariant),
              "production generator filters every incompatible model");
    }
}

void test_curve_twist_coverage_relation() {
    for (std::uint64_t index = 0; index < 12; ++index) {
        const auto pair =
            oneshotsea::deterministic_weber_curve_pair(37, 4242, index);
        const mpz_class curve_order =
            oneshotsea::count_points_bruteforce(pair.curve);
        const mpz_class twist_order =
            oneshotsea::count_points_bruteforce(pair.twist);
        check(curve_order + twist_order == 2 * pair.curve.field().modulus() + 2,
              "quadratic-twist orders sum to 2p+2");
        check(pair.curve.j_invariant() == pair.twist.j_invariant(),
              "covered curve classes share their j-invariant");
    }
}

}  // namespace

int main() {
    try {
        test_direct_weber_construction();
        test_exceptional_handling();
        test_deterministic_replay_and_retry();
        test_certificate_model_prefilter();
        test_curve_twist_coverage_relation();
        std::cout << "all Weber curve-generator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
