#include "oneshotsea/prime_isogeny.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace oneshotsea {
namespace {

void validate_level(unsigned level, const mpz_class& characteristic) {
    const mpz_class encoded_level(static_cast<unsigned long>(level));
    if (level < 3U || (level & 1U) == 0U ||
        !is_prime_u64(level)) {
        throw std::invalid_argument("isogeny level must be an odd prime");
    }
    if (encoded_level == characteristic) {
        throw std::invalid_argument(
            "isogeny level must differ from the characteristic");
    }
}

bool canonical_point(const Curve& curve, const AffinePoint& point) {
    if (point.infinity) {
        return point.x == 0 && point.y == 0;
    }
    return point.x >= 0 && point.x < curve.field().modulus() &&
           point.y >= 0 && point.y < curve.field().modulus();
}

void require_point(const Curve& curve, const AffinePoint& point) {
    if (!canonical_point(curve, point) ||
        !affine_point_is_on_curve(curve, point)) {
        throw std::invalid_argument(
            "affine point is noncanonical or not on the curve");
    }
}

void require_exact_prime_order_generator(const Curve& curve,
                                         const AffinePoint& generator,
                                         unsigned level) {
    validate_level(level, curve.field().modulus());
    require_point(curve, generator);
    if (generator.infinity ||
        !affine_scalar_multiply(
             curve, mpz_class(static_cast<unsigned long>(level)), generator)
             .infinity) {
        throw std::invalid_argument(
            "kernel generator does not have exact prime order");
    }
}

std::optional<mpz_class> square_root_mod_prime(const Field& field,
                                               const mpz_class& value) {
    const mpz_class a = field.normalize(value);
    if (a == 0) {
        return mpz_class(0);
    }
    if (field.legendre(a) != 1) {
        return std::nullopt;
    }
    const mpz_class& prime = field.modulus();
    if (mpz_fdiv_ui(prime.get_mpz_t(), 4U) == 3U) {
        return field.pow(a, (prime + 1) / 4);
    }

    mpz_class odd_part = prime - 1;
    unsigned two_adic_valuation = 0U;
    while (mpz_even_p(odd_part.get_mpz_t()) != 0) {
        odd_part /= 2;
        ++two_adic_valuation;
    }
    mpz_class nonresidue = 2;
    while (field.legendre(nonresidue) != -1) {
        ++nonresidue;
    }
    mpz_class c = field.pow(nonresidue, odd_part);
    mpz_class root = field.pow(a, (odd_part + 1) / 2);
    mpz_class t = field.pow(a, odd_part);
    unsigned remaining = two_adic_valuation;
    while (t != 1) {
        mpz_class power = t;
        unsigned index = 0U;
        for (index = 1U; index < remaining; ++index) {
            power = field.square(power);
            if (power == 1) {
                break;
            }
        }
        if (index == remaining) {
            throw std::logic_error(
                "Tonelli-Shanks contradicted the Legendre symbol");
        }
        mpz_class exponent = 1;
        mpz_mul_2exp(exponent.get_mpz_t(), exponent.get_mpz_t(),
                     remaining - index - 1U);
        const mpz_class correction = field.pow(c, exponent);
        root = field.mul(root, correction);
        c = field.square(correction);
        t = field.mul(t, c);
        remaining = index;
    }
    if (field.square(root) != a) {
        throw std::logic_error("finite-field square-root validation failed");
    }
    return root;
}

bool same_kernel(const Poly& lhs, const Poly& rhs) {
    return equal(lhs, rhs);
}

Curve velu_codomain_from_half_power_sums(
    const Curve& curve, std::size_t degree,
    const mpz_class& power_sum_1, const mpz_class& power_sum_2,
    const mpz_class& power_sum_3) {
    const Field& field = curve.field();
    const mpz_class twice_degree(
        static_cast<unsigned long>(2U * degree));
    const mpz_class four_times_degree(
        static_cast<unsigned long>(4U * degree));
    const mpz_class t = field.add(
        field.mul(6, power_sum_2),
        field.mul(twice_degree, curve.a()));
    const mpz_class w = field.add(
        field.add(field.mul(10, power_sum_3),
                  field.mul(6, field.mul(curve.a(), power_sum_1))),
        field.mul(four_times_degree, curve.b()));
    Curve codomain(field, field.sub(curve.a(), field.mul(5, t)),
                   field.sub(curve.b(), field.mul(7, w)));
    if (codomain.is_singular()) {
        throw std::logic_error("Velu quotient produced a singular codomain");
    }
    return codomain;
}

Curve velu_codomain_from_validated_kernel(const Curve& curve,
                                           const Poly& kernel,
                                           unsigned level) {
    // cyclic_kernel_polynomial has already proved that kernel is the monic
    // product of the distinct x-coordinates of a rational order-ell
    // subgroup.  Newton's identities therefore recover the three power sums
    // needed by Velu without walking that subgroup a second time.  The
    // independently checked public reference additionally divides psi_ell;
    // doing that here would make every auxiliary volcano edge quadratic in
    // the division-polynomial degree.
    const std::size_t degree = static_cast<std::size_t>((level - 1U) / 2U);
    if (kernel.field().modulus() != curve.field().modulus() ||
        kernel.degree() != static_cast<int>(degree) ||
        kernel.leading_coefficient() != 1) {
        throw std::logic_error(
            "validated cyclic kernel has inconsistent normalization");
    }
    const Field& field = curve.field();
    const mpz_class e1 = field.neg(kernel.coefficient(degree - 1U));
    const mpz_class e2 = degree >= 2U
        ? kernel.coefficient(degree - 2U) : mpz_class(0);
    const mpz_class e3 = degree >= 3U
        ? field.neg(kernel.coefficient(degree - 3U)) : mpz_class(0);
    const mpz_class power_sum_1 = e1;
    const mpz_class power_sum_2 =
        field.sub(field.square(e1), field.mul(2, e2));
    const mpz_class power_sum_3 = field.add(
        field.sub(field.mul(e1, power_sum_2),
                  field.mul(e2, power_sum_1)),
        field.mul(3, e3));
    return velu_codomain_from_half_power_sums(
        curve, degree, power_sum_1, power_sum_2, power_sum_3);
}

AffinePoint project_to_order_level(const Curve& curve,
                                   const AffinePoint& point,
                                   const mpz_class& prime_to_level_cofactor,
                                   unsigned level,
                                   unsigned level_valuation) {
    AffinePoint current = affine_scalar_multiply(
        curve, prime_to_level_cofactor, point);
    if (current.infinity) {
        return {};
    }
    const mpz_class encoded_level(static_cast<unsigned long>(level));
    for (unsigned exponent = 0U; exponent < level_valuation; ++exponent) {
        const AffinePoint next =
            affine_scalar_multiply(curve, encoded_level, current);
        if (next.infinity) {
            return current;
        }
        current = next;
    }
    throw std::invalid_argument(
        "supplied group order does not annihilate a sampled point");
}

bool same_cyclic_subgroup(const Curve& curve, const AffinePoint& lhs,
                          const AffinePoint& rhs, unsigned level) {
    // Exact odd-prime-order subgroups agree iff one generator is +/- a
    // nonzero multiple of the other.  Comparing abscissas checks both signs
    // and avoids constructing a degree-(ell-1)/2 kernel merely to find a
    // second basis point.
    AffinePoint multiple = lhs;
    for (unsigned scalar = 1U; scalar <= (level - 1U) / 2U; ++scalar) {
        if (multiple.infinity) {
            throw std::logic_error(
                "order-ell subgroup ended during basis comparison");
        }
        if (multiple.x == rhs.x) {
            return true;
        }
        if (scalar < (level - 1U) / 2U) {
            multiple = affine_point_add(curve, multiple, lhs);
        }
    }
    return false;
}

struct RationalLevelBasis {
    AffinePoint first;
    AffinePoint second;
    std::uint64_t x_candidates_tested;
    unsigned group_order_level_valuation;
};

bool is_prime_u64_thread_cached(std::uint64_t value) {
    thread_local std::uint64_t cached_value = 0U;
    thread_local bool cached_result = false;
    if (cached_value != value) {
        cached_result = is_prime_u64(value);
        cached_value = value;
    }
    return cached_result;
}

class AuxiliaryField64 {
public:
    explicit AuxiliaryField64(std::uint64_t modulus) : modulus_(modulus) {
        if (modulus_ <= 2U || (modulus_ & 1U) == 0U ||
            !is_prime_u64_thread_cached(modulus_)) {
            throw std::invalid_argument(
                "fast auxiliary field requires a proven odd 64-bit prime");
        }
        std::uint64_t inverse = 1U;
        for (unsigned round = 0U; round < 6U; ++round) {
            inverse *= 2U - modulus_ * inverse;
        }
        negative_inverse_ = 0U - inverse;
        if (modulus_ * inverse != 1U) {
            throw std::logic_error(
                "failed to derive the Montgomery modulus inverse");
        }
        const unsigned __int128 radix =
            static_cast<unsigned __int128>(1U) << 64U;
        one_ = static_cast<std::uint64_t>(radix % modulus_);
        const std::uint64_t radix_squared = static_cast<std::uint64_t>(
            (static_cast<unsigned __int128>(one_) * one_) % modulus_);
        radix_squared_ = radix_squared;
        if (decode(one_) != 1U || encode(1U) != one_) {
            throw std::logic_error(
                "Montgomery auxiliary-field initialization failed");
        }
    }

