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
    if (!is_prime_u64(characteristic)) {
        throw std::invalid_argument(
            "rational isogeny enumeration requires proven prime characteristic");
    }
    const std::uint64_t scan_limit =
        std::min(maximum_x_candidates, characteristic);
    const std::size_t required = static_cast<std::size_t>(level) + 1U;
    RationalPrimeIsogenyEnumeration output{{}, 0U, level_valuation};
    output.isogenies.reserve(required);
    const Field& field = curve.field();

    for (std::uint64_t x_value = 0U; x_value < scan_limit; ++x_value) {
        ++output.x_candidates_tested;
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
        const AffinePoint sampled{x, y, false};
        AffinePoint generator = project_to_order_level(
            curve, sampled, prime_to_level_cofactor, level,
            level_valuation);
        if (generator.infinity) {
            continue;
        }
        if (!affine_scalar_multiply(curve, encoded_level, generator)
                 .infinity) {
            throw std::logic_error(
                "ell-primary projection did not produce ell-torsion");
        }
        Poly kernel = cyclic_kernel_polynomial(curve, generator, level);
        if (std::any_of(
                output.isogenies.begin(), output.isogenies.end(),
                [&kernel](const RationalPrimeIsogeny& existing) {
                    return same_kernel(existing.kernel, kernel);
                })) {
            continue;
        }
        Curve codomain =
            velu_codomain_from_cyclic_subgroup(curve, generator, level);
        output.isogenies.push_back(
            {std::move(generator), std::move(kernel), std::move(codomain)});
        if (output.isogenies.size() == required) {
            return output;
        }
    }
    throw std::runtime_error(
        "failed to enumerate all ell+1 rational cyclic subgroups within the x-coordinate cap");
}

}  // namespace oneshotsea
