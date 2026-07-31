#include "oneshotsea/certificate.hpp"
#include "oneshotsea/curve.hpp"

#include <algorithm>
#include <cstdlib>
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

void canonical_accepts(const oneshotsea::MontgomeryCertificate& certificate) {
    const std::string command =
        "python3 third_party/oneshot_primality_proofs/voneshot.py " +
        certificate.line() + " >/dev/null";
    check(std::system(command.c_str()) == 0,
          "canonical verifier rejected: " + certificate.line());
}

void test_bounds_and_candidate_preparation() {
    const auto bounds = oneshotsea::canonical_certificate_bounds(101);
    check(bounds.bit_length == 7, "canonical bit length");
    check(bounds.n2 == 49 && bounds.n4 == 2401,
          "canonical n^2/n^4 bounds");
    check(bounds.lower_order_bound == 17, "canonical lower bound");
    check(bounds.hasse_upper_bound == 122, "canonical Hasse bound");

    const auto candidate =
        oneshotsea::prepare_certificate_candidate(101, 96, 96);
    check(static_cast<bool>(candidate), "curve candidate preparation");
    check(candidate.failure == oneshotsea::CandidateFailure::none,
          "successful candidate status");
    check(candidate.candidate->point_order == 24,
          "build_m chose canonical minimal order");
    check(candidate.candidate->distinct_prime_divisors ==
              std::vector<std::uint64_t>({2, 3}),
          "candidate exact distinct factors");
    check(candidate.candidate->large_prime_divisors.empty(),
          "candidate has no factors above n^2");

    const auto too_small =
        oneshotsea::prepare_certificate_candidate(101, 96, 12);
    check(too_small.failure ==
              oneshotsea::CandidateFailure::insufficient_smooth_part,
          "insufficient smooth part is classified");
    const auto not_divisor =
        oneshotsea::prepare_certificate_candidate(101, 96, 25);
    check(not_divisor.failure ==
              oneshotsea::CandidateFailure::smooth_part_not_divisor,
          "nondivisor smooth part is classified");
    const auto wrong_order =
        oneshotsea::prepare_certificate_candidate(101, 200, 100);
    check(wrong_order.failure == oneshotsea::CandidateFailure::invalid_order,
          "non-Hasse order is classified");
    const auto odd_only =
        oneshotsea::prepare_certificate_candidate(101, 96, 96, true);
    check(odd_only.failure ==
              oneshotsea::CandidateFailure::no_admissible_divisor,
          "odd-only fallback rejects an insufficient odd part");

    for (const auto failure : {
             oneshotsea::CandidateFailure::none,
             oneshotsea::CandidateFailure::invalid_modulus,
             oneshotsea::CandidateFailure::unsupported_bit_length,
             oneshotsea::CandidateFailure::invalid_order,
             oneshotsea::CandidateFailure::invalid_smooth_part,
             oneshotsea::CandidateFailure::smooth_part_not_divisor,
             oneshotsea::CandidateFailure::insufficient_smooth_part,
             oneshotsea::CandidateFailure::no_admissible_divisor,
         }) {
        check(std::string(oneshotsea::candidate_failure_name(failure)) !=
                  "unknown",
              "candidate status has a stable metric name");
    }
}

void test_ladder_and_exact_order() {
    struct Fixture {
        unsigned long scalar;
        unsigned long x;
        unsigned long z;
    };
    // Byte-for-byte coordinate values produced by the pinned voneshot.py
    // ladder for p=101, A=3, P=(24:1).
    const std::vector<Fixture> fixtures = {
        {0, 1, 0}, {1, 24, 1}, {2, 52, 88},
        {3, 9, 80}, {7, 92, 52}, {24, 92, 0},
    };
    for (const Fixture& fixture : fixtures) {
        const auto result = oneshotsea::montgomery_ladder(
            101, 3, fixture.scalar, {24, 1});
        check(result.x == fixture.x && result.z == fixture.z,
              "native ladder matches pinned verifier fixture");
    }
    check(oneshotsea::montgomery_has_exact_order(
              101, 3, {24, 1}, 24, {2, 3}),
          "known point has exact order 24");
    check(!oneshotsea::montgomery_has_exact_order(
               101, 3, {24, 1}, 12, {2, 3}),
          "known point does not have order 12");
    check(!oneshotsea::montgomery_has_exact_order(
               101, 3, {24, 1}, 24, {2, 2, 3}),
          "exact-order helper rejects a duplicate factor list");
}

