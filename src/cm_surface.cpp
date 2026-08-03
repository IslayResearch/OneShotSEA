#include "oneshotsea/cm_surface.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
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

struct ClassicalInterpolationMatrices {
    mpz_class auxiliary_prime;
    std::vector<mpz_class> lagrange_coefficients;
    std::vector<mpz_class> neighbor_coefficients;
};

std::size_t square_size(std::size_t count) {
    if (count != 0U &&
        count > std::numeric_limits<std::size_t>::max() / count) {
        throw std::overflow_error(
            "classical interpolation matrix size overflows size_t");
    }
    return count * count;
}

ClassicalInterpolationMatrices prepare_classical_interpolation_matrices(
    const CmSurfaceEnumeration& surfaces) {
    const unsigned level = surfaces.level();
    const std::size_t count = static_cast<std::size_t>(level) + 2U;
    const std::vector<CmSurfaceCurve>& surface_curves =
        surfaces.surface_curves();
    if (surface_curves.size() < count) {
        throw std::invalid_argument(
            "CM specialization has too few interpolation surfaces");
    }
    const Field field(surfaces.auxiliary_prime());
    Poly root_product = Poly::constant(field, 1);
    for (std::size_t row = 0U; row < count; ++row) {
        const CmSurfaceCurve& surface = surface_curves[row];
        if (surface.curve.field().modulus() !=
                surfaces.auxiliary_prime() ||
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
        throw std::logic_error("CM interpolation points are not distinct");
    }

    const std::size_t entries = square_size(count);
    std::vector<mpz_class> lagrange(entries, 0);
    std::vector<mpz_class> neighbors(entries, 0);
    for (std::size_t row = 0U; row < count; ++row) {
        const CmSurfaceCurve& surface = surface_curves[row];
        const mpz_class& x = surface.j_invariant;
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

        Poly neighbor_polynomial = Poly::constant(field, 1);
        for (const CmSurfaceEdge& edge : surface.edges) {
            const mpz_class neighbor =
                edge.isogeny.codomain.j_invariant();
            neighbor_polynomial = mul(
                neighbor_polynomial,
                Poly(field, {field.neg(neighbor), 1}));
        }
        if (neighbor_polynomial.degree() !=
                static_cast<int>(level + 1U) ||
            neighbor_polynomial.leading_coefficient() != 1) {
            throw std::logic_error(
                "CM surface neighbor polynomial has the wrong degree");
        }
        for (std::size_t degree = 0U; degree < count; ++degree) {
            const std::size_t index = row * count + degree;
            lagrange[index] = basis.coefficient(degree);
            neighbors[index] = neighbor_polynomial.coefficient(degree);
        }
    }
    for (std::size_t degree = 0U; degree < count; ++degree) {
        mpz_class sum = 0;
        for (std::size_t row = 0U; row < count; ++row) {
            sum = field.add(sum, lagrange[row * count + degree]);
        }
        const mpz_class expected = degree == 0U ? mpz_class(1)
                                                : mpz_class(0);
        if (sum != expected) {
            throw std::logic_error(
                "CM Lagrange rows do not form a partition of unity");
        }
    }
    return {surfaces.auxiliary_prime(), std::move(lagrange),
            std::move(neighbors)};
}

CrtSpecializationResidue specialize_classical_from_interpolation_matrices(
    unsigned level, const mpz_class& auxiliary_prime,
    const std::vector<mpz_class>& lagrange_coefficients,
    const std::vector<mpz_class>& neighbor_coefficients,
    const std::vector<mpz_class>& target_power_lifts) {
    const std::size_t count = static_cast<std::size_t>(level) + 2U;
    const std::size_t entries = square_size(count);
    if (lagrange_coefficients.size() != entries ||
        neighbor_coefficients.size() != entries ||
        target_power_lifts.size() != count ||
        target_power_lifts.empty() || target_power_lifts.front() != 1) {
        throw std::invalid_argument(
            "CM specialization has the wrong matrix or target-power count");
    }
    for (const mpz_class& lift : target_power_lifts) {
        if (lift < 0) {
            throw std::invalid_argument(
                "CM specialization target-power lift is negative");
        }
    }

    const Field field(auxiliary_prime);
    std::vector<mpz_class> value_weights(count, 0);
    std::vector<mpz_class> derivative_weights(count, 0);
    for (std::size_t row = 0U; row < count; ++row) {
        for (std::size_t degree = 0U; degree < count; ++degree) {
            const mpz_class& coefficient =
                lagrange_coefficients[row * count + degree];
            if (coefficient < 0 || coefficient >= auxiliary_prime) {
                throw std::logic_error(
                    "prepared Lagrange coefficient is noncanonical");
            }
            value_weights[row] = field.add(
                value_weights[row],
                field.mul(coefficient,
                          field.normalize(target_power_lifts[degree])));
            if (degree != 0U) {
                derivative_weights[row] = field.add(
                    derivative_weights[row],
                    field.mul(
                        field.mul(
                            coefficient,
                            mpz_class(static_cast<unsigned long>(degree))),
                        field.normalize(target_power_lifts[degree - 1U])));
            }
        }
    }

    std::vector<mpz_class> value(count, 0);
    std::vector<mpz_class> x_derivative(count, 0);
    for (std::size_t row = 0U; row < count; ++row) {
        for (std::size_t y_degree = 0U; y_degree < count; ++y_degree) {
            const mpz_class& coefficient =
                neighbor_coefficients[row * count + y_degree];
            if (coefficient < 0 || coefficient >= auxiliary_prime) {
                throw std::logic_error(
                    "prepared neighbor coefficient is noncanonical");
            }
            value[y_degree] = field.add(
                value[y_degree],
                field.mul(value_weights[row], coefficient));
            x_derivative[y_degree] = field.add(
                x_derivative[y_degree],
                field.mul(derivative_weights[row], coefficient));
        }
    }
    if (value.back() != 1 || x_derivative.back() != 0) {
        throw std::logic_error(
            "CM interpolation violated the monic modular-polynomial terms");
    }
    return {auxiliary_prime, std::move(value), std::move(x_derivative)};
}

std::uint64_t multiply_mod_u64(std::uint64_t lhs, std::uint64_t rhs,
                               std::uint64_t modulus) {
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(lhs) * rhs) % modulus);
}

