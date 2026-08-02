#pragma once

#include "oneshotsea/weber_curve_generator.hpp"

#include <cstdint>
#include <optional>

namespace oneshotsea {

// This probe is deliberately separate from the production curve generator.
// Its formula identity is emitted by the CLI so retained measurements can pin
// the exact external equation that was implemented.
inline constexpr char kX111ProbeSchema[] = "oneshotsea.x1-11-probe.v1";
inline constexpr char kX111ProbeGeneratorVersion[] =
    "x1-11-tate-weber-montgomery-v2";
inline constexpr char kX111FormulaSourceUrl[] =
    "https://math.mit.edu/~drew/X1/X1opt11.txt";
inline constexpr char kX111FormulaSourceSha256[] =
    "19f76aef352cea9a6e1d3347977eb9286b03e70fa6b4afb8daea013ebbd6bd4c";

enum class X111CanonicalSide : std::uint8_t {
    curve,
    twist,
};

const char* x1_11_canonical_side_name(X111CanonicalSide side);

struct X111ProbeRejections {
    std::uint64_t x_samples = 0;
    std::uint64_t x_polynomials_without_roots = 0;
    std::uint64_t x1_points = 0;
    std::uint64_t singular_curves = 0;
    std::uint64_t exceptional_j = 0;
    std::uint64_t exact_order_11_failures = 0;
    std::uint64_t points_without_weber_lifts = 0;
    std::uint64_t weber_lifts = 0;
    std::uint64_t nonsquare_explicit_montgomery_u = 0;
    std::uint64_t points_without_explicit_montgomery_model = 0;
    std::uint64_t full_two_torsion_failures = 0;
    std::uint64_t point_four_rejections = 0;
    std::uint64_t accepted = 0;

    X111ProbeRejections& operator+=(const X111ProbeRejections& other);
};

struct X111ProbeOptions {
    // Maximum independently sampled X_1(11) x-coordinates for one global
    // index.  A bounded miss is an ordinary probe result, never an unbounded
    // retry or a production-search cursor decision.
    std::uint64_t max_x_samples = 1;
    // When true, require a rational point of order four on the same twist
    // class as the order-eleven Tate point.  Full rational E[2] is always
    // validated after the explicit Weber/Montgomery square gate.
    bool require_point_four = false;
};

struct X111ProbeSample {
    std::uint64_t global_index = 0;
    mpz_class x1_x;
    mpz_class x1_y;
    mpz_class tate_r;
    mpz_class tate_s;
    mpz_class tate_b;
    mpz_class tate_c;
    Curve tate_curve;
    mpz_class tate_point_x;
    mpz_class tate_point_y;
    mpz_class explicit_montgomery_coefficient;
    WeberCurvePair pair;
    X111CanonicalSide selected_side = X111CanonicalSide::curve;
    bool has_full_rational_two_torsion = false;
    bool has_point_order_four = false;
    // The cyclic divisor is known to occur as the order of one rational
    // point.  The group divisor additionally counts independent rational
    // 2-primary structure: it is 88 on the general point-four branch and 176
    // when p=5 mod 8, where the retained Weber identity proves a subgroup of
    // order 16 on the selected twist class.
    std::uint64_t cyclic_divisor = 0;
    std::uint64_t group_divisor = 0;
    mpz_class opposite_order_residue;
};

struct X111ProbeResult {
    std::optional<X111ProbeSample> sample;
    X111ProbeRejections counters;
};

// Deterministically sample the optimized X_1(11) equation
//
//   y^2 + (x^2+1)y + x = 0,
//   r=xy+1, s=1-x,
//
// map to Tate normal form and then short Weierstrass form, validate that the
// distinguished point has exact order eleven, and retain only rational Weber
// lifts for which U=4-f^24/16 is a square.  The field characteristic must be a
// probable prime greater than seven, different from eleven, and 1 modulo 4;
// this congruence holds for p125 and makes
// A^2-4=-(f^12/4)^2 a square and therefore forces full rational E[2].
X111ProbeResult deterministic_x1_11_probe(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    X111ProbeOptions options = {});

// Production curve-family entry point. Unlike the bounded diagnostic probe,
// this deterministically retries until it finds an admitted sample, exhausting
// only the uint64 attempt domain. The returned result always has a sample;
// prior rejected x-coordinate attempts are recorded in both the counters and
// sample.pair.rejected_samples.
X111ProbeResult deterministic_x1_11_search_curve(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    bool require_point_four = false);

}  // namespace oneshotsea