void test_direct_curve_and_twist_assembly() {
    const oneshotsea::MontgomeryCurve curve(oneshotsea::Field(101), 3);
    const mpz_class curve_order =
        oneshotsea::count_points_bruteforce(curve.short_weierstrass());
    check(curve_order == 96, "Montgomery curve point count fixture");
    const mpz_class twist_order = 2 * 101 + 2 - curve_order;
    check(twist_order == 108, "Montgomery twist point count fixture");

    const auto curve_candidate = oneshotsea::prepare_certificate_candidate(
        101, curve_order, curve_order);
    check(static_cast<bool>(curve_candidate), "curve candidate exists");
    const oneshotsea::AssemblyOptions curve_options{
        17, 400, oneshotsea::MontgomerySide::curve};
    const auto certificate = oneshotsea::assemble_montgomery_certificate(
        *curve_candidate.candidate, 3, curve_options);
    check(certificate.has_value(), "direct curve certificate assembly");
    check(certificate->order == 24, "direct curve assembled order");
    check(oneshotsea::validate_montgomery_certificate(*certificate),
          "native direct-curve certificate validation");
    canonical_accepts(*certificate);

    const auto repeat = oneshotsea::assemble_montgomery_certificate(
        *curve_candidate.candidate, 3, curve_options);
    check(repeat.has_value() && repeat->line() == certificate->line(),
          "certificate assembly is deterministic by seed");

    oneshotsea::AssemblyOptions wrong_side = curve_options;
    wrong_side.side = oneshotsea::MontgomerySide::twist;
    check(!oneshotsea::assemble_montgomery_certificate(
               *curve_candidate.candidate, 3, wrong_side)
               .has_value(),
          "order projection cannot manufacture order 24 on the twist");

    const auto twist_candidate = oneshotsea::prepare_certificate_candidate(
        101, twist_order, twist_order);
    check(static_cast<bool>(twist_candidate), "twist candidate exists");
    const oneshotsea::AssemblyOptions twist_options{
        23, 400, oneshotsea::MontgomerySide::twist};
    const auto twist_certificate = oneshotsea::assemble_montgomery_certificate(
        *twist_candidate.candidate, 3, twist_options);
    check(twist_certificate.has_value(), "direct twist certificate assembly");
    check(twist_certificate->order == 27, "direct twist assembled order");
    check(oneshotsea::validate_montgomery_certificate(*twist_certificate),
          "native direct-twist certificate validation");
    canonical_accepts(*twist_certificate);
}

void test_j_conversion_and_assembly() {
    const oneshotsea::MontgomeryCurve curve(oneshotsea::Field(101), 3);
    const mpz_class j = curve.j_invariant();
    const auto coefficients =
        oneshotsea::montgomery_coefficients_from_j(101, j);
    check(!coefficients.empty(), "j invariant has a Montgomery model");
    check(std::find(coefficients.begin(), coefficients.end(), mpz_class(3)) !=
              coefficients.end(),
          "j conversion recovers the original Montgomery coefficient");
    for (const mpz_class& coefficient : coefficients) {
        const oneshotsea::MontgomeryCurve recovered(
            oneshotsea::Field(101), coefficient);
        check(!recovered.is_singular() && recovered.j_invariant() == j,
              "every returned coefficient has the requested j invariant");
    }

    const auto candidate =
        oneshotsea::prepare_certificate_candidate(101, 96, 96);
    check(static_cast<bool>(candidate), "j-path candidate exists");
    const auto certificate =
        oneshotsea::assemble_montgomery_certificate_from_j(
            *candidate.candidate, j,
            {31, 400, oneshotsea::MontgomerySide::either});
    check(certificate.has_value(), "j-derived certificate assembly");
    check(oneshotsea::MontgomeryCurve(
              oneshotsea::Field(101), certificate->coefficient)
              .j_invariant() == j,
          "assembled j-derived model has the requested invariant");
    canonical_accepts(*certificate);

    const mpz_class target_prime(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237");
    const oneshotsea::MontgomeryCurve target_curve(
        oneshotsea::Field(target_prime), 3);
    const auto target_coefficients = oneshotsea::montgomery_coefficients_from_j(
        target_prime, target_curve.j_invariant());
    check(std::find(target_coefficients.begin(), target_coefficients.end(),
                    mpz_class(3)) != target_coefficients.end(),
          "j conversion recovers a model over the 416-bit target field");
}

void test_native_validation_regressions() {
    const oneshotsea::MontgomeryCertificate upstream_fixture{
        mpz_class("1000000000039"), mpz_class("834376266027"),
        mpz_class("472587544217"), mpz_class("2240187"), {3539}};
    check(oneshotsea::validate_montgomery_certificate(upstream_fixture),
          "native validator accepts pinned n^4 fixture");
    canonical_accepts(upstream_fixture);

    auto missing_large_factor = upstream_fixture;
    missing_large_factor.large_prime_divisors.clear();
    check(!oneshotsea::validate_montgomery_certificate(missing_large_factor),
          "native validator requires the exact large-prime list");

    const oneshotsea::MontgomeryCertificate composite_modulus{
        1003, 3, 24, 24, {}};
    check(!oneshotsea::validate_montgomery_certificate(composite_modulus),
          "native validator rejects the canonical composite regression");

    const oneshotsea::MontgomeryCertificate folded_factor{
        10000019, 6873344, 1, 3308, {1654}};
    check(!oneshotsea::validate_montgomery_certificate(folded_factor),
          "native validator rejects folded composite large factors");
}

}  // namespace

int main() {
    try {
        test_bounds_and_candidate_preparation();
        test_ladder_and_exact_order();
        test_direct_curve_and_twist_assembly();
        test_j_conversion_and_assembly();
        test_native_validation_regressions();
        std::cout << "certificate candidate/assembly tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
