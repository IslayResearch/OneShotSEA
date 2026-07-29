#include "oneshotsea/curve.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/poly.hpp"
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
    const auto division = oneshotsea::divmod(f, oneshotsea::sub(
        x, oneshotsea::Poly::constant(field, 2)));
    check(division.second.is_zero(), "polynomial exact division");
    check(division.first.evaluate(3) == 0, "polynomial quotient");
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

}  // namespace

int main() {
    try {
        test_field();
        test_polynomial();
        test_curves();
        test_modular_polynomials();
        test_trace_constraints();
        std::cout << "core tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "core tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
