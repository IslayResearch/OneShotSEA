#include "oneshotsea/x1_27_probe.hpp"

#include "oneshotsea/poly.hpp"
#include "oneshotsea/weber.hpp"

#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oneshotsea {
namespace {

constexpr std::uint64_t kX127SampleDomain =
    UINT64_C(0x5831323753414d50);  // "X127SAMP"
constexpr std::uint64_t kX127RootOrderDomain =
    UINT64_C(0x58313237524f4f54);  // "X127ROOT"

struct AffinePoint {
    mpz_class x = 0;
    mpz_class y = 0;
    bool infinity = true;
};

void require_probe_prime(const mpz_class& prime) {
    if (prime <= 7 || mpz_even_p(prime.get_mpz_t()) != 0 || prime == 3 ||
        mpz_probab_prime_p(prime.get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "X1(27) probe requires an odd probable prime greater than seven "
            "whose characteristic is not three");
    }
    if (mpz_fdiv_ui(prime.get_mpz_t(), 4U) != 1U) {
        throw std::invalid_argument(
            "X1(27) Weber/Montgomery full-2-torsion probe requires p=1 mod 4");
    }
}

mpz_class evaluate(const Field& field, const mpz_class& argument,
                   std::initializer_list<long> coefficients) {
    mpz_class value = 0;
    for (const long coefficient : coefficients) {
        value = field.add(field.mul(value, argument), coefficient);
    }
    return value;
}

Poly x1_27_polynomial(const Field& field, const mpz_class& u) {
    const mpz_class um1 = field.sub(u, 1);
    const mpz_class um1_squared = field.square(um1);
    const mpz_class u2pu1 = evaluate(field, u, {1, 1, 1});
    const mpz_class u5 = field.pow(u, 5);
    const mpz_class u6 = field.mul(u5, u);

    const mpz_class c6 = um1_squared;
    const mpz_class c5 = field.mul(
        um1_squared, field.add(field.pow(u, 3), 2));
    const mpz_class c4 = field.neg(field.mul(
        um1_squared, evaluate(field, u, {1, 2, -2, -1, -2, -1})));
    const mpz_class c3 = field.mul(
        field.mul(u, um1),
        evaluate(field, u, {1, -3, -4, 1, 1, 3, -2}));
    const mpz_class c2 = field.mul(
        field.mul(field.mul(u, um1), u2pu1),
        evaluate(field, u, {3, -4, -2, 1, -1}));
    const mpz_class c1 = field.mul(
        3, field.mul(field.mul(u5, um1), u2pu1));
    const mpz_class c0 = field.mul(u6, u2pu1);
    return Poly(field, {c0, c1, c2, c3, c4, c5, c6});
}

std::optional<std::pair<Curve, AffinePoint>> tate_curve_from_x1_point(
    const Field& field, const mpz_class& u, const mpz_class& v,
    mpz_class& r_out, mpz_class& s_out, mpz_class& a1_out,
    mpz_class& a2_out, mpz_class& a3_out) {
    if (u == 0) {
        return std::nullopt;
    }
    const mpz_class v_plus_one = field.add(v, 1);
    const mpz_class x_denominator = field.mul(u, v_plus_one);
    if (x_denominator == 0) {
        return std::nullopt;
    }
    const mpz_class g = field.neg(field.inverse(u));
    const mpz_class x = field.divide(v, x_denominator);
    const mpz_class x2 = field.square(x);
    const mpz_class x3 = field.mul(x2, x);
    const mpz_class g2 = field.square(g);
    const mpz_class g3 = field.mul(g2, g);
    const mpz_class g4 = field.square(g2);

    mpz_class y_numerator = field.add(field.mul(g4, x), g4);
    y_numerator = field.add(y_numerator, field.mul(g3, x));
    y_numerator = field.sub(
        y_numerator, field.mul(2, field.mul(g2, x2)));
    y_numerator = field.sub(y_numerator, field.mul(g, x3));
    y_numerator = field.add(y_numerator, g);
    y_numerator = field.add(y_numerator, x);

    mpz_class y_denominator = field.add(g4, field.mul(g3, x));
    y_denominator = field.sub(y_denominator, field.mul(g2, x2));
    y_denominator = field.sub(y_denominator, field.mul(g, x2));
    y_denominator = field.add(y_denominator, g);
    y_denominator = field.add(y_denominator, x);
    if (y_denominator == 0) {
        return std::nullopt;
    }
    const mpz_class y = field.divide(y_numerator, y_denominator);
    const mpz_class xy = field.mul(x, y);
    const mpz_class x2y = field.mul(x2, y);
    const mpz_class r_denominator = field.sub(x2y, x);
    const mpz_class s_denominator = xy;
    if (r_denominator == 0 || s_denominator == 0) {
        return std::nullopt;
    }
    const mpz_class r = field.divide(
        field.sub(field.add(field.sub(x2y, xy), y), 1), r_denominator);
    const mpz_class s = field.divide(
        field.add(field.sub(xy, y), 1), s_denominator);
    const mpz_class rs = field.mul(r, s);
    const mpz_class r2s = field.mul(r, rs);
    const mpz_class a1 = field.add(field.sub(s, rs), 1);
    const mpz_class a2 = field.sub(rs, r2s);
    const mpz_class a3 = a2;

    const mpz_class b2 = field.add(field.square(a1), field.mul(4, a2));
    const mpz_class b4 = field.mul(a1, a3);
    const mpz_class b6 = field.square(a3);
    const mpz_class c4 = field.sub(field.square(b2), field.mul(24, b4));
    mpz_class c6 = field.neg(field.mul(field.square(b2), b2));
    c6 = field.add(c6, field.mul(36, field.mul(b2, b4)));
    c6 = field.sub(c6, field.mul(216, b6));
    Curve short_curve(field, field.neg(field.mul(27, c4)),
                      field.neg(field.mul(54, c6)));
    const AffinePoint distinguished{
        field.mul(3, b2), field.mul(108, a3), false};
    r_out = r;
    s_out = s;
    a1_out = a1;
    a2_out = a2;
    a3_out = a3;
    return std::make_pair(std::move(short_curve), distinguished);
}

AffinePoint add_points(const Curve& curve, const AffinePoint& left,
                       const AffinePoint& right) {
    if (left.infinity) {
        return right;
    }
    if (right.infinity) {
        return left;
    }
    const Field& field = curve.field();
    if (left.x == right.x && field.add(left.y, right.y) == 0) {
        return {};
    }
    mpz_class slope;
    if (left.x == right.x && left.y == right.y) {
        if (left.y == 0) {
            return {};
        }
        slope = field.divide(
            field.add(field.mul(3, field.square(left.x)), curve.a()),
            field.mul(2, left.y));
    } else {
        slope = field.divide(field.sub(right.y, left.y),
                             field.sub(right.x, left.x));
    }
    const mpz_class result_x = field.sub(
        field.sub(field.square(slope), left.x), right.x);
    const mpz_class result_y = field.sub(
        field.mul(slope, field.sub(left.x, result_x)), left.y);
    return {result_x, result_y, false};
}

AffinePoint scalar_multiply(const Curve& curve, std::uint64_t scalar,
                            AffinePoint point) {
    AffinePoint result;
    while (scalar != 0U) {
        if ((scalar & 1U) != 0U) {
            result = add_points(curve, result, point);
        }
        scalar >>= 1U;
        if (scalar != 0U) {
            point = add_points(curve, point, point);
        }
    }
    return result;
}

bool point_is_on_curve(const Curve& curve, const AffinePoint& point) {
    if (point.infinity) {
        return true;
    }
    const Field& field = curve.field();
    return field.square(point.y) ==
           field.add(field.add(field.mul(field.square(point.x), point.x),
                               field.mul(curve.a(), point.x)),
                     curve.b());
}

bool has_exact_order_27(const Curve& curve, const AffinePoint& point) {
    return !point.infinity && point_is_on_curve(curve, point) &&
           scalar_multiply(curve, 27U, point).infinity &&
           !scalar_multiply(curve, 9U, point).infinity;
}

std::vector<mpz_class> rational_two_torsion_roots(const Curve& curve) {
    return linear_roots(
        Poly(curve.field(), {curve.b(), curve.a(), 0, 1}));
}

bool has_point_order_four_p1mod4(
    const Curve& curve, const std::vector<mpz_class>& roots) {
    if (roots.size() != 3U) {
        return false;
    }
    const Field& field = curve.field();
    const mpz_class derivative_at_first = field.add(
        field.mul(3, field.square(roots[0])), curve.a());
    const int derivative_character = field.legendre(derivative_at_first);
    if (derivative_character == 0) {
        throw std::logic_error(
            "nonsingular short curve has a repeated 2-torsion root");
    }
    return derivative_character == 1
               ? field.legendre(field.sub(roots[0], roots[1])) == 1
               : field.legendre(field.sub(roots[1], roots[2])) == 1;
}

X127CanonicalSide identify_canonical_side(const Curve& tate,
                                           const Curve& canonical) {
    if (tate.field().modulus() != canonical.field().modulus() ||
        tate.j_invariant() != canonical.j_invariant()) {
        throw std::invalid_argument(
            "canonical-side comparison requires equal fields and j-invariants");
    }
    if (tate.a() == 0 || tate.b() == 0 || canonical.a() == 0 ||
        canonical.b() == 0) {
        throw std::invalid_argument(
            "canonical-side comparison excludes j=0 and j=1728");
    }
    const Field& field = tate.field();
    const mpz_class a_ratio = field.divide(canonical.a(), tate.a());
    const mpz_class b_ratio = field.divide(canonical.b(), tate.b());
    const mpz_class scaling_square = field.divide(
        field.mul(canonical.b(), tate.a()),
        field.mul(tate.b(), canonical.a()));
    if (field.square(scaling_square) != a_ratio ||
        field.mul(field.square(scaling_square), scaling_square) != b_ratio) {
        throw std::logic_error(
            "same-j short curves failed the scaling-square identities");
    }
    const int character = field.legendre(scaling_square);
    if (character == 0) {
        throw std::logic_error("short-curve scaling square unexpectedly vanished");
    }
    return character == 1 ? X127CanonicalSide::curve
                          : X127CanonicalSide::twist;
}

void increment(std::uint64_t& value, const char* label) {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(std::string("X1(27) counter overflow: ") +
                                  label);
    }
    ++value;
}

