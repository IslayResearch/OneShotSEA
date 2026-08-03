#include "oneshotsea/cm_surface.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace oneshotsea {
namespace {

void validate_witness(const SutherlandSuitableOrder& order,
                      const SutherlandCrtPrime& witness) {
    std::uint64_t encoded_prime = 0U;
    if (!export_u64(witness.prime, encoded_prime) ||
        !is_prime_u64(encoded_prime)) {
        throw std::invalid_argument(
            "CM surface requires a proven 64-bit auxiliary prime");
    }
    const mpz_class level(static_cast<unsigned long>(order.level()));
    if (witness.trace <= 0 || witness.volcano_parameter <= 0 ||
        4 * witness.prime !=
            witness.trace * witness.trace - level * level *
                witness.volcano_parameter * witness.volcano_parameter *
                order.discriminant() ||
        mpz_fdiv_ui(witness.prime.get_mpz_t(), order.level()) != 1U ||
        mpz_fdiv_ui(witness.trace.get_mpz_t(), order.level()) != 2U ||
        mpz_divisible_ui_p(witness.volcano_parameter.get_mpz_t(),
                           order.level()) != 0 ||
        mpz_divisible_p(order.discriminant().get_mpz_t(),
                        witness.prime.get_mpz_t()) != 0) {
        throw std::invalid_argument(
            "CM surface received an invalid (p,t,v,D) witness");
    }
}

std::optional<RationalPrimeIsogenyEnumeration> try_surface_twist(
    const Curve& curve, unsigned level, const mpz_class& group_order,
    std::uint64_t maximum_x_candidates) {
    try {
        return enumerate_rational_prime_isogenies(
            curve, level, group_order, maximum_x_candidates);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

}  // namespace

CmSurfaceEnumeration::CmSurfaceEnumeration(
    unsigned level, mpz_class auxiliary_prime,
    std::vector<mpz_class> all_surface_invariants,
    std::vector<CmSurfaceCurve> surface_curves,
    mpz_class exact_group_order,
    std::size_t horizontal_edges_per_surface)
    : level_(level), auxiliary_prime_(std::move(auxiliary_prime)),
      all_surface_invariants_(std::move(all_surface_invariants)),
      surface_curves_(std::move(surface_curves)),
      exact_group_order_(std::move(exact_group_order)),
      horizontal_edges_per_surface_(horizontal_edges_per_surface) {}

ClassicalDirectLevelContext::ClassicalDirectLevelContext(
    SutherlandSuitableOrder order, mpz_class target_modulus,
    mpz_class coefficient_abs_bound,
    std::vector<SutherlandCrtPrime> witnesses,
    std::vector<CmSurfaceEnumeration> surfaces)
    : order_(std::move(order)), target_modulus_(std::move(target_modulus)),
      coefficient_abs_bound_(std::move(coefficient_abs_bound)),
      witnesses_(std::move(witnesses)), surfaces_(std::move(surfaces)) {}

CmSurfaceEnumeration enumerate_cm_interpolation_surfaces_limited(
    const SutherlandSuitableOrder& order,
    const SutherlandCrtPrime& prime_witness,
    const Poly& hilbert_class_polynomial_mod_prime,
    std::uint64_t maximum_x_candidates_per_surface,
    std::size_t surface_limit) {
    validate_witness(order, prime_witness);
    if (maximum_x_candidates_per_surface == 0U) {
        throw std::invalid_argument("CM surface x-coordinate cap is zero");
    }
    if (order.class_number() >
        static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument(
            "Hilbert class polynomial degree exceeds polynomial limits");
    }
    if (hilbert_class_polynomial_mod_prime.field().modulus() !=
            prime_witness.prime ||
        hilbert_class_polynomial_mod_prime.degree() !=
            static_cast<int>(order.class_number()) ||
        hilbert_class_polynomial_mod_prime.leading_coefficient() != 1) {
        throw std::invalid_argument(
            "Hilbert class polynomial has the wrong field, degree, or normalization");
    }
    if (gcd(hilbert_class_polynomial_mod_prime,
            hilbert_class_polynomial_mod_prime.derivative())
            .degree() != 0) {
        throw std::invalid_argument(
            "Hilbert class polynomial is not square-free modulo p");
    }
    std::vector<mpz_class> surface_roots =
        linear_roots(hilbert_class_polynomial_mod_prime);
    if (surface_roots.size() != order.class_number()) {
        throw std::invalid_argument(
            "Hilbert class polynomial does not split completely modulo p");
    }
    std::sort(surface_roots.begin(), surface_roots.end());
    if (std::adjacent_find(surface_roots.begin(), surface_roots.end()) !=
        surface_roots.end()) {
        throw std::logic_error(
            "square-free Hilbert class polynomial returned duplicate roots");
    }

    const std::size_t interpolation_count =
        static_cast<std::size_t>(order.level()) + 2U;
    if (surface_roots.size() < interpolation_count ||
        surface_limit < interpolation_count ||
        surface_limit > surface_roots.size()) {
        throw std::invalid_argument(
            "CM surface row limit is outside the interpolation range");
    }
    const mpz_class exact_group_order =
        prime_witness.prime + 1 - prime_witness.trace;
    const Field& field = hilbert_class_polynomial_mod_prime.field();
    const mpz_class nonsquare = least_quadratic_nonsquare(field);
    const mpz_class encoded_level(static_cast<unsigned long>(order.level()));
    const int splitting_symbol = mpz_kronecker(
        order.discriminant().get_mpz_t(), encoded_level.get_mpz_t());
    if (splitting_symbol < -1 || splitting_symbol > 1) {
        throw std::logic_error(
            "invalid quadratic splitting symbol for the CM order");
    }
    const std::size_t expected_horizontal =
        static_cast<std::size_t>(splitting_symbol + 1);

    std::vector<CmSurfaceCurve> surface_curves;
    surface_curves.reserve(surface_limit);
    for (std::size_t index = 0U; index < surface_limit; ++index) {
        const mpz_class& j = surface_roots[index];
        if (j == 0 || j == field.normalize(1728)) {
            throw std::runtime_error(
                "auxiliary prime has an exceptional reduced CM invariant");
        }
        Curve base = short_weierstrass_curve_from_j(field, j);
        Curve twist = base.quadratic_twist(nonsquare);
        std::optional<RationalPrimeIsogenyEnumeration> base_edges =
            try_surface_twist(base, order.level(), exact_group_order,
                              maximum_x_candidates_per_surface);
        std::optional<RationalPrimeIsogenyEnumeration> twist_edges =
            try_surface_twist(twist, order.level(), exact_group_order,
                              maximum_x_candidates_per_surface);
        if (base_edges.has_value() == twist_edges.has_value()) {
            throw std::runtime_error(
                "CM trace-sign admission found zero or two surface twists");
        }
        Curve selected = base_edges.has_value() ? std::move(base)
                                                : std::move(twist);
        RationalPrimeIsogenyEnumeration selected_edges =
            base_edges.has_value() ? std::move(*base_edges)
                                   : std::move(*twist_edges);
        std::vector<CmSurfaceEdge> classified;
        classified.reserve(selected_edges.isogenies.size());
        std::size_t horizontal = 0U;
        for (RationalPrimeIsogeny& edge : selected_edges.isogenies) {
            const bool on_surface = std::binary_search(
                surface_roots.begin(), surface_roots.end(),
                edge.codomain.j_invariant());
            horizontal += on_surface ? 1U : 0U;
            classified.push_back({std::move(edge), on_surface});
        }
        if (horizontal != expected_horizontal ||
            classified.size() != static_cast<std::size_t>(order.level()) + 1U) {
            throw std::runtime_error(
                "CM surface edge counts contradict quadratic splitting");
        }
        surface_curves.push_back(
            {j, std::move(selected), std::move(classified),
             selected_edges.x_candidates_tested});
    }
    return CmSurfaceEnumeration(
        order.level(), prime_witness.prime, std::move(surface_roots),
        std::move(surface_curves), exact_group_order,
        expected_horizontal);
}

CmSurfaceEnumeration enumerate_cm_interpolation_surfaces(
    const SutherlandSuitableOrder& order,
    const SutherlandCrtPrime& prime_witness,
    const Poly& hilbert_class_polynomial_mod_prime,
    std::uint64_t maximum_x_candidates_per_surface) {
    return enumerate_cm_interpolation_surfaces_limited(
        order, prime_witness, hilbert_class_polynomial_mod_prime,
        maximum_x_candidates_per_surface,
        static_cast<std::size_t>(order.class_number()));
}

CmSurfaceEnumeration enumerate_cm_interpolation_surfaces(
    const SutherlandSuitableOrder& order,
    const SutherlandCrtPrime& prime_witness,
    const ClassicalCmClassPolynomial& class_polynomial,
    std::uint64_t maximum_x_candidates_per_surface) {
    if (class_polynomial.discriminant() != order.discriminant() ||
        class_polynomial.auxiliary_prime() != prime_witness.prime) {
        throw std::invalid_argument(
            "authenticated class polynomial does not match the CM witness");
    }
    return enumerate_cm_interpolation_surfaces(
        order, prime_witness, class_polynomial.polynomial(),
        maximum_x_candidates_per_surface);
}

CrtSpecializationResidue specialize_classical_from_cm_surfaces(
    const CmSurfaceEnumeration& surfaces,
    const std::vector<mpz_class>& target_power_lifts) {
    const std::size_t count = static_cast<std::size_t>(surfaces.level_) + 2U;
    if (surfaces.surface_curves_.size() < count ||
        target_power_lifts.size() != count ||
        target_power_lifts.empty() || target_power_lifts.front() != 1) {
        throw std::invalid_argument(
            "CM specialization has the wrong surface or target-power count");
    }
    for (const mpz_class& lift : target_power_lifts) {
        if (lift < 0) {
            throw std::invalid_argument(
                "CM specialization target-power lift is negative");
        }
    }
    const Field field(surfaces.auxiliary_prime_);
    Poly root_product = Poly::constant(field, 1);
    for (std::size_t row = 0U; row < count; ++row) {
        const CmSurfaceCurve& surface = surfaces.surface_curves_[row];
        if (surface.curve.field().modulus() != surfaces.auxiliary_prime_ ||
            surface.j_invariant != surface.curve.j_invariant() ||
            surface.edges.size() + 1U != count) {
            throw std::invalid_argument(
                "CM specialization surface row is inconsistent");
        }
        root_product = mul(
            root_product,
            Poly(field, {field.neg(surface.j_invariant), 1}));
    }
    if (root_product.degree() != static_cast<int>(count)) {
        throw std::logic_error(
            "CM interpolation points are not distinct");
    }

    std::vector<mpz_class> value_weights(count, 0);
    std::vector<mpz_class> derivative_weights(count, 0);
    for (std::size_t row = 0U; row < count; ++row) {
        const mpz_class x =
            surfaces.surface_curves_[row].j_invariant;
        const Poly divisor(field, {field.neg(x), 1});
        auto quotient_and_remainder = divmod(root_product, divisor);
        if (!quotient_and_remainder.second.is_zero()) {
            throw std::logic_error(
                "CM interpolation root does not divide its product");
        }
        const mpz_class denominator =
            quotient_and_remainder.first.evaluate(x);
        if (denominator == 0) {
            throw std::invalid_argument(
                "CM interpolation points collide modulo p");
        }
        const Poly basis = scalar_mul(
            quotient_and_remainder.first, field.inverse(denominator));
        for (std::size_t degree = 0U; degree < count; ++degree) {
            value_weights[row] = field.add(
                value_weights[row],
                field.mul(basis.coefficient(degree),
                          field.normalize(target_power_lifts[degree])));
            if (degree != 0U) {
                derivative_weights[row] = field.add(
                    derivative_weights[row],
                    field.mul(
                        field.mul(
                            basis.coefficient(degree),
                            mpz_class(static_cast<unsigned long>(degree))),
                        field.normalize(target_power_lifts[degree - 1U])));
            }
        }
    }

    std::vector<mpz_class> value(count, 0);
    std::vector<mpz_class> x_derivative(count, 0);
    for (std::size_t row = 0U; row < count; ++row) {
        Poly neighbor_polynomial = Poly::constant(field, 1);
        for (const CmSurfaceEdge& edge :
             surfaces.surface_curves_[row].edges) {
            const mpz_class neighbor = edge.isogeny.codomain.j_invariant();
            neighbor_polynomial = mul(
                neighbor_polynomial,
                Poly(field, {field.neg(neighbor), 1}));
        }
        if (neighbor_polynomial.degree() !=
                static_cast<int>(surfaces.level_ + 1U) ||
            neighbor_polynomial.leading_coefficient() != 1) {
            throw std::logic_error(
                "CM surface neighbor polynomial has the wrong degree");
        }
        for (std::size_t y_degree = 0U; y_degree < count; ++y_degree) {
            const mpz_class coefficient =
                neighbor_polynomial.coefficient(y_degree);
            value[y_degree] = field.add(
                value[y_degree], field.mul(value_weights[row], coefficient));
            x_derivative[y_degree] = field.add(
                x_derivative[y_degree],
                field.mul(derivative_weights[row], coefficient));
        }
    }
    if (value.back() != 1 || x_derivative.back() != 0) {
        throw std::logic_error(
            "CM interpolation violated the monic modular-polynomial terms");
    }
    return {surfaces.auxiliary_prime_, std::move(value),
            std::move(x_derivative)};
}

CrtSpecializationResult reconstruct_classical_specialization_from_cm(
    const SutherlandSuitableOrder& order, const Field& target_field,
    const mpz_class& source_j, std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface) {
    const ClassicalDirectLevelContext context =
        prepare_classical_direct_level_context(
            order, target_field, maximum_prime_candidates,
            maximum_x_candidates_per_surface);
    return reconstruct_classical_specialization_from_prepared_context(
        context, target_field, source_j);
}

ClassicalDirectLevelContext prepare_classical_direct_level_context(
    const SutherlandSuitableOrder& order, const Field& target_field,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface) {
    if (maximum_prime_candidates == 0U ||
        maximum_x_candidates_per_surface == 0U) {
        throw std::invalid_argument(
            "classical direct context received a zero preparation cap");
    }
    const CrtCoefficientBound coefficient_bound =
        derive_proved_classical_algorithm1_coefficient_bound(
            order.level(), target_field.modulus());
    std::vector<SutherlandCrtPrime> witnesses =
        select_sutherland_crt_primes(
            order, target_field.modulus(),
            coefficient_bound.absolute_bound(), maximum_prime_candidates);
    std::vector<CmSurfaceEnumeration> surfaces;
    surfaces.reserve(witnesses.size());
    for (const SutherlandCrtPrime& witness : witnesses) {
        const ClassicalCmClassPolynomial class_polynomial =
            derive_three_power_class_polynomial_mod_prime(order, witness);
        surfaces.push_back(enumerate_cm_interpolation_surfaces_limited(
            order, witness, class_polynomial.polynomial(),
            maximum_x_candidates_per_surface,
            static_cast<std::size_t>(order.level()) + 2U));
    }
    return ClassicalDirectLevelContext(
        order, target_field.modulus(), coefficient_bound.absolute_bound(),
        std::move(witnesses), std::move(surfaces));
}

CrtSpecializationResult
reconstruct_classical_specialization_from_prepared_context(
    const ClassicalDirectLevelContext& context, const Field& target_field,
    const mpz_class& source_j) {
    if (target_field.modulus() != context.target_modulus_) {
        throw std::invalid_argument(
            "prepared classical direct context belongs to another field");
    }
    if (target_field.normalize(source_j) != source_j) {
        throw std::invalid_argument(
            "prepared classical direct source j is not canonical");
    }
    if (context.witnesses_.empty() ||
        context.witnesses_.size() != context.surfaces_.size()) {
        throw std::logic_error(
            "prepared classical direct context is incomplete");
    }
    const std::vector<mpz_class> target_power_lifts =
        lifted_target_powers(
            target_field, source_j, context.order_.level() + 1U);
    std::vector<mpz_class> primes;
    primes.reserve(context.witnesses_.size());
    for (const SutherlandCrtPrime& witness : context.witnesses_) {
        primes.push_back(witness.prime);
    }
    std::size_t next = 0U;
    CrtSpecializationResult result =
        reconstruct_specialization_explicit_crt(
            context.order_.level(), target_field, source_j,
            context.coefficient_abs_bound_, primes,
            [&context, &target_power_lifts, &next](const mpz_class& prime) {
                if (next >= context.witnesses_.size() ||
                    context.witnesses_[next].prime != prime ||
                    context.surfaces_[next].auxiliary_prime() != prime) {
                    throw std::logic_error(
                        "prepared classical direct prime stream lost synchronization");
                }
                return specialize_classical_from_cm_surfaces(
                    context.surfaces_[next++], target_power_lifts);
            });
    if (next != context.witnesses_.size()) {
        throw std::logic_error(
            "prepared classical direct context was not fully consumed");
    }
    return result;
}

}  // namespace oneshotsea
