#include "oneshotsea/schoof.hpp"

#include "oneshotsea/isogeny.hpp"
#include "oneshotsea/poly.hpp"
#include "oneshotsea/torsion.hpp"
#include "oneshotsea/trace.hpp"

#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace oneshotsea {
namespace {

constexpr std::uint64_t kMaxReferenceSchoofEll = 37;
#if defined(ONESHOTSEA_QUOTIENT_CONTEXT_REUSE)
constexpr bool kQuotientContextReuse =
    ONESHOTSEA_QUOTIENT_CONTEXT_REUSE != 0;
#else
constexpr bool kQuotientContextReuse = false;
#endif

struct RawElement {
    Poly u;
    Poly v;
};

RawElement raw_add(const RawElement& lhs, const RawElement& rhs) {
    return {add(lhs.u, rhs.u), add(lhs.v, rhs.v)};
}

RawElement raw_neg(const RawElement& value) {
    return {neg(value.u), neg(value.v)};
}

RawElement raw_sub(const RawElement& lhs, const RawElement& rhs) {
    return raw_add(lhs, raw_neg(rhs));
}

RawElement raw_mul(const RawElement& lhs, const RawElement& rhs, const Poly& curve_rhs) {
    return {
        add(mul(lhs.u, rhs.u), mul(curve_rhs, mul(lhs.v, rhs.v))),
        add(mul(lhs.u, rhs.v), mul(lhs.v, rhs.u)),
    };
}

RawElement raw_square(const RawElement& value, const Poly& curve_rhs) {
    return raw_mul(value, value, curve_rhs);
}

Poly exact_divide(const Poly& numerator, const Poly& denominator) {
    auto [quotient, remainder] = divmod(numerator, denominator);
    if (!remainder.is_zero()) {
        throw std::runtime_error("division-polynomial recurrence was not exact");
    }
    return quotient;
}

bool is_small_prime(std::uint64_t value) {
    if (value < 2) {
        return false;
    }
    for (std::uint64_t divisor = 2; divisor <= value / divisor; ++divisor) {
        if (value % divisor == 0) {
            return value == divisor;
        }
    }
    return true;
}

Poly division_polynomial(std::uint64_t ell, const Curve& curve) {
    const Field& field = curve.field();
    const Poly zero(field);
    const Poly one = Poly::constant(field, 1);
    const Poly curve_rhs(field, {curve.b(), curve.a(), 0, 1});
    std::map<std::uint64_t, RawElement> cache;

    const auto psi = [&](auto&& self, std::uint64_t index) -> RawElement {
        const auto found = cache.find(index);
        if (found != cache.end()) {
            return found->second;
        }
        RawElement result{zero, zero};
        if (index == 0) {
            result = {zero, zero};
        } else if (index == 1) {
            result = {one, zero};
        } else if (index == 2) {
            result = {zero, Poly::constant(field, 2)};
        } else if (index == 3) {
            const mpz_class a = curve.a();
            const mpz_class b = curve.b();
            result = {Poly(field, {-a * a, 12 * b, 6 * a, 0, 3}), zero};
        } else if (index == 4) {
            const mpz_class a = curve.a();
            const mpz_class b = curve.b();
            const Poly inside(field, {
                -a * a * a - 8 * b * b,
                -4 * a * b,
                -5 * a * a,
                20 * b,
                5 * a,
                0,
                1,
            });
            result = {zero, scalar_mul(inside, 4)};
        } else if ((index & 1U) != 0U) {
            const std::uint64_t m = (index - 1U) / 2U;
            const RawElement psi_m = self(self, m);
            const RawElement psi_m1 = self(self, m + 1U);
            const RawElement first = raw_mul(
                self(self, m + 2U),
                raw_mul(raw_square(psi_m, curve_rhs), psi_m, curve_rhs), curve_rhs);
            const RawElement second = raw_mul(
                self(self, m - 1U),
                raw_mul(raw_square(psi_m1, curve_rhs), psi_m1, curve_rhs), curve_rhs);
            result = raw_sub(first, second);
        } else {
            const std::uint64_t m = index / 2U;
            const RawElement bracket = raw_sub(
                raw_mul(self(self, m + 2U), raw_square(self(self, m - 1U), curve_rhs),
                        curve_rhs),
                raw_mul(self(self, m - 2U), raw_square(self(self, m + 1U), curve_rhs),
                        curve_rhs));
            const RawElement numerator = raw_mul(self(self, m), bracket, curve_rhs);
            if (!numerator.v.is_zero()) {
                throw std::runtime_error("unexpected y term in even division polynomial");
            }
            const Poly quotient = exact_divide(numerator.u, curve_rhs);
            result = {zero, scalar_mul(quotient, field.inverse(2))};
        }
        cache.insert_or_assign(index, result);
        return result;
    };

    const RawElement raw_result = psi(psi, ell);
    if (!raw_result.v.is_zero() || raw_result.u.is_zero()) {
        throw std::runtime_error("invalid odd division polynomial");
    }
    return raw_result.u.monic();
}

struct QuotientRing {
    const Field* field;
    Poly modulus;
    Poly curve_rhs;
    std::optional<PolyModContext> arithmetic;

