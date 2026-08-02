#include "oneshotsea/curve.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/isogeny.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/torsion.hpp"

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

struct Coverage {
    std::size_t kernels = 0;
    std::size_t lower_sign = 0;
    std::size_t upper_sign = 0;
    std::size_t lambda_one = 0;
    std::size_t lambda_minus_one = 0;
    std::size_t giant_boundaries = 0;
    std::size_t levels = 0;
};

std::uint64_t mitm_width(std::uint64_t ell) {
    std::uint64_t width = 1;
    while (width < ell / width + (ell % width == 0 ? 0U : 1U)) {
        ++width;
    }
    return width;
}

void record_coverage(std::uint64_t ell, std::uint64_t eigenvalue,
                     Coverage& coverage) {
    ++coverage.kernels;
    coverage.lower_sign += eigenvalue <= (ell - 1U) / 2U ? 1U : 0U;
    coverage.upper_sign += eigenvalue > (ell - 1U) / 2U ? 1U : 0U;
    coverage.lambda_one += eigenvalue == 1U ? 1U : 0U;
    coverage.lambda_minus_one += eigenvalue == ell - 1U ? 1U : 0U;
    coverage.giant_boundaries +=
        eigenvalue % mitm_width(ell) == 0U ? 1U : 0U;
}

void compare_validated_isogeny(
    const oneshotsea::Curve& source, const oneshotsea::Curve& codomain,
    const oneshotsea::BmssIsogenyResult& isogeny, std::uint64_t ell,
    std::uint64_t expected, Coverage& coverage) {
    const auto linear =
        oneshotsea::try_frobenius_eigenvalue_from_isogeny_for_testing(
            source, codomain, isogeny, ell,
            oneshotsea::FrobeniusEigenvalueTestPath::linear);
    const auto mitm =
        oneshotsea::try_frobenius_eigenvalue_from_isogeny_for_testing(
            source, codomain, isogeny, ell,
            oneshotsea::FrobeniusEigenvalueTestPath::meet_in_the_middle);
    const auto automatic = oneshotsea::try_frobenius_eigenvalue_from_isogeny(
        source, codomain, isogeny, ell);
    check(linear.has_value(), "linear search rejected a validated eigenkernel");
    check(mitm.has_value(), "MITM search rejected a validated eigenkernel");
    check(automatic.has_value(),
          "automatic search rejected a validated eigenkernel");
    check(*linear == *mitm, "linear and MITM eigenvalues disagree");
    check(*linear == *automatic,
          "forced and automatic eigenvalues disagree");
    check(*linear == expected, "forced eigenvalue disagrees with fixture");

    record_coverage(ell, expected, coverage);
}

void compare_division_kernels(const oneshotsea::Curve& curve,
                              std::uint64_t ell, Coverage& coverage) {
    const auto kernels =
        oneshotsea::elkies_kernels_division_reference(curve, ell);
    check(!kernels.empty(), "division-polynomial fixture is not Elkies");
    ++coverage.levels;
    for (const auto& kernel : kernels) {
        const auto linear =
            oneshotsea::try_frobenius_eigenvalue_reference_for_testing(
                curve, kernel.kernel, ell,
                oneshotsea::FrobeniusEigenvalueTestPath::linear);
        const auto mitm =
            oneshotsea::try_frobenius_eigenvalue_reference_for_testing(
                curve, kernel.kernel, ell,
                oneshotsea::FrobeniusEigenvalueTestPath::meet_in_the_middle);
        const auto automatic = oneshotsea::try_frobenius_eigenvalue_reference(
            curve, kernel.kernel, ell);
        check(linear.has_value() && mitm.has_value() && automatic.has_value(),
              "validated division-polynomial kernel lost its eigenvalue");
        check(*linear == *mitm && *linear == *automatic,
              "division-polynomial linear/MITM result differs");
        check(*linear == kernel.eigenvalue,
              "forced result differs from division-polynomial fixture");
        record_coverage(ell, kernel.eigenvalue, coverage);
    }
}

