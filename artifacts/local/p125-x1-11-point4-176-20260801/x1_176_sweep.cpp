#include "oneshotsea/curve.hpp"
#include "oneshotsea/poly.hpp"
#include "oneshotsea/weber.hpp"
#include "oneshotsea/x1_11_probe.hpp"

#include <gmpxx.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool prime_u64(std::uint64_t p) {
    mpz_class z(static_cast<unsigned long>(p));
    return mpz_probab_prime_p(z.get_mpz_t(), 25) != 0;
}

struct Point4Decision {
    bool full_e2 = false;
    bool point4 = false;
    int derivative_character = 0;
};

Point4Decision classify_point4(const oneshotsea::Curve& curve) {
    const oneshotsea::Field& f = curve.field();
    const unsigned long p = f.modulus().get_ui();
    std::vector<mpz_class> roots;
    for (unsigned long xu = 0; xu < p; ++xu) {
        const mpz_class x = xu;
        if (f.add(f.add(f.mul(f.square(x), x), f.mul(curve.a(), x)),
                  curve.b()) == 0) {
            roots.push_back(x);
        }
    }
    if (roots.size() != 3) return {};
    const mpz_class derivative =
        f.add(f.mul(3, f.square(roots[0])), curve.a());
    const int dc = f.legendre(derivative);
    bool p4 = false;
    if (dc == 1) {
        p4 = f.legendre(f.sub(roots[0], roots[1])) == 1;
    } else if (dc == -1) {
        p4 = f.legendre(f.sub(roots[1], roots[2])) == 1;
    }
    return {true, p4, dc};
}

oneshotsea::Curve tate_curve_from_xy(const oneshotsea::Field& f,
                                     const mpz_class& x,
                                     const mpz_class& y) {
    const mpz_class r = f.add(f.mul(x, y), 1);
    const mpz_class s = f.sub(1, x);
    const mpz_class c = f.mul(s, f.sub(r, 1));
    const mpz_class b = f.mul(r, c);
    const mpz_class a = f.sub(c, 1);
    const mpz_class e = f.sub(f.square(a), f.mul(4, b));
    const mpz_class short_a = f.mul(
        27, f.sub(f.mul(24, f.mul(a, b)), f.square(e)));
    const mpz_class short_b = f.mul(
        54, f.add(f.sub(f.mul(f.square(e), e),
                          f.mul(36, f.mul(f.mul(a, b), e))),
                  f.mul(216, f.square(b))));
    return oneshotsea::Curve(f, short_a, short_b);
}

struct Totals {
    std::uint64_t primes = 0;
    std::uint64_t x1_points = 0;
    std::uint64_t singular = 0;
    std::uint64_t exceptional0 = 0;
    std::uint64_t exceptional1728 = 0;
    std::uint64_t full_e2 = 0;
    std::uint64_t point4 = 0;
    std::uint64_t branch_plus = 0;
    std::uint64_t branch_minus = 0;
    std::uint64_t n_not_176 = 0;
    std::uint64_t weber_admitted = 0;
    std::uint64_t admitted_not_176 = 0;
    std::uint64_t admitted_side_curve = 0;
    std::uint64_t admitted_side_twist = 0;
    std::uint64_t admitted_branch_plus = 0;
    std::uint64_t admitted_branch_minus = 0;
};