    QuotientRing(const Field& input_field, Poly input_modulus,
                 const Poly& input_curve_rhs)
        : field(&input_field),
          modulus(std::move(input_modulus)),
          curve_rhs(mod(input_curve_rhs, modulus)) {
        if constexpr (kQuotientContextReuse) {
            arithmetic.emplace(modulus);
        }
    }
};

struct Element {
    const QuotientRing* ring;
    Poly u;
    Poly v;
};

Element element(const QuotientRing& ring, const Poly& u, const Poly& v) {
    if constexpr (kQuotientContextReuse) {
        return {&ring, ring.arithmetic->reduce(u),
                ring.arithmetic->reduce(v)};
    }
    return {&ring, mod(u, ring.modulus), mod(v, ring.modulus)};
}

Element constant(const QuotientRing& ring, const mpz_class& value) {
    return element(ring, Poly::constant(*ring.field, value), Poly(*ring.field));
}

void require_same_ring(const Element& lhs, const Element& rhs) {
    if (lhs.ring != rhs.ring) {
        throw std::invalid_argument("quotient elements belong to different rings");
    }
}

Element element_add(const Element& lhs, const Element& rhs) {
    require_same_ring(lhs, rhs);
    return {lhs.ring, add(lhs.u, rhs.u), add(lhs.v, rhs.v)};
}

Element element_neg(const Element& value) {
    return {value.ring, neg(value.u), neg(value.v)};
}

Element element_sub(const Element& lhs, const Element& rhs) {
    return element_add(lhs, element_neg(rhs));
}

Poly quotient_multiply(const QuotientRing& ring, const Poly& lhs,
                       const Poly& rhs) {
    if constexpr (kQuotientContextReuse) {
        return ring.arithmetic->multiply(lhs, rhs);
    }
    return mulmod(lhs, rhs, ring.modulus);
}

Poly quotient_square(const QuotientRing& ring, const Poly& value) {
    if constexpr (kQuotientContextReuse) {
        return ring.arithmetic->square(value);
    }
    return squaremod(value, ring.modulus);
}

Poly quotient_pow(const QuotientRing& ring, Poly base,
                  const mpz_class& exponent) {
    if constexpr (kQuotientContextReuse) {
        return ring.arithmetic->pow(std::move(base), exponent);
    }
    return powmod(std::move(base), exponent, ring.modulus);
}

Element element_mul(const Element& lhs, const Element& rhs) {
    require_same_ring(lhs, rhs);
    const QuotientRing& ring = *lhs.ring;
    const Poly uu = quotient_multiply(ring, lhs.u, rhs.u);
    const Poly vv = quotient_multiply(ring, lhs.v, rhs.v);
    const Poly uv = quotient_multiply(ring, lhs.u, rhs.v);
    const Poly vu = quotient_multiply(ring, lhs.v, rhs.u);
    return {
        lhs.ring,
        add(uu, quotient_multiply(ring, lhs.ring->curve_rhs, vv)),
        add(uv, vu),
    };
}

Element element_square(const Element& value) {
    const QuotientRing& ring = *value.ring;
    const Poly uu = quotient_square(ring, value.u);
    const Poly vv = quotient_square(ring, value.v);
    const Poly uv = quotient_multiply(ring, value.u, value.v);
    return {
        value.ring,
        add(uu, quotient_multiply(ring, value.ring->curve_rhs, vv)),
        scalar_mul(uv, 2),
    };
}

Element element_scale(const Element& value, const mpz_class& scalar) {
    return {value.ring, scalar_mul(value.u, scalar),
            scalar_mul(value.v, scalar)};
}

Element element_pow(Element base, mpz_class exponent) {
    if (exponent < 0) {
        throw std::invalid_argument("negative quotient-ring exponent");
    }
    // Every production Frobenius call starts with x or f in the polynomial
    // subring (v=0), and this property is closed under exponentiation.  Route
    // that exact case through Poly::powmod so it inherits the exact-cost
    // unsigned window without building an Element table full of zero v parts.
    // Rewrap through element() to preserve canonical behavior for every
    // quotient, including the zero ring defined by a constant modulus.
    if (base.v.is_zero()) {
        return element(
            *base.ring,
            quotient_pow(*base.ring, std::move(base.u), exponent),
            Poly(*base.ring->field));
    }
    Element result = constant(*base.ring, 1);
    while (exponent > 0) {
        if (mpz_odd_p(exponent.get_mpz_t()) != 0) {
            result = element_mul(result, base);
        }
        exponent >>= 1;
        if (exponent > 0) {
            base = element_square(base);
        }
    }
    return result;
}

Element element_pow_binary_reference(Element base, mpz_class exponent) {
    if (exponent < 0) {
        throw std::invalid_argument("negative quotient-ring exponent");
    }
    Element result = constant(*base.ring, 1);
    while (exponent > 0) {
        if (mpz_odd_p(exponent.get_mpz_t()) != 0) {
            result = element_mul(result, base);
        }
        exponent >>= 1;
        if (exponent > 0) {
            base = element_square(base);
        }
    }
    return result;
}

bool element_zero(const Element& value) {
    return value.u.is_zero() && value.v.is_zero();
}

bool element_equal(const Element& lhs, const Element& rhs) {
    require_same_ring(lhs, rhs);
    return equal(lhs.u, rhs.u) && equal(lhs.v, rhs.v);
}

struct JacobianPoint {
    Element x;
    Element y;
    Element z;
};

JacobianPoint infinity(const QuotientRing& ring) {
    return {constant(ring, 0), constant(ring, 1), constant(ring, 0)};
}

bool point_infinity(const JacobianPoint& point) {
    return element_zero(point.z);
}

JacobianPoint point_double(const JacobianPoint& point, const mpz_class& curve_a) {
    if (point_infinity(point) || element_zero(point.y)) {
        return infinity(*point.x.ring);
    }
    const Element xx = element_square(point.x);
    const Element yy = element_square(point.y);
    const Element yyyy = element_square(yy);
    const Element zz = element_square(point.z);
    const Element s = element_scale(
        element_sub(element_sub(element_square(element_add(point.x, yy)), xx), yyyy), 2);
    const Element m = element_add(element_scale(xx, 3),
                                  element_scale(element_square(zz), curve_a));
    const Element t = element_sub(element_square(m), element_scale(s, 2));
    return {
        t,
        element_sub(element_mul(m, element_sub(s, t)), element_scale(yyyy, 8)),
        element_sub(element_sub(element_square(element_add(point.y, point.z)), yy), zz),
    };
}

JacobianPoint point_add(const JacobianPoint& lhs, const JacobianPoint& rhs,
                        const mpz_class& curve_a) {
    if (point_infinity(lhs)) {
        return rhs;
    }
    if (point_infinity(rhs)) {
        return lhs;
    }
    const Element z1z1 = element_square(lhs.z);
    const Element z2z2 = element_square(rhs.z);
    const Element u1 = element_mul(lhs.x, z2z2);
    const Element u2 = element_mul(rhs.x, z1z1);
    const Element s1 = element_mul(element_mul(lhs.y, rhs.z), z2z2);
    const Element s2 = element_mul(element_mul(rhs.y, lhs.z), z1z1);
    if (element_equal(u1, u2)) {
        if (element_equal(s1, s2)) {
            return point_double(lhs, curve_a);
        }
        if (element_equal(s1, element_neg(s2))) {
            return infinity(*lhs.x.ring);
        }
    }
    const Element h = element_sub(u2, u1);
    const Element i = element_square(element_scale(h, 2));
    const Element j = element_mul(h, i);
    const Element r = element_scale(element_sub(s2, s1), 2);
    const Element v = element_mul(u1, i);
    const Element x3 = element_sub(element_sub(element_square(r), j), element_scale(v, 2));
    return {
        x3,
        element_sub(element_mul(r, element_sub(v, x3)),
                    element_scale(element_mul(s1, j), 2)),
        element_mul(
            element_sub(element_sub(element_square(element_add(lhs.z, rhs.z)), z1z1), z2z2),
            h),
    };
}

JacobianPoint scalar_multiply(std::uint64_t scalar, const JacobianPoint& point,
                              const mpz_class& curve_a) {
    JacobianPoint result = infinity(*point.x.ring);
    JacobianPoint addend = point;
    while (scalar != 0) {
        if ((scalar & 1U) != 0U) {
            result = point_add(result, addend, curve_a);
        }
        scalar >>= 1U;
        if (scalar != 0) {
            addend = point_double(addend, curve_a);
        }
    }
    return result;
}

JacobianPoint point_neg(const JacobianPoint& point) {
    return {point.x, element_neg(point.y), point.z};
}

bool same_point(const JacobianPoint& lhs, const JacobianPoint& rhs) {
    if (point_infinity(lhs) || point_infinity(rhs)) {
        return point_infinity(lhs) && point_infinity(rhs);
    }
    const Element left_z2 = element_square(lhs.z);
    const Element right_z2 = element_square(rhs.z);
    return element_equal(element_mul(lhs.x, right_z2), element_mul(rhs.x, left_z2)) &&
           element_equal(element_mul(element_mul(lhs.y, right_z2), rhs.z),
                         element_mul(element_mul(rhs.y, left_z2), lhs.z));
}

bool same_x(const JacobianPoint& lhs, const JacobianPoint& rhs) {
    if (point_infinity(lhs) || point_infinity(rhs)) {
        return point_infinity(lhs) && point_infinity(rhs);
    }
    const Element left_z2 = element_square(lhs.z);
    const Element right_z2 = element_square(rhs.z);
    return element_equal(
        element_mul(lhs.x, right_z2), element_mul(rhs.x, left_z2));
}

bool projective_x_satisfies(const Poly& polynomial, const JacobianPoint& point) {
    if (point_infinity(point)) {
        return false;
    }
    const Element z2 = element_square(point.z);
    Element z_power = z2;
    Element value = constant(*point.x.ring, polynomial.leading_coefficient());
    for (int index = polynomial.degree() - 1; index >= 0; --index) {
        value = element_add(
            element_mul(value, point.x),
            element_scale(z_power,
                          polynomial.coefficient(static_cast<std::size_t>(index))));
        if (index != 0) {
            z_power = element_mul(z_power, z2);
        }
    }
    return element_zero(value);
}

Poly invert_mod(const Poly& value, const Poly& modulus) {
    Poly old_remainder = modulus;
    Poly remainder = mod(value, modulus);
    Poly old_cofactor = Poly::constant(value.field(), 0);
    Poly cofactor = Poly::constant(value.field(), 1);
    while (!remainder.is_zero()) {
        auto [quotient, next_remainder] =
            divmod(old_remainder, remainder);
        Poly next_cofactor = sub(
            old_cofactor, mulmod(quotient, cofactor, modulus));
        old_remainder = std::move(remainder);
        remainder = std::move(next_remainder);
        old_cofactor = std::move(cofactor);
        cofactor = std::move(next_cofactor);
    }
    if (old_remainder.degree() != 0) {
        throw std::domain_error("quotient-ring element is not invertible");
    }
    return mod(
        scalar_mul(old_cofactor,
                   value.field().inverse(old_remainder.coefficient(0))),
        modulus);
}

struct AffineXKey {
    std::vector<mpz_class> x;