void compare_reference_validation_hook() {
    const oneshotsea::Curve curve(oneshotsea::Field(101), 1, 1);
    const oneshotsea::Curve codomain(oneshotsea::Field(101), 75, 16);
    const auto isogeny =
        oneshotsea::bmss_isogeny_reference(curve, codomain, 11);
    const auto linear =
        oneshotsea::try_frobenius_eigenvalue_reference_for_testing(
            curve, isogeny.kernel, 11,
            oneshotsea::FrobeniusEigenvalueTestPath::linear);
    const auto mitm =
        oneshotsea::try_frobenius_eigenvalue_reference_for_testing(
            curve, isogeny.kernel, 11,
            oneshotsea::FrobeniusEigenvalueTestPath::meet_in_the_middle);
    check(linear.has_value() && mitm.has_value() && *linear == *mitm,
          "fully independent linear/MITM validation disagrees");
}

void compare_level_37(Coverage& coverage) {
    const oneshotsea::Curve curve(oneshotsea::Field(1009), 799, 474);
    const auto modular_polynomial =
        oneshotsea::SparseModularPolynomial::load(
            37, "data/modpoly/weber_f/phi_37.txt");
    const auto independent =
        oneshotsea::compute_weber_elkies_level_reference(
            curve, modular_polynomial, nullptr, 1, true, false);
    const auto reused = oneshotsea::compute_weber_elkies_level_reference(
        curve, modular_polynomial, nullptr, 1, true, true);
    check(!independent.kernels.empty(),
          "level-37 Weber fixture is not Elkies");
    check(independent.kernels.size() == reused.kernels.size(),
          "conjugate reuse changed the level-37 kernel count");
    check(independent.compatible_source_lifts ==
              reused.compatible_source_lifts,
          "conjugate reuse changed compatible Weber source lifts");
    check(!independent.timings.conjugate_eigenvalue_reuse &&
              independent.timings.conjugate_eigenvalues_derived == 0U &&
              independent.timings.independent_eigenvalue_recoveries ==
                  independent.timings.eigenvalue_attempts,
          "disabled conjugate reuse did not retain independent recovery");
    check(reused.timings.conjugate_eigenvalue_reuse &&
              reused.timings.eigenvalue_attempts == reused.kernels.size() &&
              reused.timings.independent_eigenvalue_recoveries == 1U &&
              reused.timings.conjugate_eigenvalues_derived + 1U ==
                  reused.kernels.size(),
          "enabled conjugate reuse telemetry does not partition recoveries");
    for (std::size_t index = 0; index < independent.kernels.size(); ++index) {
        const auto& expected = independent.kernels[index];
        const auto& actual = reused.kernels[index];
        check(oneshotsea::equal(expected.kernel, actual.kernel) &&
                  expected.codomain.a() == actual.codomain.a() &&
                  expected.codomain.b() == actual.codomain.b() &&
                  expected.neighbor_j == actual.neighbor_j &&
                  expected.eigenvalue == actual.eigenvalue &&
                  expected.trace_residue == actual.trace_residue,
              "independent and conjugate-derived Weber kernels disagree");
    }
    ++coverage.levels;
    for (const auto& kernel : reused.kernels) {
        const auto isogeny = oneshotsea::bmss_isogeny_reference(
            curve, kernel.codomain, 37);
        check(oneshotsea::equal(isogeny.kernel, kernel.kernel),
              "level-37 BMSS kernel reconstruction changed");
        compare_validated_isogeny(
            curve, kernel.codomain, isogeny, 37, kernel.eigenvalue, coverage);
    }
}

}  // namespace

int main() {
    try {
        Coverage coverage;
        compare_division_kernels(
            oneshotsea::Curve(oneshotsea::Field(5), 1, 1), 3, coverage);
        compare_division_kernels(
            oneshotsea::Curve(oneshotsea::Field(19), 0, 4), 5, coverage);
        compare_division_kernels(
            oneshotsea::Curve(oneshotsea::Field(37), 0, 3), 7, coverage);
        compare_reference_validation_hook();
        compare_level_37(coverage);

        check(coverage.kernels >= 17,
              "insufficient forced-path kernel coverage");
        check(coverage.levels == 4,
              "small and medium levels were not all exercised");
        check(coverage.lower_sign > 0 && coverage.upper_sign > 0,
              "both Frobenius eigenvalue signs were not exercised");
        check(coverage.lambda_one > 0 && coverage.lambda_minus_one > 0,
              "lambda = +/-1 boundaries were not exercised");
        check(coverage.giant_boundaries > 0,
              "MITM exact giant-step boundary was not exercised");
        std::cout << "eigenvalue MITM differential tests: ok ("
                  << coverage.kernels << " kernels)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "eigenvalue MITM differential tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