    std::uint64_t modulus() const { return modulus_; }
    std::uint64_t one() const { return one_; }

    std::uint64_t encode(std::uint64_t canonical) const {
        if (canonical >= modulus_) {
            throw std::logic_error(
                "cannot Montgomery-encode a noncanonical residue");
        }
        return reduce(
            static_cast<unsigned __int128>(canonical) * radix_squared_);
    }

    std::uint64_t decode(std::uint64_t encoded) const {
        require_canonical(encoded);
        return reduce(encoded);
    }

    std::uint64_t constant(std::uint64_t value) const {
        return encode(value % modulus_);
    }

    std::uint64_t add(std::uint64_t lhs, std::uint64_t rhs) const {
        require_canonical(lhs);
        require_canonical(rhs);
        std::uint64_t sum = lhs + rhs;
        if (sum < lhs || sum >= modulus_) {
            sum -= modulus_;
        }
        return sum;
    }

    std::uint64_t sub(std::uint64_t lhs, std::uint64_t rhs) const {
        require_canonical(lhs);
        require_canonical(rhs);
        return lhs >= rhs ? lhs - rhs : modulus_ - (rhs - lhs);
    }

    std::uint64_t mul(std::uint64_t lhs, std::uint64_t rhs) const {
        require_canonical(lhs);
        require_canonical(rhs);
        return reduce(static_cast<unsigned __int128>(lhs) * rhs);
    }