void add_counter(std::uint64_t& target, std::uint64_t value,
                 const char* label) {
    if (value > std::numeric_limits<std::uint64_t>::max() - target) {
        throw std::overflow_error(std::string("X1(27) counter overflow: ") +
                                  label);
    }
    target += value;
}

std::uint64_t next_attempt(std::uint64_t attempt) {
    if (attempt == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("exhausted deterministic X1(27) retry space");
    }
    return attempt + 1U;
}

}  // namespace

const char* x1_27_canonical_side_name(X127CanonicalSide side) {
    switch (side) {
        case X127CanonicalSide::curve:
            return "curve";
        case X127CanonicalSide::twist:
            return "twist";
    }
    throw std::logic_error("unknown X1(27) canonical side");
}

X127ProbeRejections& X127ProbeRejections::operator+=(
    const X127ProbeRejections& other) {
    add_counter(u_samples, other.u_samples, "u samples");
    add_counter(u_polynomials_without_roots,
                other.u_polynomials_without_roots, "rootless polynomials");
    add_counter(x1_points, other.x1_points, "X1 points");
    add_counter(exceptional_map_points, other.exceptional_map_points,
                "exceptional map points");
    add_counter(singular_curves, other.singular_curves, "singular curves");
    add_counter(exceptional_j, other.exceptional_j, "exceptional j");
    add_counter(exact_order_27_failures, other.exact_order_27_failures,
                "exact-order failures");
    add_counter(points_without_weber_lifts, other.points_without_weber_lifts,
                "points without Weber lifts");
    add_counter(weber_lifts, other.weber_lifts, "Weber lifts");
    add_counter(nonsquare_explicit_montgomery_u,
                other.nonsquare_explicit_montgomery_u,
                "nonsquare Montgomery U");
    add_counter(points_without_explicit_montgomery_model,
                other.points_without_explicit_montgomery_model,
                "points without explicit Montgomery model");
    add_counter(full_two_torsion_failures, other.full_two_torsion_failures,
                "full 2-torsion failures");
    add_counter(point_four_rejections, other.point_four_rejections,
                "point-four rejections");
    add_counter(accepted, other.accepted, "accepted");
    return *this;
}