void exhaustive_tate_sweep(std::uint64_t maximum, std::uint64_t congruence,
                           Totals& t) {
    bool printed_exception0 = false;
    bool printed_exception1728 = false;
    bool printed_bad = false;
    bool printed_admitted_bad = false;
    bool printed_admitted = false;
    for (std::uint64_t p = 13; p <= maximum; ++p) {
        if (p % 8 != congruence || !prime_u64(p) || p == 11) continue;
        ++t.primes;
        const oneshotsea::Field f{
            mpz_class(static_cast<unsigned long>(p))};
        std::vector<long> square_root(p, -1);
        for (std::uint64_t yu = 0; yu < p; ++yu) {
            square_root[(yu * yu) % p] = static_cast<long>(yu);
        }
        const mpz_class inv2 = f.inverse(2);
        for (std::uint64_t xu = 0; xu < p; ++xu) {
            const mpz_class x(static_cast<unsigned long>(xu));
            const mpz_class coefficient = f.add(f.square(x), 1);
            const mpz_class discriminant =
                f.sub(f.square(coefficient), f.mul(4, x));
            const long square_root_value =
                square_root[discriminant.get_ui()];
            std::vector<mpz_class> ys;
            if (square_root_value >= 0) {
                const mpz_class root(static_cast<unsigned long>(square_root_value));
                ys.push_back(f.mul(f.sub(root, coefficient), inv2));
                const mpz_class other = f.mul(f.sub(f.neg(root), coefficient), inv2);
                if (other != ys.front()) ys.push_back(other);
            }
            for (const mpz_class& y : ys) {
                ++t.x1_points;
                const auto curve = tate_curve_from_xy(f, x, y);
                if (curve.is_singular()) {
                    ++t.singular;
                    continue;
                }
                const mpz_class j = curve.j_invariant();
                if (j == 0) {
                    ++t.exceptional0;
                    if (!printed_exception0) {
                        std::cout << "EXCEPTION_J0 congruence=" << congruence
                                  << " p=" << p << " x=" << x << " y=" << y
                                  << "\n";
                        printed_exception0 = true;
                    }
                    continue;
                }
                if (j == f.normalize(1728)) {
                    ++t.exceptional1728;
                    if (!printed_exception1728) {
                        std::cout << "EXCEPTION_J1728 congruence=" << congruence
                                  << " p=" << p << " x=" << x << " y=" << y
                                  << "\n";
                        printed_exception1728 = true;
                    }
                    continue;
                }
                const auto decision = classify_point4(curve);
                if (!decision.full_e2) continue;
                ++t.full_e2;
                if (!decision.point4) continue;
                ++t.point4;
                if (decision.derivative_character == 1) ++t.branch_plus;
                if (decision.derivative_character == -1) ++t.branch_minus;
                const mpz_class n = oneshotsea::count_points_bruteforce(curve);
                if (mpz_fdiv_ui(n.get_mpz_t(), 176) != 0) {
                    ++t.n_not_176;
                    if (!printed_bad) {
                        std::cout << "FIRST_TATE_NOT176 congruence=" << congruence
                                  << " p=" << p << " x=" << x << " y=" << y
                                  << " j=" << j << " N=" << n
                                  << " Nmod176="
                                  << mpz_fdiv_ui(n.get_mpz_t(), 176)
                                  << " branch=" << decision.derivative_character
                                  << "\n";
                        printed_bad = true;
                    }
                }
                const auto lifts = oneshotsea::weber_f_lifts(f, j);
                for (const mpz_class& lift : lifts) {
                    const mpz_class z = f.pow(lift, 24);
                    const mpz_class u = f.sub(4, f.divide(z, 16));
                    if (u != 0 && f.legendre(u) != 1) continue;
                    const auto pair = oneshotsea::weber_curve_pair_from_f(f, lift);
                    const mpz_class n_curve =
                        oneshotsea::count_points_bruteforce(pair.curve);
                    const bool side_curve = n_curve == n;
                    const bool side_twist =
                        (2 * (mpz_class(static_cast<unsigned long>(p)) + 1) -
                         n_curve) == n;
                    if (!side_curve && !side_twist) {
                        std::cerr << "selected side mismatch p=" << p << " j=" << j
                                  << "\n";
                        return;
                    }
                    ++t.weber_admitted;
                    if (side_curve) ++t.admitted_side_curve;
                    if (side_twist) ++t.admitted_side_twist;
                    if (decision.derivative_character == 1)
                        ++t.admitted_branch_plus;
                    if (decision.derivative_character == -1)
                        ++t.admitted_branch_minus;
                    if (!printed_admitted) {
                        std::cout << "FIRST_ADMITTED congruence=" << congruence
                                  << " p=" << p << " x=" << x << " y=" << y
                                  << " j=" << j << " f=" << lift
                                  << " side=" << (side_curve ? "curve" : "twist")
                                  << " N=" << n << " Nmod176="
                                  << mpz_fdiv_ui(n.get_mpz_t(), 176)
                                  << " branch=" << decision.derivative_character
                                  << "\n";
                        printed_admitted = true;
                    }
                    if (mpz_fdiv_ui(n.get_mpz_t(), 176) != 0) {
                        ++t.admitted_not_176;
                        if (!printed_admitted_bad) {
                            std::cout << "FIRST_ADMITTED_NOT176 congruence="
                                      << congruence << " p=" << p << " x=" << x
                                      << " y=" << y << " j=" << j << " f=" << lift
                                      << " side=" << (side_curve ? "curve" : "twist")
                                      << " N=" << n << " Nmod176="
                                      << mpz_fdiv_ui(n.get_mpz_t(), 176)
                                      << " branch=" << decision.derivative_character
                                      << "\n";
                            printed_admitted_bad = true;
                        }
                    }
                    break;
                }
            }
        }
    }
}

struct ApiTotals {
    std::uint64_t samples = 0;
    std::uint64_t side_curve = 0;
    std::uint64_t side_twist = 0;
    std::uint64_t branch_plus = 0;
    std::uint64_t branch_minus = 0;
    std::uint64_t n_not_176 = 0;
    std::uint64_t selected_mismatch = 0;
};