    std::uint64_t square(std::uint64_t value) const {
        return mul(value, value);
    }

    std::uint64_t inverse(std::uint64_t value) const {
        require_canonical(value);
        if (value == 0U) {
            throw std::domain_error(
                "cannot invert zero in the fast auxiliary field");
        }
        const std::uint64_t candidate = power(value, modulus_ - 2U);
        if (mul(value, candidate) != one_) {
            throw std::logic_error(
                "fast auxiliary-field inversion failed validation");
        }
        return candidate;
    }

private:
    void require_canonical(std::uint64_t value) const {
        if (value >= modulus_) {
            throw std::logic_error(
                "fast auxiliary-field operand is noncanonical");
        }
    }

    std::uint64_t power(std::uint64_t base,
                        std::uint64_t exponent) const {
        std::uint64_t result = one_;
        while (exponent != 0U) {
            if ((exponent & 1U) != 0U) {
                result = mul(result, base);
            }
            exponent >>= 1U;
            if (exponent != 0U) {
                base = square(base);
            }
        }
        return result;
    }

    std::uint64_t reduce(unsigned __int128 product) const {
        const std::uint64_t low = static_cast<std::uint64_t>(product);
        const std::uint64_t multiplier = low * negative_inverse_;
        const unsigned __int128 correction =
            static_cast<unsigned __int128>(multiplier) * modulus_;
        unsigned __int128 quotient =
            (product >> 64U) + (correction >> 64U);
        if (low != 0U) {
            ++quotient;
        }
        if (quotient >= modulus_) {
            quotient -= modulus_;
        }
        const std::uint64_t reduced =
            static_cast<std::uint64_t>(quotient);
        if (reduced >= modulus_) {
            throw std::logic_error(
                "Montgomery reduction produced a noncanonical residue");
        }
        return reduced;
    }

