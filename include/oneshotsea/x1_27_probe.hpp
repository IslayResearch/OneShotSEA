#pragma once

#include "oneshotsea/weber_curve_generator.hpp"

#include <cstdint>
#include <optional>

namespace oneshotsea {

inline constexpr char kX127ProbeSchema[] = "oneshotsea.x1-27-probe.v1";
inline constexpr char kX127ProbeGeneratorVersion[] =
    "x1-27-sutherland-tate-weber-montgomery-v1";
inline constexpr char kX127FormulaSourceUrl[] =
    "https://math.mit.edu/~drew/X1/X1opt27new.txt";
inline constexpr char kX127FormulaSourceSha256[] =
    "b63a2527b1778acce2fa7d003655d929c1687eec9902b03982e729e11a571250";

enum class X127CanonicalSide : std::uint8_t {
    curve,
    twist,
};

const char* x1_27_canonical_side_name(X127CanonicalSide side);

struct X127ProbeRejections {
    std::uint64_t u_samples = 0;
    std::uint64_t u_polynomials_without_roots = 0;
    std::uint64_t x1_points = 0;
    std::uint64_t exceptional_map_points = 0;
    std::uint64_t singular_curves = 0;
    std::uint64_t exceptional_j = 0;
    std::uint64_t exact_order_27_failures = 0;
    std::uint64_t points_without_weber_lifts = 0;
    std::uint64_t weber_lifts = 0;
    std::uint64_t nonsquare_explicit_montgomery_u = 0;
    std::uint64_t points_without_explicit_montgomery_model = 0;
    std::uint64_t full_two_torsion_failures = 0;
    std::uint64_t point_four_rejections = 0;
    std::uint64_t accepted = 0;

    X127ProbeRejections& operator+=(const X127ProbeRejections& other);
};

struct X127ProbeOptions {
    std::uint64_t max_u_samples = 1;
    bool require_point_four = false;
};

struct X127ProbeSample {
    std::uint64_t global_index = 0;
    mpz_class x1_u;
    mpz_class x1_v;
    mpz_class tate_r;
    mpz_class tate_s;
    mpz_class tate_a1;
    mpz_class tate_a2;
    mpz_class tate_a3;
    Curve tate_curve;
    mpz_class tate_point_x;
    mpz_class tate_point_y;
    mpz_class explicit_montgomery_coefficient;
    WeberCurvePair pair;
    X127CanonicalSide selected_side = X127CanonicalSide::curve;
    bool has_full_rational_two_torsion = false;
    bool has_point_order_four = false;
    // Full E[2] plus the order-27 point gives group divisor 108 and cyclic
    // divisor 54.  Point four promotes these to 216/108; on p=5 mod 8 the
    // retained Weber identity proves a 2-primary subgroup of order 16, hence
    // group divisor 432 while the conservative cyclic divisor stays 108.
    std::uint64_t cyclic_divisor = 0;
    std::uint64_t group_divisor = 0;
    mpz_class opposite_order_residue;
};

struct X127ProbeResult {
    std::optional<X127ProbeSample> sample;
    X127ProbeRejections counters;
};

// Deterministically sample Sutherland's optimized X_1(27) model.  The source
// equation is implemented directly as field arithmetic rather than vendored;
// the identity above pins the primary mathematical source.
X127ProbeResult deterministic_x1_27_probe(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    X127ProbeOptions options = {});

X127ProbeResult deterministic_x1_27_search_curve(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    bool require_point_four = false);

}  // namespace oneshotsea