void api_sweep(std::uint64_t maximum, std::uint64_t congruence,
               std::uint64_t seeds_per_prime, std::uint64_t indices_per_seed,
               ApiTotals& t) {
    bool printed_bad = false;
    bool printed_p1_witness = false;
    for (std::uint64_t p = 13; p <= maximum; ++p) {
        if (p % 8 != congruence || !prime_u64(p) || p == 11) continue;
        for (std::uint64_t seed_offset = 0; seed_offset < seeds_per_prime;
             ++seed_offset) {
            const std::uint64_t seed = UINT64_C(0x17600000) +
                                       p * 131U + seed_offset;
            for (std::uint64_t index = 0; index < indices_per_seed; ++index) {
                const auto result = oneshotsea::deterministic_x1_11_search_curve(
                    mpz_class(static_cast<unsigned long>(p)), seed, index, true);
                const auto& sample = *result.sample;
                ++t.samples;
                const auto decision = classify_point4(sample.tate_curve);
                if (decision.derivative_character == 1) ++t.branch_plus;
                if (decision.derivative_character == -1) ++t.branch_minus;
                if (sample.selected_side == oneshotsea::X111CanonicalSide::curve)
                    ++t.side_curve;
                else
                    ++t.side_twist;
                const mpz_class n_tate =
                    oneshotsea::count_points_bruteforce(sample.tate_curve);
                const auto& selected =
                    sample.selected_side == oneshotsea::X111CanonicalSide::curve
                        ? sample.pair.curve
                        : sample.pair.twist;
                const mpz_class n_selected =
                    oneshotsea::count_points_bruteforce(selected);
                if (n_selected != n_tate) ++t.selected_mismatch;
                if (mpz_fdiv_ui(n_selected.get_mpz_t(), 176) != 0) {
                    ++t.n_not_176;
                    if (!printed_bad) {
                        std::cout << "FIRST_API_NOT176 congruence=" << congruence
                                  << " p=" << p << " seed=" << seed
                                  << " index=" << index
                                  << " side="
                                  << oneshotsea::x1_11_canonical_side_name(
                                         sample.selected_side)
                                  << " x=" << sample.x1_x
                                  << " y=" << sample.x1_y << " N=" << n_selected
                                  << " Nmod176="
                                  << mpz_fdiv_ui(n_selected.get_mpz_t(), 176)
                                  << " branch=" << decision.derivative_character
                                  << " attempts=" << result.counters.x_samples
                                  << "\n";
                        printed_bad = true;
                    }
                    if (congruence == 1 && !printed_p1_witness &&
                        mpz_fdiv_ui(n_selected.get_mpz_t(), 88) == 0) {
                        std::cout << "P1MOD8_POLICY88_WITNESS p=" << p
                                  << " seed=" << seed << " index=" << index
                                  << " side="
                                  << oneshotsea::x1_11_canonical_side_name(
                                         sample.selected_side)
                                  << " x=" << sample.x1_x
                                  << " y=" << sample.x1_y << " N=" << n_selected
                                  << " Nmod176="
                                  << mpz_fdiv_ui(n_selected.get_mpz_t(), 176)
                                  << " branch=" << decision.derivative_character
                                  << " attempts=" << result.counters.x_samples
                                  << "\n";
                        printed_p1_witness = true;
                    }
                }
            }
        }
    }
}

void print_totals(const std::string& label, const Totals& t) {
    std::cout << label << " primes=" << t.primes
              << " x1_points=" << t.x1_points << " singular=" << t.singular
              << " exceptional0=" << t.exceptional0
              << " exceptional1728=" << t.exceptional1728
              << " full_e2=" << t.full_e2 << " point4=" << t.point4
              << " branch_plus=" << t.branch_plus
              << " branch_minus=" << t.branch_minus
              << " n_not_176=" << t.n_not_176
              << " weber_admitted=" << t.weber_admitted
              << " admitted_not_176=" << t.admitted_not_176
              << " admitted_side_curve=" << t.admitted_side_curve
              << " admitted_side_twist=" << t.admitted_side_twist
              << " admitted_branch_plus=" << t.admitted_branch_plus
              << " admitted_branch_minus=" << t.admitted_branch_minus
              << std::endl;
}

void print_api(const std::string& label, const ApiTotals& t) {
    std::cout << label << " samples=" << t.samples
              << " side_curve=" << t.side_curve
              << " side_twist=" << t.side_twist
              << " branch_plus=" << t.branch_plus
              << " branch_minus=" << t.branch_minus
              << " n_not_176=" << t.n_not_176
              << " selected_mismatch=" << t.selected_mismatch << std::endl;
}

}  // namespace

int main() {
    Totals exhaustive5, exhaustive1;
    exhaustive_tate_sweep(1009, 5, exhaustive5);
    exhaustive_tate_sweep(1009, 1, exhaustive1);
    print_totals("EXHAUSTIVE_P5MOD8", exhaustive5);
    print_totals("EXHAUSTIVE_P1MOD8", exhaustive1);

    // Deterministic API seed sweep is run separately after identifying a
    // small admitted fixture; unbounded search is deliberately avoided here.
    return 0;
}