    bool operator<(const AffineXKey& other) const {
        return x < other.x;
    }
};

// Convert a batch of nonzero projective x-coordinates to canonical affine
// keys with one polynomial inversion (Montgomery's batch-inversion trick).
// Generic odd-torsion X and Z^2 coordinates lie in F_p[x]/(h), even though Z
// itself and the full points can have y terms in the quadratic curve algebra.
std::vector<AffineXKey> affine_x_keys(
    const std::vector<JacobianPoint>& points) {
    if (points.empty()) {
        return {};
    }
    const QuotientRing& ring = *points.front().x.ring;
    const Field& field = *ring.field;
    std::vector<Poly> z_squared;
    z_squared.reserve(points.size());
    std::vector<Poly> prefix_products;
    prefix_products.reserve(points.size() + 1U);
    prefix_products.push_back(Poly::constant(field, 1));
    for (const JacobianPoint& point : points) {
        if (point_infinity(point)) {
            throw std::domain_error("point batch contains infinity");
        }
        if (!point.x.v.is_zero()) {
            throw std::domain_error("point batch x left base quotient ring");
        }
        if (point.x.ring != &ring) {
            throw std::domain_error(
                "point batch mixes quotient rings");
        }
        const Element denominator = element_square(point.z);
        if (!denominator.v.is_zero()) {
            throw std::domain_error(
                "point batch Z^2 left base quotient ring");
        }
        z_squared.push_back(denominator.u);
        prefix_products.push_back(mod(
            mul(prefix_products.back(), z_squared.back()), ring.modulus));
    }

    std::vector<Poly> inverse_z_squared(points.size(), Poly(field));
    Poly inverse_product = invert_mod(prefix_products.back(), ring.modulus);
    for (std::size_t index = points.size(); index > 0U; --index) {
        inverse_z_squared[index - 1U] = mod(
            mul(inverse_product, prefix_products[index - 1U]), ring.modulus);
        inverse_product = mod(
            mul(inverse_product, z_squared[index - 1U]), ring.modulus);
    }

    std::vector<AffineXKey> keys;
    keys.reserve(points.size());
    for (std::size_t index = 0; index < points.size(); ++index) {
        const Poly affine_x =
            mulmod(points[index].x.u, inverse_z_squared[index], ring.modulus);
        keys.push_back({affine_x.coefficients()});
    }
    return keys;
}

std::optional<std::uint64_t> try_eigenvalue_x_linear(
    const Curve& curve, const JacobianPoint& generic,
    const JacobianPoint& frobenius, std::uint64_t ell) {
    JacobianPoint multiple = generic;
    for (std::uint64_t eigenvalue = 1; eigenvalue <= (ell - 1U) / 2U;
         ++eigenvalue) {
        if (same_x(frobenius, multiple)) {
            if (same_point(frobenius, multiple)) {
                return eigenvalue;
            }
            if (same_point(frobenius, point_neg(multiple))) {
                return ell - eigenvalue;
            }
        }
        if (eigenvalue < (ell - 1U) / 2U) {
            multiple = point_add(multiple, generic, curve.a());
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> try_eigenvalue_mitm(
    const Curve& curve, const JacobianPoint& generic,
    const JacobianPoint& frobenius, std::uint64_t ell) {
    std::uint64_t width = 1U;
    while (width < ell / width + (ell % width == 0U ? 0U : 1U)) {
        ++width;
    }

    std::vector<JacobianPoint> babies;
    std::vector<std::uint64_t> baby_scalars;
    babies.reserve(static_cast<std::size_t>(width - 1U));
    baby_scalars.reserve(static_cast<std::size_t>(width - 1U));
    JacobianPoint multiple = generic;
    for (std::uint64_t scalar = 1U; scalar < width; ++scalar) {
        babies.push_back(multiple);
        baby_scalars.push_back(scalar);
        if (scalar + 1U < width) {
            multiple = point_add(multiple, generic, curve.a());
        }
    }

    const JacobianPoint giant_step =
        scalar_multiply(width, generic, curve.a());
    std::vector<JacobianPoint> residuals;
    std::vector<std::uint64_t> giant_scalars;
    residuals.reserve(static_cast<std::size_t>((ell - 1U) / width + 1U));
    giant_scalars.reserve(residuals.capacity());
    JacobianPoint giant = infinity(*generic.x.ring);
    for (std::uint64_t scalar = 0U; scalar <= (ell - 1U) / width;
         ++scalar) {
        const JacobianPoint residual = point_add(
            frobenius, point_neg(giant), curve.a());
        if (point_infinity(residual)) {
            const std::uint64_t eigenvalue = scalar * width;
            if (eigenvalue > 0U && eigenvalue < ell) {
                return eigenvalue;
            }
        } else {
            residuals.push_back(residual);
            giant_scalars.push_back(scalar);
        }
        if (scalar < (ell - 1U) / width) {
            giant = point_add(giant, giant_step, curve.a());
        }
    }

    std::vector<JacobianPoint> normalized_points = babies;
    normalized_points.insert(
        normalized_points.end(), residuals.begin(), residuals.end());
    const std::vector<AffineXKey> keys = affine_x_keys(normalized_points);
    std::map<AffineXKey, std::uint64_t> baby_table;
    for (std::size_t index = 0; index < babies.size(); ++index) {
        baby_table.emplace(keys[index], baby_scalars[index]);
    }
    for (std::size_t index = 0; index < residuals.size(); ++index) {
        const auto found = baby_table.find(keys[babies.size() + index]);
        if (found == baby_table.end()) {
            continue;
        }
        const std::uint64_t base = giant_scalars[index] * width;
        const std::uint64_t baby = found->second;
        const std::uint64_t plus =
            base >= ell - baby ? base - (ell - baby) : base + baby;
        const std::uint64_t minus =
            base >= baby ? base - baby : ell - (baby - base);
        for (const std::uint64_t eigenvalue : {plus, minus}) {
            if (eigenvalue > 0U && eigenvalue < ell &&
                same_point(
                    frobenius,
                    scalar_multiply(eigenvalue, generic, curve.a()))) {
                return eigenvalue;
            }
        }
    }
    return std::nullopt;
}

}  // namespace

bool quotient_element_pow_paths_agree_for_testing(
    const Poly& modulus, const Poly& curve_rhs, const Poly& u,
    const Poly& v, const mpz_class& exponent) {
    const Field& field = modulus.field();
    const QuotientRing ring(field, modulus, curve_rhs);
    const Element base = element(ring, u, v);
    const Element selected = element_pow(base, exponent);
    const Element binary = element_pow_binary_reference(base, exponent);
    return element_equal(selected, binary);
}

Poly division_polynomial_reference(const Curve& curve, std::uint64_t ell) {
    if (curve.is_singular()) {
        throw std::invalid_argument("division polynomial requires a nonsingular curve");
    }
    if (ell > kMaxReferenceSchoofEll) {
        throw std::invalid_argument("ell exceeds the reference torsion limit of 37");
    }
    if (!is_small_prime(ell) || ell == 2) {
        throw std::invalid_argument("ell must be an odd prime");
    }
    if (mpz_cmp_ui(curve.field().modulus().get_mpz_t(), ell) == 0) {
        throw std::invalid_argument("ell must differ from p");
    }
    if (mpz_probab_prime_p(curve.field().modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("division polynomial requires probable-prime p");
    }
    return division_polynomial(ell, curve);
}

namespace {

std::optional<std::uint64_t> try_frobenius_eigenvalue_impl(
    const Curve& curve, const Poly& kernel, std::uint64_t ell,
    bool validate_division_polynomial, bool validate_subgroup_closure,
    std::optional<FrobeniusEigenvalueTestPath> forced_path = std::nullopt) {
    if (curve.is_singular()) {
        throw std::invalid_argument("Frobenius eigenvalue requires a nonsingular curve");
    }
    if (!is_small_prime(ell) || ell == 2) {
        throw std::invalid_argument("ell must be an odd prime");
    }
    if (mpz_cmp_ui(curve.field().modulus().get_mpz_t(), ell) == 0) {
        throw std::invalid_argument("ell must differ from p");
    }
    if (mpz_probab_prime_p(curve.field().modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("Frobenius eigenvalue requires probable-prime p");
    }
    if (kernel.field().modulus() != curve.field().modulus()) {
        throw std::invalid_argument("kernel and curve use different fields");
    }
    if (kernel.degree() != static_cast<int>((ell - 1U) / 2U) ||
        kernel.leading_coefficient() != 1) {
        throw std::invalid_argument("kernel has the wrong degree or normalization");
    }
    if (gcd(kernel, kernel.derivative()).degree() != 0) {
        throw std::invalid_argument("kernel is not square-free");
    }
    if (validate_division_polynomial) {
        const Poly psi_ell = division_polynomial_reference(curve, ell);
        if (!divmod(psi_ell, kernel).second.is_zero()) {
            throw std::invalid_argument("kernel does not divide psi_ell");
        }
    }

    const Field& field = curve.field();
    const Poly curve_rhs(field, {curve.b(), curve.a(), 0, 1});
    const QuotientRing ring(field, kernel, curve_rhs);
    const Element x = element(ring, Poly::x(field), Poly(field));
    const Element y = element(ring, Poly(field), Poly::constant(field, 1));
    const JacobianPoint generic{x, y, constant(ring, 1)};
    const Element f = element(ring, curve_rhs, Poly(field));
    const mpz_class p = field.modulus();

    const JacobianPoint frobenius{
        element_pow(x, p),
        element_mul(y, element_pow(f, (p - 1) / 2)),
        constant(ring, 1),
    };
    if (validate_subgroup_closure) {
        JacobianPoint multiple = generic;
        // Degree, square-freeness, or even divisibility by psi_ell does not by
        // itself prove that the roots form one cyclic subgroup. In scalar-
        // Frobenius cases a mixture of roots from different lines could
        // otherwise masquerade as an eigenkernel. Closure under every
        // nonzero scalar modulo sign proves that the roots form one subgroup.
        // A BMSS denominator that has passed the full rational-isogeny identity
        // already has this property, so its production caller skips this
        // deliberately expensive independent check.
        for (std::uint64_t scalar = 2U; scalar <= (ell - 1U) / 2U;
             ++scalar) {
            multiple = point_add(multiple, generic, curve.a());
            if (!projective_x_satisfies(kernel, multiple)) {
                return std::nullopt;
            }
        }
    }
    if (forced_path.has_value()) {
        switch (*forced_path) {
            case FrobeniusEigenvalueTestPath::linear:
                return try_eigenvalue_x_linear(
                    curve, generic, frobenius, ell);
            case FrobeniusEigenvalueTestPath::meet_in_the_middle:
                return try_eigenvalue_mitm(curve, generic, frobenius, ell);
        }
        throw std::invalid_argument("invalid Frobenius eigenvalue test path");
    }

    try {
        if (ell <= 401U) {
            return try_eigenvalue_x_linear(curve, generic, frobenius, ell);
        }
        return try_eigenvalue_mitm(curve, generic, frobenius, ell);
    } catch (const std::domain_error&) {
        // A malformed or non-eigen kernel can make a projective Z coordinate
        // a zero divisor.  Preserve the complete reference behavior in this
        // exceptional case without weakening the exact final comparison.
        return try_eigenvalue_x_linear(curve, generic, frobenius, ell);
    }
}

}  // namespace

std::optional<std::uint64_t> try_frobenius_eigenvalue_reference(
    const Curve& curve, const Poly& kernel, std::uint64_t ell) {
    return try_frobenius_eigenvalue_impl(curve, kernel, ell, true, true);
}

std::optional<std::uint64_t>
try_frobenius_eigenvalue_reference_for_testing(
    const Curve& curve, const Poly& kernel, std::uint64_t ell,
    FrobeniusEigenvalueTestPath path) {
    return try_frobenius_eigenvalue_impl(
        curve, kernel, ell, true, true, path);
}

std::optional<std::uint64_t> try_frobenius_eigenvalue_from_isogeny(
    const Curve& curve, const Curve& normalized_codomain,
    const BmssIsogenyResult& isogeny, std::uint64_t ell) {
    validate_rational_isogeny_reference(
        curve, normalized_codomain, ell, isogeny);
    return try_frobenius_eigenvalue_impl(
        curve, isogeny.kernel, ell, false, false);
}

std::optional<std::uint64_t>
try_frobenius_eigenvalue_from_isogeny_for_testing(
    const Curve& curve, const Curve& normalized_codomain,
    const BmssIsogenyResult& isogeny, std::uint64_t ell,
    FrobeniusEigenvalueTestPath path) {
    validate_rational_isogeny_reference(
        curve, normalized_codomain, ell, isogeny);
    return try_frobenius_eigenvalue_impl(
        curve, isogeny.kernel, ell, false, false, path);
}

std::uint64_t frobenius_eigenvalue_reference(const Curve& curve,
                                             const Poly& kernel,
                                             std::uint64_t ell) {
    const auto eigenvalue =
        try_frobenius_eigenvalue_reference(curve, kernel, ell);
    if (!eigenvalue.has_value()) {
        throw std::invalid_argument(
            "kernel is not one closed Frobenius eigenline");
    }
    return *eigenvalue;
}

std::uint64_t schoof_trace_mod_ell(const Curve& curve, std::uint64_t ell) {
    if (curve.is_singular()) {
        throw std::invalid_argument("Schoof residue requires a nonsingular curve");
    }
    if (ell > kMaxReferenceSchoofEll) {
        throw std::invalid_argument("ell exceeds the reference Schoof limit of 37");
    }
    if (!is_small_prime(ell) || ell == 2) {
        throw std::invalid_argument("ell must be an odd prime");
    }
    if (mpz_cmp_ui(curve.field().modulus().get_mpz_t(), ell) == 0) {
        throw std::invalid_argument("ell must differ from p");
    }
    if (mpz_probab_prime_p(curve.field().modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("Schoof residue requires probable-prime p");
    }

    const Field& field = curve.field();
    const Poly psi_ell = division_polynomial_reference(curve, ell);
    const Poly curve_rhs(field, {curve.b(), curve.a(), 0, 1});
    const QuotientRing ring(field, psi_ell, curve_rhs);
    const Element x = element(ring, Poly::x(field), Poly(field));
    const Element y = element(ring, Poly(field), Poly::constant(field, 1));
    const Element one = constant(ring, 1);
    const JacobianPoint generic{x, y, one};
    const Element f = element(ring, curve_rhs, Poly(field));
    const mpz_class p = field.modulus();
    const JacobianPoint frobenius{
        element_pow(x, p),
        element_mul(y, element_pow(f, (p - 1) / 2)),
        one,
    };
    const mpz_class p_squared = p * p;
    const JacobianPoint frobenius_squared{
        element_pow(x, p_squared),
        element_mul(y, element_pow(f, (p_squared - 1) / 2)),
        one,
    };
    const std::uint64_t p_mod_ell = mpz_fdiv_ui(p.get_mpz_t(), ell);
    const JacobianPoint lhs = point_add(
        frobenius_squared, scalar_multiply(p_mod_ell, generic, curve.a()), curve.a());
    std::vector<std::uint64_t> matches;
    for (std::uint64_t residue = 0; residue < ell; ++residue) {
        if (same_point(lhs, scalar_multiply(residue, frobenius, curve.a()))) {
            matches.push_back(residue);
        }
    }
    if (matches.size() != 1U) {
        throw std::runtime_error("Schoof characteristic equation did not have a unique residue");
    }
    return matches.front();
}

SchoofCountResult schoof_count_reference(const Curve& curve, std::uint64_t max_ell) {
    if (max_ell < 3) {
        throw std::invalid_argument("max_ell must be at least 3");
    }
    if (max_ell > kMaxReferenceSchoofEll) {
        throw std::invalid_argument("max_ell exceeds the reference Schoof limit of 37");
    }
    TraceConstraints constraints(curve.field().modulus());
    std::vector<std::uint64_t> levels;
    for (std::uint64_t ell = 3; ell <= max_ell; ell += 2U) {
        if (!is_small_prime(ell) || mpz_cmp_ui(curve.field().modulus().get_mpz_t(), ell) == 0) {
            continue;
        }
        const std::uint64_t residue = schoof_trace_mod_ell(curve, ell);
        constraints.refine(ell, {residue});
        levels.push_back(ell);
        const auto traces = constraints.enumerate(1);
        if (traces.has_value() && traces->size() == 1U) {
            const mpz_class trace = traces->front();
            return {
                trace,
                curve.field().modulus() + 1 - trace,
                constraints.modulus(),
                std::move(levels),
            };
        }
    }
    throw std::runtime_error("max_ell did not determine a unique Hasse trace");
}

}  // namespace oneshotsea