    std::uint64_t modulus_;
    std::uint64_t negative_inverse_ = 0U;
    std::uint64_t one_ = 0U;
    std::uint64_t radix_squared_ = 0U;
};

struct AuxiliaryPoint64 {
    std::uint64_t x = 0U;
    std::uint64_t y = 0U;
    bool infinity = true;
};

struct AuxiliaryCurve64 {
    AuxiliaryField64 field;
    std::uint64_t a;
    std::uint64_t b;
};

mpz_class import_u64(std::uint64_t value) {
    mpz_class output;
    mpz_import(output.get_mpz_t(), 1U, -1, sizeof(value), 0, 0, &value);
    return output;
}

AuxiliaryPoint64 encode_auxiliary_point(
    const AffinePoint& point, const AuxiliaryField64& field) {
    if (point.infinity) {
        if (point.x != 0 || point.y != 0) {
            throw std::logic_error(
                "cannot encode a noncanonical point at infinity");
        }
        return {};
    }
    AuxiliaryPoint64 encoded;
    std::uint64_t canonical_x = 0U;
    std::uint64_t canonical_y = 0U;
    if (!export_u64(point.x, canonical_x) ||
        !export_u64(point.y, canonical_y) ||
        canonical_x >= field.modulus() ||
        canonical_y >= field.modulus()) {
        throw std::logic_error(
            "auxiliary point does not fit its proven 64-bit field");
    }
    encoded.x = field.encode(canonical_x);
    encoded.y = field.encode(canonical_y);
    encoded.infinity = false;
    return encoded;
}

AffinePoint decode_auxiliary_point(const AuxiliaryPoint64& point,
                                   const AuxiliaryField64& field) {
    if (point.infinity) {
        return {};
    }
    return {import_u64(field.decode(point.x)),
            import_u64(field.decode(point.y)), false};
}

bool auxiliary_point_is_on_curve(const AuxiliaryCurve64& curve,
                                 const AuxiliaryPoint64& point) {
    if (point.infinity) {
        return point.x == 0U && point.y == 0U;
    }
    const AuxiliaryField64& field = curve.field;
    if (point.x >= field.modulus() || point.y >= field.modulus()) {
        return false;
    }
    const std::uint64_t rhs = field.add(
        field.add(field.mul(field.square(point.x), point.x),
                  field.mul(curve.a, point.x)),
        curve.b);
    return field.square(point.y) == rhs;
}

AuxiliaryPoint64 auxiliary_point_add(const AuxiliaryCurve64& curve,
                                     const AuxiliaryPoint64& lhs,
                                     const AuxiliaryPoint64& rhs) {
    if (!auxiliary_point_is_on_curve(curve, lhs) ||
        !auxiliary_point_is_on_curve(curve, rhs)) {
        throw std::logic_error(
            "fast auxiliary addition received an invalid point");
    }
    if (lhs.infinity) {
        return rhs;
    }
    if (rhs.infinity) {
        return lhs;
    }
    const AuxiliaryField64& field = curve.field;
    if (lhs.x == rhs.x && field.add(lhs.y, rhs.y) == 0U) {
        return {};
    }

    std::uint64_t numerator = 0U;
    std::uint64_t denominator = 0U;
    if (lhs.x == rhs.x) {
        if (lhs.y != rhs.y || lhs.y == 0U) {
            throw std::logic_error(
                "fast auxiliary doubling reached an invalid state");
        }
        numerator = field.add(
            field.mul(field.constant(3U), field.square(lhs.x)),
            curve.a);
        denominator = field.mul(field.constant(2U), lhs.y);
    } else {
        numerator = field.sub(rhs.y, lhs.y);
        denominator = field.sub(rhs.x, lhs.x);
    }
    const std::uint64_t slope = field.mul(
        numerator, field.inverse(denominator));
    const std::uint64_t x = field.sub(
        field.sub(field.square(slope), lhs.x), rhs.x);
    const std::uint64_t y = field.sub(
        field.mul(slope, field.sub(lhs.x, x)), lhs.y);
    const AuxiliaryPoint64 output{x, y, false};
    if (!auxiliary_point_is_on_curve(curve, output)) {
        throw std::logic_error(
            "fast auxiliary addition produced an invalid point");
    }
    return output;
}

RationalLevelBasis find_rational_level_basis(
    const Curve& curve, unsigned level, const mpz_class& exact_group_order,
    std::uint64_t maximum_x_candidates) {
    validate_level(level, curve.field().modulus());
    if (curve.is_singular()) {
        throw std::invalid_argument(
            "cannot enumerate isogenies from a singular curve");
    }
    if (exact_group_order <= 0 || maximum_x_candidates == 0U) {
        throw std::invalid_argument(
            "isogeny enumeration requires a positive order and work cap");
    }
    const mpz_class trace =
        curve.field().modulus() + 1 - exact_group_order;
    if (trace * trace > 4 * curve.field().modulus()) {
        throw std::invalid_argument(
            "supplied group order lies outside the Hasse interval");
    }

    const mpz_class encoded_level(static_cast<unsigned long>(level));
    mpz_class prime_to_level_cofactor = exact_group_order;
    unsigned level_valuation = 0U;
    while (mpz_divisible_p(prime_to_level_cofactor.get_mpz_t(),
                           encoded_level.get_mpz_t()) != 0) {
        prime_to_level_cofactor /= encoded_level;
        ++level_valuation;
    }
    if (level_valuation < 2U) {
        throw std::invalid_argument(
            "full rational level torsion requires ell^2 to divide the order");
    }

    std::uint64_t characteristic = 0U;
    if (!export_u64(curve.field().modulus(), characteristic)) {
        throw std::invalid_argument(
            "auxiliary isogeny enumeration currently requires a 64-bit field");
    }
    if (!is_prime_u64_thread_cached(characteristic)) {
        throw std::invalid_argument(
            "rational isogeny enumeration requires proven prime characteristic");
    }
    const std::uint64_t scan_limit =
        std::min(maximum_x_candidates, characteristic);
    const Field& field = curve.field();
    std::optional<AffinePoint> first;
    std::uint64_t tested = 0U;
    for (std::uint64_t x_value = 0U; x_value < scan_limit; ++x_value) {
        ++tested;
        const mpz_class x(std::to_string(x_value));
        const mpz_class rhs = field.add(
            field.add(field.mul(field.square(x), x),
                      field.mul(curve.a(), x)),
            curve.b());
        const std::optional<mpz_class> root =
            square_root_mod_prime(field, rhs);
        if (!root.has_value()) {
            continue;
        }
        const mpz_class y = std::min(*root, field.neg(*root));
        AffinePoint generator = project_to_order_level(
            curve, {x, y, false}, prime_to_level_cofactor, level,
            level_valuation);
        if (generator.infinity) {
            continue;
        }
        if (!affine_scalar_multiply(curve, encoded_level, generator)
                 .infinity) {
            throw std::logic_error(
                "ell-primary projection did not produce ell-torsion");
        }
        if (!first.has_value()) {
            first = std::move(generator);
            continue;
        }
        if (!same_cyclic_subgroup(curve, *first, generator, level)) {
            return {*first, std::move(generator), tested, level_valuation};
        }
    }
    throw std::runtime_error(
        "failed to find a rational E[ell] basis within the x-coordinate cap");
}

template <class Consumer>
void for_each_projective_generator(const Curve& curve, unsigned level,
                                   const RationalLevelBasis& basis,
                                   Consumer&& consume) {
    consume(basis.first);
    AffinePoint generator = basis.second;
    for (unsigned k = 0U; k < level; ++k) {
        consume(generator);
        if (k + 1U < level) {
            generator = affine_point_add(curve, generator, basis.first);
            if (generator.infinity) {
                throw std::logic_error(
                    "independent rational torsion basis became dependent");
            }
        }
    }
}

AuxiliaryCurve64 encode_auxiliary_curve(const Curve& curve) {
    std::uint64_t modulus = 0U;
    std::uint64_t a = 0U;
    std::uint64_t b = 0U;
    if (!export_u64(curve.field().modulus(), modulus) ||
        !export_u64(curve.a(), a) || !export_u64(curve.b(), b) ||
        a >= modulus || b >= modulus) {
        throw std::logic_error(
            "curve does not fit its proven 64-bit auxiliary field");
    }
    AuxiliaryField64 field(modulus);
    AuxiliaryCurve64 encoded{
        field, field.encode(a), field.encode(b)};
    if (curve.is_singular()) {
        throw std::logic_error(
            "cannot encode a singular auxiliary curve");
    }
    return encoded;
}

std::vector<AuxiliaryPoint64> auxiliary_projective_line_generators(
    const AuxiliaryCurve64& curve, unsigned level,
    const RationalLevelBasis& basis) {
    const AuxiliaryPoint64 first =
        encode_auxiliary_point(basis.first, curve.field);
    AuxiliaryPoint64 generator =
        encode_auxiliary_point(basis.second, curve.field);
    if (first.infinity || generator.infinity ||
        !auxiliary_point_is_on_curve(curve, first) ||
        !auxiliary_point_is_on_curve(curve, generator)) {
        throw std::logic_error(
            "proved rational basis failed 64-bit conversion");
    }

    std::vector<AuxiliaryPoint64> generators;
    generators.reserve(static_cast<std::size_t>(level) + 1U);
    generators.push_back(first);
    for (unsigned k = 0U; k < level; ++k) {
        generators.push_back(generator);
        if (k + 1U < level) {
            generator = auxiliary_point_add(curve, generator, first);
            if (generator.infinity) {
                throw std::logic_error(
                    "fast rational torsion basis became dependent");
            }
        }
    }
    if (generators.size() != static_cast<std::size_t>(level) + 1U) {
        throw std::logic_error(
            "fast rational basis produced the wrong projective-line size");
    }
    return generators;
}

std::vector<AuxiliaryPoint64> auxiliary_batch_add_generators(
    const AuxiliaryCurve64& curve,
    const std::vector<AuxiliaryPoint64>& points,
    const std::vector<AuxiliaryPoint64>& generators) {
    if (points.empty() || points.size() != generators.size()) {
        throw std::invalid_argument(
            "fast batch addition received inconsistent point sets");
    }
    const AuxiliaryField64& field = curve.field;
    const std::size_t count = points.size();
    std::vector<std::uint64_t> numerators(count);
    std::vector<std::uint64_t> denominators(count);
    std::vector<std::uint64_t> prefixes(count);
    std::uint64_t product = field.one();
    for (std::size_t index = 0U; index < count; ++index) {
        const AuxiliaryPoint64& lhs = points[index];
        const AuxiliaryPoint64& rhs = generators[index];
        // generators were proved when the basis/projective line was built,
        // and every preceding batch output was checked below.  Re-evaluating
        // two curve equations for every point at every scalar would triple
        // the batch's field work without adding a new trust boundary.
        if (lhs.x >= field.modulus() || lhs.y >= field.modulus() ||
            rhs.x >= field.modulus() || rhs.y >= field.modulus() ||
            lhs.infinity || rhs.infinity) {
            throw std::logic_error(
                "fast projective-line batch contains an invalid input point");
        }
        if (lhs.x == rhs.x) {
            if (lhs.y != rhs.y || lhs.y == 0U) {
                throw std::logic_error(
                    "fast projective-line batch reached an inverse point");
            }
            numerators[index] = field.add(
                field.mul(field.constant(3U), field.square(lhs.x)),
                curve.a);
            denominators[index] = field.mul(
                field.constant(2U), lhs.y);
        } else {
            numerators[index] = field.sub(rhs.y, lhs.y);
            denominators[index] = field.sub(rhs.x, lhs.x);
        }
        if (denominators[index] == 0U) {
            throw std::logic_error(
                "fast projective-line batch has a zero denominator");
        }
        prefixes[index] = product;
        product = field.mul(product, denominators[index]);
    }

    std::uint64_t inverse_product = field.inverse(product);
    std::vector<std::uint64_t> inverses(count);
    for (std::size_t offset = 0U; offset < count; ++offset) {
        const std::size_t index = count - 1U - offset;
        inverses[index] = field.mul(inverse_product, prefixes[index]);
        inverse_product = field.mul(
            inverse_product, denominators[index]);
    }
    if (inverse_product != field.one()) {
        throw std::logic_error(
            "fast batch inversion failed its product check");
    }

    std::vector<AuxiliaryPoint64> output;
    output.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const AuxiliaryPoint64& lhs = points[index];
        const AuxiliaryPoint64& rhs = generators[index];
        const std::uint64_t slope = field.mul(
            numerators[index], inverses[index]);
        const std::uint64_t x = field.sub(
            field.sub(field.square(slope), lhs.x), rhs.x);
        const std::uint64_t y = field.sub(
            field.mul(slope, field.sub(lhs.x, x)), lhs.y);
        const AuxiliaryPoint64 sum{x, y, false};
        if (!auxiliary_point_is_on_curve(curve, sum)) {
            throw std::logic_error(
                "fast batch addition produced an invalid point");
        }
        output.push_back(sum);
    }
    return output;
}