std::uint64_t add_mod_u64(std::uint64_t lhs, std::uint64_t rhs,
                          std::uint64_t modulus) {
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(lhs) + rhs) % modulus);
}

mpz_class import_u64(std::uint64_t value) {
    mpz_class output;
    mpz_import(output.get_mpz_t(), 1U, -1, sizeof(value), 0, 0, &value);
    return output;
}

std::vector<std::uint64_t> encode_interpolation_coefficients(
    const std::vector<mpz_class>& coefficients,
    std::uint64_t auxiliary_prime) {
    std::vector<std::uint64_t> encoded;
    encoded.reserve(coefficients.size());
    for (const mpz_class& coefficient : coefficients) {
        std::uint64_t value = 0U;
        if (!export_u64(coefficient, value) || value >= auxiliary_prime) {
            throw std::logic_error(
                "classical interpolation coefficient is noncanonical");
        }
        encoded.push_back(value);
    }
    return encoded;
}

CrtSpecializationResidue
specialize_classical_from_compact_interpolation_matrices(
    unsigned level, const mpz_class& auxiliary_prime,
    const std::vector<std::uint64_t>& lagrange_coefficients,
    const std::vector<std::uint64_t>& neighbor_coefficients,
    const std::vector<mpz_class>& target_power_lifts) {
    std::uint64_t modulus = 0U;
    if (!export_u64(auxiliary_prime, modulus) || modulus < 2U ||
        !is_prime_u64(modulus)) {
        throw std::logic_error(
            "compact classical interpolation lost its proven prime");
    }
    const std::size_t count = static_cast<std::size_t>(level) + 2U;
    const std::size_t entries = square_size(count);
    if (lagrange_coefficients.size() != entries ||
        neighbor_coefficients.size() != entries ||
        target_power_lifts.size() != count ||
        target_power_lifts.empty() || target_power_lifts.front() != 1) {
        throw std::invalid_argument(
            "compact CM specialization has the wrong matrix or target-power count");
    }

    std::vector<std::uint64_t> target_powers;
    target_powers.reserve(count);
    for (const mpz_class& lift : target_power_lifts) {
        if (lift < 0) {
            throw std::invalid_argument(
                "CM specialization target-power lift is negative");
        }
        mpz_class remainder;
        mpz_fdiv_r(remainder.get_mpz_t(), lift.get_mpz_t(),
                   auxiliary_prime.get_mpz_t());
        std::uint64_t encoded = 0U;
        if (!export_u64(remainder, encoded) || encoded >= modulus) {
            throw std::logic_error(
                "target-power reduction is outside the auxiliary field");
        }
        target_powers.push_back(encoded);
    }

    std::vector<std::uint64_t> value_weights(count, 0U);
    std::vector<std::uint64_t> derivative_weights(count, 0U);
    for (std::size_t row = 0U; row < count; ++row) {
        for (std::size_t degree = 0U; degree < count; ++degree) {
            const std::uint64_t coefficient =
                lagrange_coefficients[row * count + degree];
            if (coefficient >= modulus) {
                throw std::logic_error(
                    "compact Lagrange coefficient is noncanonical");
            }
            value_weights[row] = add_mod_u64(
                value_weights[row],
                multiply_mod_u64(coefficient, target_powers[degree],
                                 modulus),
                modulus);
            if (degree != 0U) {
                const std::uint64_t differentiated = multiply_mod_u64(
                    coefficient, static_cast<std::uint64_t>(degree),
                    modulus);
                derivative_weights[row] = add_mod_u64(
                    derivative_weights[row],
                    multiply_mod_u64(differentiated,
                                     target_powers[degree - 1U], modulus),
                    modulus);
            }
        }
    }

    std::vector<std::uint64_t> value_u64(count, 0U);
    std::vector<std::uint64_t> derivative_u64(count, 0U);
    for (std::size_t row = 0U; row < count; ++row) {
        for (std::size_t y_degree = 0U; y_degree < count; ++y_degree) {
            const std::uint64_t coefficient =
                neighbor_coefficients[row * count + y_degree];
            if (coefficient >= modulus) {
                throw std::logic_error(
                    "compact neighbor coefficient is noncanonical");
            }
            value_u64[y_degree] = add_mod_u64(
                value_u64[y_degree],
                multiply_mod_u64(value_weights[row], coefficient,
                                 modulus),
                modulus);
            derivative_u64[y_degree] = add_mod_u64(
                derivative_u64[y_degree],
                multiply_mod_u64(derivative_weights[row], coefficient,
                                 modulus),
                modulus);
        }
    }
    if (value_u64.back() != 1U || derivative_u64.back() != 0U) {
        throw std::logic_error(
            "compact CM interpolation violated the monic terms");
    }
    std::vector<mpz_class> value;
    std::vector<mpz_class> derivative;
    value.reserve(count);
    derivative.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        value.push_back(import_u64(value_u64[index]));
        derivative.push_back(import_u64(derivative_u64[index]));
    }
    return {auxiliary_prime, std::move(value), std::move(derivative)};
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
    std::vector<InterpolationSurface> interpolation_surfaces)
    : order_(std::move(order)), target_modulus_(std::move(target_modulus)),
      coefficient_abs_bound_(std::move(coefficient_abs_bound)),
      witnesses_(std::move(witnesses)),
      interpolation_surfaces_(std::move(interpolation_surfaces)) {}

