#include "oneshotsea/curve.hpp"
#include "oneshotsea/early_abort.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/poly.hpp"
#include "oneshotsea/schoof.hpp"
#include "oneshotsea/trace.hpp"

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

    const oneshotsea::Poly temporary_field_poly(oneshotsea::Field(101), {1, 2});
    check(temporary_field_poly.evaluate(3) == 7,
          "polynomial owns a temporary field context");
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
}

void test_early_abort() {
    check(oneshotsea::trial_smooth_part(2 * 2 * 3 * 7 * 11, 7) == 2 * 2 * 3 * 7,
          "trial smooth part");
    check(oneshotsea::certificate_lower_bound(101) == 17,
          "integer certificate lower bound");

    oneshotsea::TraceConstraints constraints(101);
    constraints.refine(41, {0});  // only trace zero lies in [-20,20]
    const auto rejected = oneshotsea::screen_order_candidates(
        constraints, 4, 20,
        [](const mpz_class& order) { return oneshotsea::trial_smooth_part(order, 3); });
    check(rejected.has_value() && rejected->trace_count == 1,
          "early-abort exhaustive trace set");
    check(rejected->rejects_curve(), "sound screen rejects both sides");

    const auto retained = oneshotsea::screen_order_candidates(
        constraints, 4, 5,
        [](const mpz_class& order) { return oneshotsea::trial_smooth_part(order, 3); });
    check(retained.has_value() && retained->survivors.size() == 2,
          "sound screen retains curve and twist");

    oneshotsea::TraceConstraints many(101);
    check(!oneshotsea::screen_order_candidates(
               many, 4, 1, [](const mpz_class&) { return mpz_class(1); })
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

    // A coarse Phi_3 root is not by itself a sound Elkies classification in
    // supersingular/collision cases.  This curve has two rational modular
    // neighbors but no rational degree-one factor of psi_3.
    const oneshotsea::Curve collision(oneshotsea::Field(19), 8, 14);
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
}

}  // namespace

int main() {
    try {
        test_field();
        test_polynomial();
        test_curves();
        test_modular_polynomials();
        test_trace_constraints();
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