std::pair<std::uint64_t, std::uint64_t>
auxiliary_velu_codomain_coefficients(
    const AuxiliaryCurve64& curve, std::size_t degree,
    std::uint64_t power_sum_1, std::uint64_t power_sum_2,
    std::uint64_t power_sum_3) {
    const AuxiliaryField64& field = curve.field;
    const auto constant = [&field](std::uint64_t value) {
        return field.constant(value);
    };
    const std::uint64_t twice_degree = constant(
        static_cast<std::uint64_t>(2U * degree));
    const std::uint64_t four_times_degree = constant(
        static_cast<std::uint64_t>(4U * degree));
    const std::uint64_t t = field.add(
        field.mul(constant(6U), power_sum_2),
        field.mul(twice_degree, curve.a));
    const std::uint64_t w = field.add(
        field.add(field.mul(constant(10U), power_sum_3),
                  field.mul(constant(6U),
                            field.mul(curve.a, power_sum_1))),
        field.mul(four_times_degree, curve.b));
    const std::uint64_t a = field.sub(
        curve.a, field.mul(constant(5U), t));
    const std::uint64_t b = field.sub(
        curve.b, field.mul(constant(7U), w));
    const std::uint64_t discriminant = field.add(
        field.mul(constant(4U), field.mul(a, field.square(a))),
        field.mul(constant(27U), field.square(b)));
    if (discriminant == 0U) {
        throw std::logic_error(
            "fast Velu quotient produced a singular codomain");
    }
    return {a, b};
}

}  // namespace