namespace {

X127ProbeResult deterministic_x1_27_impl(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    std::optional<std::uint64_t> maximum_u_samples,
    bool require_point_four) {
    require_probe_prime(prime);
    const Field field(prime);
    X127ProbeResult result;

    for (std::uint64_t attempt = 0;
         !maximum_u_samples.has_value() || attempt < *maximum_u_samples;
         attempt = next_attempt(attempt)) {
        increment(result.counters.u_samples, "u samples");
        const mpz_class u = deterministic_residue(
            field, seed, global_index,
            splitmix64(kX127SampleDomain ^ attempt));
        const std::vector<mpz_class> roots = linear_roots(
            x1_27_polynomial(field, u));
        if (roots.empty()) {
            increment(result.counters.u_polynomials_without_roots,
                      "rootless polynomials");
            continue;
        }
        const std::size_t root_offset = static_cast<std::size_t>(
            splitmix64(kX127RootOrderDomain ^ seed ^ global_index ^ attempt) %
            roots.size());
        for (std::size_t root_index = 0; root_index < roots.size();
             ++root_index) {
            increment(result.counters.x1_points, "X1 points");
            const mpz_class v = roots[(root_offset + root_index) % roots.size()];
            mpz_class r;
            mpz_class s;
            mpz_class a1;
            mpz_class a2;
            mpz_class a3;
            std::optional<std::pair<Curve, AffinePoint>> mapped =
                tate_curve_from_x1_point(field, u, v, r, s, a1, a2, a3);
            if (!mapped.has_value()) {
                increment(result.counters.exceptional_map_points,
                          "exceptional map points");
                continue;
            }
            Curve tate_curve = std::move(mapped->first);
            const AffinePoint distinguished = mapped->second;
            if (tate_curve.is_singular()) {
                increment(result.counters.singular_curves, "singular curves");
                continue;
            }
            const mpz_class j = tate_curve.j_invariant();
            if (j == 0 || j == field.normalize(1728)) {
                increment(result.counters.exceptional_j, "exceptional j");
                continue;
            }
            if (!has_exact_order_27(tate_curve, distinguished)) {
                increment(result.counters.exact_order_27_failures,
                          "exact-order failures");
                continue;
            }

            const std::vector<mpz_class> lifts = weber_f_lifts(field, j);
            if (lifts.empty()) {
                increment(result.counters.points_without_weber_lifts,
                          "points without Weber lifts");
                continue;
            }
            add_counter(result.counters.weber_lifts,
                        static_cast<std::uint64_t>(lifts.size()),
                        "Weber lifts");
            std::optional<std::pair<mpz_class, mpz_class>> explicit_model;
            for (const mpz_class& lift : lifts) {
                const mpz_class z = field.pow(lift, 24);
                const mpz_class montgomery_u =
                    field.sub(4, field.divide(z, 16));
                const std::vector<mpz_class> coefficients = linear_roots(
                    Poly(field, {field.neg(montgomery_u), 0, 1}));
                if (coefficients.empty()) {
                    increment(result.counters.nonsquare_explicit_montgomery_u,
                              "nonsquare Montgomery U");
                    continue;
                }
                const mpz_class montgomery_a = coefficients.front();
                const MontgomeryCurve montgomery(field, montgomery_a);
                if (montgomery.is_singular() || montgomery.j_invariant() != j ||
                    field.square(montgomery_a) != montgomery_u) {
                    throw std::logic_error(
                        "explicit Weber/Montgomery identity validation failed");
                }
                explicit_model.emplace(lift, montgomery_a);
                break;
            }
            if (!explicit_model.has_value()) {
                increment(result.counters.points_without_explicit_montgomery_model,
                          "points without explicit Montgomery model");
                continue;
            }

            const mpz_class& lift = explicit_model->first;
            const mpz_class& montgomery_a = explicit_model->second;
            const mpz_class two_torsion_discriminant =
                field.sub(field.square(montgomery_a), 4);
            const std::vector<mpz_class> two_torsion_roots =
                rational_two_torsion_roots(tate_curve);
            if (field.legendre(two_torsion_discriminant) != 1 ||
                two_torsion_roots.size() != 3U) {
                increment(result.counters.full_two_torsion_failures,
                          "full 2-torsion failures");
                continue;
            }
            const bool point_four =
                has_point_order_four_p1mod4(tate_curve, two_torsion_roots);
            if (require_point_four && !point_four) {
                increment(result.counters.point_four_rejections,
                          "point-four rejections");
                continue;
            }

            WeberCurvePair pair = weber_curve_pair_from_f(field, lift);
            if (pair.j_invariant != j) {
                throw std::logic_error(
                    "X1(27) Tate and Weber j-invariants disagree");
            }
            const X127CanonicalSide side =
                identify_canonical_side(tate_curve, pair.curve);
            const std::uint64_t cyclic_divisor = point_four ? 108U : 54U;
            const bool point_four_sixteen_divisor =
                point_four && mpz_fdiv_ui(prime.get_mpz_t(), 8U) == 5U;
            const std::uint64_t group_divisor =
                point_four_sixteen_divisor ? 432U
                                           : (point_four ? 216U : 108U);
            const mpz_class opposite_residue =
                (2 * (prime + 1)) %
                mpz_class(static_cast<unsigned long>(group_divisor));
            increment(result.counters.accepted, "accepted");
            result.sample.emplace(X127ProbeSample{
                global_index,
                u,
                v,
                r,
                s,
                a1,
                a2,
                a3,
                std::move(tate_curve),
                distinguished.x,
                distinguished.y,
                montgomery_a,
                std::move(pair),
                side,
                true,
                point_four,
                cyclic_divisor,
                group_divisor,
                opposite_residue,
            });
            return result;
        }
    }
    return result;
}

}  // namespace

X127ProbeResult deterministic_x1_27_probe(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    X127ProbeOptions options) {
    if (options.max_u_samples == 0U) {
        throw std::invalid_argument(
            "X1(27) probe max_u_samples must be positive");
    }
    return deterministic_x1_27_impl(
        prime, seed, global_index, options.max_u_samples,
        options.require_point_four);
}

X127ProbeResult deterministic_x1_27_search_curve(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    bool require_point_four) {
    X127ProbeResult result = deterministic_x1_27_impl(
        prime, seed, global_index, std::nullopt, require_point_four);
    if (!result.sample.has_value() || result.counters.u_samples == 0U) {
        throw std::logic_error(
            "unbounded X1(27) generator returned without an admitted sample");
    }
    result.sample->pair.rejected_samples = result.counters.u_samples - 1U;
    return result;
}

}  // namespace oneshotsea
