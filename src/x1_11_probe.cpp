#include "oneshotsea/x1_11_probe.hpp"

#include "oneshotsea/poly.hpp"
#include "oneshotsea/weber.hpp"

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oneshotsea {
namespace {

constexpr std::uint64_t kX111SampleDomain =
    UINT64_C(0x58313153414d504c);  // "X11SAMPL"
constexpr std::uint64_t kX111RootOrderDomain =
    UINT64_C(0x583131524f4f544f);  // "X11ROOTO"

struct AffinePoint {
    mpz_class x = 0;
    mpz_class y = 0;
    bool infinity = true;
};

void require_probe_prime(const mpz_class& prime) {
    if (prime <= 7 || mpz_even_p(prime.get_mpz_t()) != 0 || prime == 11 ||
        mpz_probab_prime_p(prime.get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "X1(11) probe requires an odd probable prime greater than seven "
            "whose characteristic is not eleven");
    }
    if (mpz_fdiv_ui(prime.get_mpz_t(), 4U) != 1U) {
        throw std::invalid_argument(
            "X1(11) Weber/Montgomery full-2-torsion probe requires p=1 mod 4");
    }
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
    const mpz_class x = field.sub(
        field.sub(field.square(slope), left.x), right.x);
    const mpz_class y = field.sub(
        field.mul(slope, field.sub(left.x, x)), left.y);
    return {x, y, false};
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

bool has_exact_order_eleven(const Curve& curve, const AffinePoint& point) {
    return !point.infinity && point_is_on_curve(curve, point) &&
           scalar_multiply(curve, 11U, point).infinity;
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
    // Sutherland, Proposition 2, q=1 mod 4 and n=3.  Sorting does not alter
    // either decision: swapping the other roots changes the tested value by a
    // square-character-equivalent factor (and -1 is a square here).
    return derivative_character == 1
               ? field.legendre(field.sub(roots[0], roots[1])) == 1
               : field.legendre(field.sub(roots[1], roots[2])) == 1;
}

X111CanonicalSide identify_canonical_side(const Curve& tate,
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
    return character == 1 ? X111CanonicalSide::curve
                          : X111CanonicalSide::twist;
}

void increment(std::uint64_t& value, const char* label) {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(std::string("X1(11) counter overflow: ") +
                                  label);
    }
    ++value;
}

void add_counter(std::uint64_t& target, std::uint64_t value,
                 const char* label) {
    if (value > std::numeric_limits<std::uint64_t>::max() - target) {
        throw std::overflow_error(std::string("X1(11) counter overflow: ") +
                                  label);
    }
    target += value;
}

}  // namespace

const char* x1_11_canonical_side_name(X111CanonicalSide side) {
    switch (side) {
        case X111CanonicalSide::curve:
            return "curve";
        case X111CanonicalSide::twist:
            return "twist";
    }
    throw std::logic_error("unknown X1(11) canonical side");
}

X111ProbeRejections& X111ProbeRejections::operator+=(
    const X111ProbeRejections& other) {
    add_counter(x_samples, other.x_samples, "x samples");
    add_counter(x_polynomials_without_roots,
                other.x_polynomials_without_roots,
                "rootless X1 polynomials");
    add_counter(x1_points, other.x1_points, "X1 points");
    add_counter(singular_curves, other.singular_curves, "singular curves");
    add_counter(exceptional_j, other.exceptional_j, "exceptional j");
    add_counter(exact_order_11_failures, other.exact_order_11_failures,
                "exact-order failures");
    add_counter(points_without_weber_lifts,
                other.points_without_weber_lifts,
                "points without Weber lifts");
    add_counter(weber_lifts, other.weber_lifts, "Weber lifts");
    add_counter(nonsquare_explicit_montgomery_u,
                other.nonsquare_explicit_montgomery_u,
                "nonsquare Montgomery U");
    add_counter(points_without_explicit_montgomery_model,
                other.points_without_explicit_montgomery_model,
                "points without explicit Montgomery model");
    add_counter(full_two_torsion_failures,
                other.full_two_torsion_failures,
                "full 2-torsion failures");
    add_counter(point_four_rejections, other.point_four_rejections,
                "point-four rejections");
    add_counter(accepted, other.accepted, "accepted");
    return *this;
}

namespace {

std::uint64_t next_x1_attempt(std::uint64_t attempt) {
    if (attempt == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error(
            "exhausted deterministic X1(11) retry space");
    }
    return attempt + 1U;
}

X111ProbeResult deterministic_x1_11_impl(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    std::optional<std::uint64_t> maximum_x_samples,
    bool require_point_four) {
    require_probe_prime(prime);
    const Field field(prime);
    X111ProbeResult result;

    for (std::uint64_t attempt = 0;
         !maximum_x_samples.has_value() ||
             attempt < *maximum_x_samples;
         attempt = next_x1_attempt(attempt)) {
        increment(result.counters.x_samples, "x samples");
        const mpz_class x = deterministic_residue(
            field, seed, global_index,
            splitmix64(kX111SampleDomain ^ attempt));
        const std::vector<mpz_class> roots = linear_roots(
            Poly(field, {x, field.add(field.square(x), 1), 1}));
        if (roots.empty()) {
            increment(result.counters.x_polynomials_without_roots,
                      "rootless X1 polynomials");
            continue;
        }
        const std::size_t root_offset = static_cast<std::size_t>(
            splitmix64(kX111RootOrderDomain ^ seed ^ global_index ^ attempt) %
            roots.size());
        for (std::size_t root_index = 0; root_index < roots.size();
             ++root_index) {
            increment(result.counters.x1_points, "X1 points");
            const mpz_class y =
                roots[(root_offset + root_index) % roots.size()];
            const mpz_class r = field.add(field.mul(x, y), 1);
            const mpz_class s = field.sub(1, x);
            const mpz_class c = field.mul(s, field.sub(r, 1));
            const mpz_class b = field.mul(r, c);

            const mpz_class a = field.sub(c, 1);
            const mpz_class e = field.sub(field.square(a), field.mul(4, b));
            const mpz_class short_a = field.mul(
                27, field.sub(field.mul(24, field.mul(a, b)),
                              field.square(e)));
            const mpz_class short_b = field.mul(
                54,
                field.add(
                    field.sub(field.mul(field.square(e), e),
                              field.mul(36, field.mul(field.mul(a, b), e))),
                    field.mul(216, field.square(b))));
            Curve tate_curve(field, short_a, short_b);
            if (tate_curve.is_singular()) {
                increment(result.counters.singular_curves, "singular curves");
                continue;
            }
            const mpz_class j = tate_curve.j_invariant();
            if (j == 0 || j == field.normalize(1728)) {
                increment(result.counters.exceptional_j, "exceptional j");
                continue;
            }
            const AffinePoint distinguished{
                field.mul(3, e), field.neg(field.mul(108, b)), false};
            if (!has_exact_order_eleven(tate_curve, distinguished)) {
                increment(result.counters.exact_order_11_failures,
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
                const mpz_class u = field.sub(4, field.divide(z, 16));
                const std::vector<mpz_class> coefficients = linear_roots(
                    Poly(field, {field.neg(u), 0, 1}));
                if (coefficients.empty()) {
                    increment(result.counters.nonsquare_explicit_montgomery_u,
                              "nonsquare Montgomery U");
                    continue;
                }
                const mpz_class montgomery_a = coefficients.front();
                const MontgomeryCurve montgomery(field, montgomery_a);
                if (montgomery.is_singular() ||
                    montgomery.j_invariant() != j ||
                    field.square(montgomery_a) != u) {
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
                    "X1(11) Tate and Weber j-invariants disagree");
            }
            const X111CanonicalSide side =
                identify_canonical_side(tate_curve, pair.curve);
            const std::uint64_t cyclic_divisor = point_four ? 44U : 22U;
            const bool point_four_sixteen_divisor =
                point_four && mpz_fdiv_ui(prime.get_mpz_t(), 8U) == 5U;
            const std::uint64_t group_divisor =
                point_four_sixteen_divisor ? 176U
                                           : (point_four ? 88U : 44U);
            const mpz_class opposite_residue =
                (2 * (prime + 1)) %
                mpz_class(static_cast<unsigned long>(group_divisor));
            increment(result.counters.accepted, "accepted");
            result.sample.emplace(X111ProbeSample{
                global_index,
                x,
                y,
                r,
                s,
                b,
                c,
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

X111ProbeResult deterministic_x1_11_probe(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    X111ProbeOptions options) {
    if (options.max_x_samples == 0U) {
        throw std::invalid_argument(
            "X1(11) probe max_x_samples must be positive");
    }
    return deterministic_x1_11_impl(
        prime, seed, global_index, options.max_x_samples,
        options.require_point_four);
}

X111ProbeResult deterministic_x1_11_search_curve(
    const mpz_class& prime, std::uint64_t seed, std::uint64_t global_index,
    bool require_point_four) {
    X111ProbeResult result = deterministic_x1_11_impl(
        prime, seed, global_index, std::nullopt, require_point_four);
    if (!result.sample.has_value() || result.counters.x_samples == 0U) {
        throw std::logic_error(
            "unbounded X1(11) generator returned without an admitted sample");
    }
    result.sample->pair.rejected_samples = result.counters.x_samples - 1U;
    return result;
}

}  // namespace oneshotsea