bool affine_point_is_on_curve(const Curve& curve, const AffinePoint& point) {
    if (!canonical_point(curve, point)) {
        return false;
    }
    if (point.infinity) {
        return true;
    }
    const Field& field = curve.field();
    const mpz_class rhs = field.add(
        field.add(field.mul(field.square(point.x), point.x),
                  field.mul(curve.a(), point.x)),
        curve.b());
    return field.square(point.y) == rhs;
}

AffinePoint affine_point_negate(const Curve& curve,
                                const AffinePoint& point) {
    require_point(curve, point);
    if (point.infinity) {
        return {};
    }
    return {point.x, curve.field().neg(point.y), false};
}

AffinePoint affine_point_add(const Curve& curve, const AffinePoint& lhs,
                             const AffinePoint& rhs) {
    require_point(curve, lhs);
    require_point(curve, rhs);
    if (lhs.infinity) {
        return rhs;
    }
    if (rhs.infinity) {
        return lhs;
    }
    const Field& field = curve.field();
    if (lhs.x == rhs.x && field.add(lhs.y, rhs.y) == 0) {
        return {};
    }

    mpz_class slope;
    if (lhs.x == rhs.x) {
        if (lhs.y != rhs.y || lhs.y == 0) {
            throw std::logic_error("invalid affine doubling state");
        }
        slope = field.divide(
            field.add(field.mul(3, field.square(lhs.x)), curve.a()),
            field.mul(2, lhs.y));
    } else {
        slope = field.divide(field.sub(rhs.y, lhs.y),
                             field.sub(rhs.x, lhs.x));
    }
    const mpz_class x =
        field.sub(field.sub(field.square(slope), lhs.x), rhs.x);
    const mpz_class y =
        field.sub(field.mul(slope, field.sub(lhs.x, x)), lhs.y);
    const AffinePoint output{x, y, false};
    if (!affine_point_is_on_curve(curve, output)) {
        throw std::logic_error("affine addition produced an invalid point");
    }
    return output;
}