std::size_t ClassicalDirectLevelContext::interpolation_coefficient_count()
    const {
    std::size_t total = 0U;
    for (const InterpolationSurface& surface : interpolation_surfaces_) {
        for (const std::size_t count :
             {surface.lagrange_coefficients.size(),
              surface.neighbor_coefficients.size()}) {
            if (count > std::numeric_limits<std::size_t>::max() - total) {
                throw std::overflow_error(
                    "classical interpolation coefficient count overflows size_t");
            }
            total += count;
        }
    }
    return total;
}

std::size_t ClassicalDirectLevelContext::interpolation_storage_bytes() const {
    const std::size_t count = interpolation_coefficient_count();
    if (count > std::numeric_limits<std::size_t>::max() /
                    sizeof(std::uint64_t)) {
        throw std::overflow_error(
            "classical interpolation storage size overflows size_t");
    }
    return count * sizeof(std::uint64_t);
}

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
    const ClassicalInterpolationMatrices matrices =
        prepare_classical_interpolation_matrices(surfaces);
    return specialize_classical_from_interpolation_matrices(
        surfaces.level(), matrices.auxiliary_prime,
        matrices.lagrange_coefficients, matrices.neighbor_coefficients,
        target_power_lifts);
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
    std::uint64_t maximum_x_candidates_per_surface,
    std::size_t worker_threads) {
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
    if (witnesses.empty()) {
        throw std::logic_error(
            "classical direct context selected no CRT witnesses");
    }
    std::size_t resolved_threads = worker_threads;
    if (resolved_threads == 0U) {
        resolved_threads = static_cast<std::size_t>(
            std::thread::hardware_concurrency());
        if (resolved_threads == 0U) {
            resolved_threads = 1U;
        }
    }
    resolved_threads = std::min(resolved_threads, witnesses.size());

    std::vector<std::optional<
        ClassicalDirectLevelContext::InterpolationSurface>> prepared(
        witnesses.size());
    std::vector<std::exception_ptr> failures(witnesses.size());
    std::atomic<std::size_t> next_index{0U};
    const auto worker = [&] {
        for (;;) {
            const std::size_t index =
                next_index.fetch_add(1U, std::memory_order_relaxed);
            if (index >= witnesses.size()) {
                return;
            }
            try {
                const SutherlandCrtPrime& witness = witnesses[index];
                const ClassicalCmClassPolynomial class_polynomial =
                    derive_three_power_class_polynomial_mod_prime(
                        order, witness);
                const CmSurfaceEnumeration surfaces =
                    enumerate_cm_interpolation_surfaces_limited(
                        order, witness, class_polynomial.polynomial(),
                        maximum_x_candidates_per_surface,
                        static_cast<std::size_t>(order.level()) + 2U);
                ClassicalInterpolationMatrices matrices =
                    prepare_classical_interpolation_matrices(surfaces);
                std::uint64_t encoded_prime = 0U;
                if (!export_u64(matrices.auxiliary_prime, encoded_prime) ||
                    !is_prime_u64(encoded_prime)) {
                    throw std::logic_error(
                        "classical interpolation matrix prime is not proved 64-bit");
                }
                prepared[index].emplace(
                    ClassicalDirectLevelContext::InterpolationSurface{
                        std::move(matrices.auxiliary_prime),
                        encode_interpolation_coefficients(
                            matrices.lagrange_coefficients, encoded_prime),
                        encode_interpolation_coefficients(
                            matrices.neighbor_coefficients, encoded_prime)});
            } catch (...) {
                failures[index] = std::current_exception();
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(resolved_threads > 0U ? resolved_threads - 1U : 0U);
    try {
        for (std::size_t index = 1U; index < resolved_threads; ++index) {
            workers.emplace_back(worker);
        }
    } catch (...) {
        next_index.store(witnesses.size(), std::memory_order_relaxed);
        for (std::thread& thread : workers) {
            thread.join();
        }
        throw;
    }
    worker();
    for (std::thread& thread : workers) {
        thread.join();
    }

    std::vector<ClassicalDirectLevelContext::InterpolationSurface>
        interpolation_surfaces;
    interpolation_surfaces.reserve(witnesses.size());
    for (std::size_t index = 0U; index < witnesses.size(); ++index) {
        if (failures[index]) {
            std::rethrow_exception(failures[index]);
        }
        if (!prepared[index].has_value()) {
            throw std::logic_error(
                "classical direct context worker produced no surface");
        }
        interpolation_surfaces.push_back(std::move(*prepared[index]));
    }
    return ClassicalDirectLevelContext(
        order, target_field.modulus(), coefficient_bound.absolute_bound(),
        std::move(witnesses), std::move(interpolation_surfaces));
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
        context.witnesses_.size() !=
            context.interpolation_surfaces_.size()) {
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
                    context.interpolation_surfaces_[next].auxiliary_prime !=
                        prime) {
                    throw std::logic_error(
                        "prepared classical direct prime stream lost synchronization");
                }
                const ClassicalDirectLevelContext::InterpolationSurface&
                    surface = context.interpolation_surfaces_[next++];
                return specialize_classical_from_compact_interpolation_matrices(
                    context.order_.level(), surface.auxiliary_prime,
                    surface.lagrange_coefficients,
                    surface.neighbor_coefficients, target_power_lifts);
            });
    if (next != context.witnesses_.size()) {
        throw std::logic_error(
            "prepared classical direct context was not fully consumed");
    }
    return result;
}

}  // namespace oneshotsea
