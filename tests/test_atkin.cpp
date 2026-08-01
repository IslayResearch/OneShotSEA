#include "oneshotsea/curve.hpp"
#include "oneshotsea/factor.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/trace.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Matrix2 {
    std::uint64_t a;
    std::uint64_t b;
    std::uint64_t c;
    std::uint64_t d;
};

Matrix2 multiply(const Matrix2& lhs, const Matrix2& rhs,
                 std::uint64_t modulus) {
    const auto product = [modulus](std::uint64_t a, std::uint64_t b) {
        return static_cast<std::uint64_t>(
            static_cast<unsigned __int128>(a) * b % modulus);
    };
    return {
        (product(lhs.a, rhs.a) + product(lhs.b, rhs.c)) % modulus,
        (product(lhs.a, rhs.b) + product(lhs.b, rhs.d)) % modulus,
        (product(lhs.c, rhs.a) + product(lhs.d, rhs.c)) % modulus,
        (product(lhs.c, rhs.b) + product(lhs.d, rhs.d)) % modulus,
    };
}

std::uint64_t brute_projective_order(std::uint64_t ell, std::uint64_t p,
                                     std::uint64_t trace) {
    const Matrix2 frobenius{0U, (ell - p % ell) % ell, 1U, trace % ell};
    Matrix2 power{1U, 0U, 0U, 1U};
    for (std::uint64_t order = 1U; order <= ell + 1U; ++order) {
        power = multiply(power, frobenius, ell);
        if (power.b == 0U && power.c == 0U && power.a == power.d) {
            return order;
        }
    }
    throw std::runtime_error("Atkin matrix exceeded ell+1 in PGL");
}

std::uint64_t euler_phi(std::uint64_t value) {
    std::uint64_t count = 0U;
    for (std::uint64_t candidate = 1U; candidate <= value; ++candidate) {
        if (std::gcd(candidate, value) == 1U) {
            ++count;
        }
    }
    return count;
}

void test_projective_order_and_residue_sets() {
    for (const std::uint64_t ell :
         {5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U, 31U, 37U, 41U, 43U}) {
        for (const unsigned long p : {101UL, 103UL, 107UL, 109UL, 127UL}) {
            if (p % ell == 0U) {
                continue;
            }
            const auto coarse = oneshotsea::trace_residues_from_classification(
                ell, mpz_class(p), false);
            std::vector<std::uint64_t> observed_orders;
            for (const std::uint64_t trace : coarse) {
                const std::uint64_t order =
                    oneshotsea::projective_frobenius_order(
                        ell, mpz_class(p), trace);
                check(order == brute_projective_order(ell, p, trace),
                      "PGL order agrees with independent repeated multiplication");
                observed_orders.push_back(order);
            }
            std::sort(observed_orders.begin(), observed_orders.end());
            observed_orders.erase(
                std::unique(observed_orders.begin(), observed_orders.end()),
                observed_orders.end());
            std::vector<std::uint64_t> reconstructed;
            for (const std::uint64_t order : observed_orders) {
                const auto residues =
                    oneshotsea::atkin_trace_residues_from_projective_order(
                        ell, mpz_class(p), order);
                check(residues.size() == euler_phi(order),
                      "Atkin order r has phi(r) trace residues");
                reconstructed.insert(
                    reconstructed.end(), residues.begin(), residues.end());
            }
            std::sort(reconstructed.begin(), reconstructed.end());
            check(reconstructed == coarse,
                  "projective-order residue sets partition coarse Atkin residues");
        }
    }
}

void test_classical_factor_degrees() {
    const struct Fixture {
        std::uint64_t ell;
        const char* path;
    } fixtures[] = {
        {5U, "data/modpoly/j/phi_5.txt"},
        {7U, "data/modpoly/j/phi_7.txt"},
    };
    std::size_t checked = 0U;
    for (const Fixture& fixture : fixtures) {
        const auto modular_polynomial =
            oneshotsea::SparseModularPolynomial::load(
                static_cast<unsigned>(fixture.ell), fixture.path);
        for (const unsigned long p : {11UL, 13UL, 17UL, 19UL, 23UL, 29UL}) {
            if (p == fixture.ell) {
                continue;
            }
            for (unsigned long index = 0U; index < 12U; ++index) {
                const auto curve = oneshotsea::deterministic_curve(
                    p, UINT64_C(0x61746b696e), index);
                if (curve.is_singular() || curve.j_invariant() == 0 ||
                    curve.j_invariant() == 1728 % p) {
                    continue;
                }
                const mpz_class trace = mpz_class(p) + 1 -
                    oneshotsea::count_points_bruteforce(curve);
                const std::uint64_t residue =
                    mpz_fdiv_ui(trace.get_mpz_t(), fixture.ell);
                const auto coarse =
                    oneshotsea::trace_residues_from_classification(
                        fixture.ell, mpz_class(p), false);
                if (!std::binary_search(coarse.begin(), coarse.end(), residue)) {
                    continue;
                }
                const auto factors = oneshotsea::factor_polynomial(
                    modular_polynomial.evaluate_x(
                        curve.field(), curve.j_invariant()));
                const bool square_free = std::all_of(
                    factors.begin(), factors.end(), [](const auto& factor) {
                        return factor.multiplicity == 1UL;
                    });
                if (!square_free || factors.empty()) {
                    continue;
                }
                const std::uint64_t order =
                    oneshotsea::projective_frobenius_order(
                        fixture.ell, mpz_class(p), residue);
                check(std::all_of(
                          factors.begin(), factors.end(),
                          [order](const auto& factor) {
                              return factor.polynomial.degree() ==
                                  static_cast<int>(order);
                          }),
                      "classical modular factor degree equals PGL order");
                const auto allowed =
                    oneshotsea::atkin_trace_residues_from_projective_order(
                        fixture.ell, mpz_class(p), order);
                check(std::binary_search(allowed.begin(), allowed.end(), residue),
                      "factor-derived Atkin constraint retains the true trace");
                ++checked;
            }
        }
    }
    check(checked >= 40U,
          "classical factor-degree differential has broad Atkin coverage");
}

}  // namespace

int main() {
    try {
        test_projective_order_and_residue_sets();
        test_classical_factor_degrees();
        std::cout << "Atkin projective-order tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