AffinePoint affine_scalar_multiply(const Curve& curve,
                                   const mpz_class& scalar,
                                   AffinePoint point) {
    if (scalar < 0) {
        throw std::invalid_argument("affine scalar must be nonnegative");
    }
    require_point(curve, point);
    AffinePoint result;
    const std::size_t bits =
        scalar == 0 ? 0U : mpz_sizeinbase(scalar.get_mpz_t(), 2);
    for (std::size_t bit = 0U; bit < bits; ++bit) {
        if (mpz_tstbit(scalar.get_mpz_t(), bit) != 0) {
            result = affine_point_add(curve, result, point);
        }
        if (bit + 1U < bits) {
            point = affine_point_add(curve, point, point);
        }
    }
    return result;
}

Poly cyclic_kernel_polynomial(const Curve& curve,
                              const AffinePoint& generator,
                              unsigned level) {
    require_exact_prime_order_generator(curve, generator, level);

    const Field& field = curve.field();
    Poly kernel = Poly::constant(field, 1);
    AffinePoint multiple = generator;
    std::vector<mpz_class> roots;
    roots.reserve(static_cast<std::size_t>((level - 1U) / 2U));
    for (unsigned scalar = 1U; scalar <= (level - 1U) / 2U; ++scalar) {
        if (multiple.infinity ||
            std::find(roots.begin(), roots.end(), multiple.x) != roots.end()) {
            throw std::logic_error(
                "prime-order subgroup has a repeated kernel abscissa");
        }
        roots.push_back(multiple.x);
        kernel = mul(kernel, Poly(field, {field.neg(multiple.x), 1}));
        if (scalar < (level - 1U) / 2U) {
            multiple = affine_point_add(curve, multiple, generator);
        }
    }
    if (kernel.degree() != static_cast<int>((level - 1U) / 2U) ||
        kernel.leading_coefficient() != 1) {
        throw std::logic_error("cyclic kernel has the wrong normalization");
    }
    return kernel;
}

Curve velu_codomain_from_cyclic_subgroup(const Curve& curve,
                                         const AffinePoint& generator,
                                         unsigned level) {
    require_exact_prime_order_generator(curve, generator, level);
    const Field& field = curve.field();
    mpz_class t = 0;
    mpz_class w = 0;
    AffinePoint multiple = generator;
    for (unsigned scalar = 1U; scalar < level; ++scalar) {
        if (multiple.infinity) {
            throw std::logic_error("cyclic subgroup ended before its order");
        }
        const mpz_class x_squared = field.square(multiple.x);
        const mpz_class x_cubed = field.mul(x_squared, multiple.x);
        t = field.add(t, field.add(field.mul(3, x_squared), curve.a()));
        w = field.add(
            w, field.add(
                   field.add(field.mul(5, x_cubed),
                             field.mul(3, field.mul(curve.a(), multiple.x))),
                   field.mul(2, curve.b())));
        if (scalar + 1U < level) {
            multiple = affine_point_add(curve, multiple, generator);
        }
    }
    Curve codomain(field, field.sub(curve.a(), field.mul(5, t)),
                   field.sub(curve.b(), field.mul(7, w)));
    if (codomain.is_singular()) {
        throw std::logic_error("Velu quotient produced a singular codomain");
    }
    return codomain;
}

RationalPrimeIsogenyEnumeration enumerate_rational_prime_isogenies(
    const Curve& curve, unsigned level, const mpz_class& exact_group_order,
    std::uint64_t maximum_x_candidates) {
    const RationalLevelBasis basis = find_rational_level_basis(
        curve, level, exact_group_order, maximum_x_candidates);
    const std::size_t required = static_cast<std::size_t>(level) + 1U;
    RationalPrimeIsogenyEnumeration output{
        {}, basis.x_candidates_tested,
        basis.group_order_level_valuation};
    output.isogenies.reserve(required);
    for_each_projective_generator(
        curve, level, basis, [&](const AffinePoint& generator) {
        Poly kernel = cyclic_kernel_polynomial(curve, generator, level);
        if (std::any_of(
                output.isogenies.begin(), output.isogenies.end(),
                [&kernel](const RationalPrimeIsogeny& existing) {
                    return same_kernel(existing.kernel, kernel);
                })) {
            throw std::logic_error(
                "rational E[ell] basis produced a duplicate subgroup");
        }
        Curve codomain = velu_codomain_from_validated_kernel(
            curve, kernel, level);
        output.isogenies.push_back(
            {generator, std::move(kernel), std::move(codomain)});
    });
    if (output.isogenies.size() != required) {
        throw std::logic_error(
            "rational E[ell] basis did not enumerate the projective line");
    }
    return output;
}

RationalPrimeIsogenyNeighborEnumeration
enumerate_rational_prime_isogeny_neighbors(
    const Curve& curve, unsigned level, const mpz_class& exact_group_order,
    std::uint64_t maximum_x_candidates) {
    const RationalLevelBasis basis = find_rational_level_basis(
        curve, level, exact_group_order, maximum_x_candidates);
    const std::size_t required = static_cast<std::size_t>(level) + 1U;
    RationalPrimeIsogenyNeighborEnumeration output{
        {}, basis.x_candidates_tested,
        basis.group_order_level_valuation};
    output.neighbors.reserve(required);
    const AuxiliaryCurve64 auxiliary_curve =
        encode_auxiliary_curve(curve);
    const AuxiliaryField64& field = auxiliary_curve.field;
    const std::vector<AuxiliaryPoint64> generators =
        auxiliary_projective_line_generators(
            auxiliary_curve, level, basis);
    std::vector<std::uint64_t> power_sum_1(required, 0U);
    std::vector<std::uint64_t> power_sum_2(required, 0U);
    std::vector<std::uint64_t> power_sum_3(required, 0U);
    std::vector<AuxiliaryPoint64> multiples = generators;
    const unsigned half = (level - 1U) / 2U;
    for (unsigned scalar = 1U; scalar <= half; ++scalar) {
        for (std::size_t index = 0U; index < required; ++index) {
            if (multiples[index].infinity) {
                throw std::logic_error(
                    "fast batch Velu half-system contains infinity");
            }
            const std::uint64_t x_squared =
                field.square(multiples[index].x);
            power_sum_1[index] = field.add(
                power_sum_1[index], multiples[index].x);
            power_sum_2[index] = field.add(
                power_sum_2[index], x_squared);
            power_sum_3[index] = field.add(
                power_sum_3[index],
                field.mul(x_squared, multiples[index].x));
        }
        if (scalar < half) {
            multiples = auxiliary_batch_add_generators(
                auxiliary_curve, multiples, generators);
        }
    }
    for (std::size_t index = 0U; index < required; ++index) {
        const auto [a, b] = auxiliary_velu_codomain_coefficients(
            auxiliary_curve, half, power_sum_1[index],
            power_sum_2[index], power_sum_3[index]);
        Curve codomain(
            curve.field(), import_u64(field.decode(a)),
            import_u64(field.decode(b)));
        if (codomain.is_singular()) {
            throw std::logic_error(
                "decoded fast Velu quotient is singular");
        }
        output.neighbors.push_back({
            decode_auxiliary_point(generators[index], field),
            std::move(codomain)});
    }
    if (output.neighbors.size() != required) {
        throw std::logic_error(
            "compact E[ell] basis did not enumerate the projective line");
    }
    return output;
}

}  // namespace oneshotsea
