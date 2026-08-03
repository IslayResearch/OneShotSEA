#include "oneshotsea/weber_cm_surface.hpp"

#include "oneshotsea/weber.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace oneshotsea {
namespace {

using InvariantMap = std::map<mpz_class, mpz_class>;

struct WeberInterpolationRow {
    mpz_class source;
    Poly neighbors;
};

void validate_orientation_relation(
    const SparseModularPolynomial& relation, unsigned target_level,
    const mpz_class& auxiliary_prime) {
    const unsigned level = relation.level();
    if (level == target_level || !is_prime_u64(level) ||
        auxiliary_prime == level) {
        throw std::invalid_argument(
            "Weber orientation relation has an invalid level");
    }
    using DegreePair = std::pair<unsigned, unsigned>;
    std::map<DegreePair, mpz_class> coefficients;
    for (const BivariateTerm& term : relation.terms()) {
        if (term.coefficient == 0 || term.x_degree > level + 1U ||
            term.y_degree > level + 1U ||
            ((level % 24U) * (term.x_degree % 24U) +
             term.y_degree % 24U) % 24U != (level + 1U) % 24U ||
            !coefficients.emplace(
                 DegreePair(term.x_degree, term.y_degree), term.coefficient)
                 .second) {
            throw std::invalid_argument(
                "Weber orientation relation has invalid sparse terms");
        }
    }
    const auto coefficient = [&coefficients](unsigned x, unsigned y) {
        const auto found = coefficients.find({x, y});
        return found == coefficients.end() ? mpz_class(0) : found->second;
    };
    if (coefficient(level + 1U, 0U) != 1 ||
        coefficient(0U, level + 1U) != 1 ||
        coefficient(level, level) != -1) {
        throw std::invalid_argument(
            "Weber orientation relation has invalid normalization");
    }
    for (const auto& [degrees, value] : coefficients) {
        if (coefficient(degrees.second, degrees.first) != value) {
            throw std::invalid_argument(
                "Weber orientation relation is not symmetric");
        }
    }
}

std::vector<std::vector<mpz_class>> connected_components(
    const Field& field, const InvariantMap& invariant_by_weber,
    const std::vector<SparseModularPolynomial>& relations) {
    std::map<mpz_class, std::set<mpz_class>> adjacency;
    for (const auto& [weber, unused] : invariant_by_weber) {
        static_cast<void>(unused);
        adjacency.emplace(weber, std::set<mpz_class>{});
    }
    for (const auto& [weber, unused] : invariant_by_weber) {
        static_cast<void>(unused);
        for (const SparseModularPolynomial& relation : relations) {
            const std::vector<mpz_class> roots =
                linear_roots(relation.evaluate_x(field, weber));
            for (const mpz_class& root : roots) {
                if (root != weber && invariant_by_weber.contains(root)) {
                    adjacency[weber].insert(root);
                    adjacency[root].insert(weber);
                }
            }
        }
    }

    std::set<mpz_class> visited;
    std::vector<std::vector<mpz_class>> components;
    for (const auto& [start, unused] : invariant_by_weber) {
        static_cast<void>(unused);
        if (visited.contains(start)) {
            continue;
        }
        std::vector<mpz_class> stack{start};
        std::vector<mpz_class> component;
        visited.insert(start);
        while (!stack.empty()) {
            const mpz_class current = std::move(stack.back());
            stack.pop_back();
            component.push_back(current);
            for (const mpz_class& neighbor : adjacency.at(current)) {
                if (visited.insert(neighbor).second) {
                    stack.push_back(neighbor);
                }
            }
        }
        std::sort(component.begin(), component.end());
        components.push_back(std::move(component));
    }
    std::sort(components.begin(), components.end());
    return components;
}

std::vector<Poly> lagrange_bases(
    const Field& field, const std::vector<mpz_class>& points) {
    Poly root_product = Poly::constant(field, 1);
    for (const mpz_class& point : points) {
        root_product = mul(
            root_product, Poly(field, {field.neg(point), 1}));
    }
    if (root_product.degree() != static_cast<int>(points.size())) {
        throw std::invalid_argument("Weber interpolation points collide");
    }
    std::vector<Poly> bases;
    bases.reserve(points.size());
    for (const mpz_class& point : points) {
        auto quotient_and_remainder = divmod(
            root_product, Poly(field, {field.neg(point), 1}));
        if (!quotient_and_remainder.second.is_zero()) {
            throw std::logic_error(
                "Weber interpolation point does not divide its product");
        }
        const mpz_class denominator =
            quotient_and_remainder.first.evaluate(point);
        if (denominator == 0) {
            throw std::invalid_argument(
                "Weber interpolation points are not distinct");
        }
        bases.push_back(scalar_mul(
            quotient_and_remainder.first, field.inverse(denominator)));
    }
    return bases;
}

Poly neighbor_polynomial(const Field& field,
                         const std::vector<mpz_class>& roots,
                         unsigned level) {
    if (roots.size() != static_cast<std::size_t>(level) + 1U) {
        throw std::logic_error(
            "Weber row has the wrong number of neighbors");
    }
    std::set<mpz_class> distinct;
    Poly result = Poly::constant(field, 1);
    for (const mpz_class& root : roots) {
        if (!distinct.insert(root).second) {
            throw std::runtime_error(
                "Weber row contains duplicate neighbor invariants");
        }
        result = mul(result, Poly(field, {field.neg(root), 1}));
    }
    if (result.degree() != static_cast<int>(level + 1U) ||
        result.leading_coefficient() != 1) {
        throw std::logic_error(
            "Weber neighbor polynomial has invalid normalization");
    }
    return result;
}

mpz_class relative_sign_coefficient(
    const Field& field, unsigned level,
    const std::vector<WeberInterpolationRow>& rows,
    const std::vector<Poly>& bases) {
    mpz_class result = 0;
    for (std::size_t row = 0U; row < rows.size(); ++row) {
        result = field.add(
            result,
            field.mul(bases[row].coefficient(level),
                      rows[row].neighbors.coefficient(level)));
    }
    return result;
}

CrtSpecializationResidue specialize_rows(
    const Field& field, unsigned level,
    const std::vector<WeberInterpolationRow>& rows,
    const std::vector<Poly>& bases,
    const std::vector<mpz_class>& target_power_lifts) {
    const std::size_t count = static_cast<std::size_t>(level) + 2U;
    if (rows.size() != count || bases.size() != count ||
        target_power_lifts.size() != count ||
        target_power_lifts.empty() || target_power_lifts.front() != 1) {
        throw std::invalid_argument(
            "Weber specialization has invalid interpolation dimensions");
    }
    for (const mpz_class& lift : target_power_lifts) {
        if (lift < 0) {
            throw std::invalid_argument(
                "Weber specialization target-power lift is negative");
        }
    }

    std::vector<mpz_class> value_weights(count, 0);
    std::vector<mpz_class> derivative_weights(count, 0);
    for (std::size_t row = 0U; row < count; ++row) {
        for (std::size_t degree = 0U; degree < count; ++degree) {
            value_weights[row] = field.add(
                value_weights[row],
                field.mul(bases[row].coefficient(degree),
                          field.normalize(target_power_lifts[degree])));
            if (degree != 0U) {
                derivative_weights[row] = field.add(
                    derivative_weights[row],
                    field.mul(
                        field.mul(
                            bases[row].coefficient(degree),
                            mpz_class(static_cast<unsigned long>(degree))),
                        field.normalize(target_power_lifts[degree - 1U])));
            }
        }
    }

    std::vector<mpz_class> value(count, 0);
    std::vector<mpz_class> x_derivative(count, 0);
    for (std::size_t row = 0U; row < count; ++row) {
        for (std::size_t y_degree = 0U; y_degree < count; ++y_degree) {
            const mpz_class coefficient =
                rows[row].neighbors.coefficient(y_degree);
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
            "Weber interpolation violated its monic terms");
    }
    return {field.modulus(), std::move(value), std::move(x_derivative)};
}

InvariantMap component_map(
    const std::vector<mpz_class>& component,
    const InvariantMap& invariant_by_weber,
    std::size_t expected_invariants) {
    if (component.size() != expected_invariants) {
        throw std::runtime_error(
            "Weber orientation component has the wrong size");
    }
    InvariantMap weber_by_invariant;
    for (const mpz_class& weber : component) {
        const mpz_class& invariant = invariant_by_weber.at(weber);
        if (!weber_by_invariant.emplace(invariant, weber).second) {
            throw std::runtime_error(
                "Weber orientation component repeats a j-invariant");
        }
    }
    return weber_by_invariant;
}

}  // namespace

WeberCmSpecialization specialize_weber_from_cm_surfaces(
    const CmSurfaceEnumeration& surfaces,
    const Poly& weber_surface_class_polynomial_mod_prime,
    const std::vector<SparseModularPolynomial>& orientation_relations,
    const std::vector<mpz_class>& target_power_lifts) {
    const unsigned level = surfaces.level();
    const mpz_class& auxiliary_prime = surfaces.auxiliary_prime();
    const std::size_t interpolation_count =
        static_cast<std::size_t>(level) + 2U;
    if (target_power_lifts.size() != interpolation_count ||
        target_power_lifts.empty() || target_power_lifts.front() != 1 ||
        std::any_of(
            target_power_lifts.begin(), target_power_lifts.end(),
            [](const mpz_class& lift) { return lift < 0; })) {
        throw std::invalid_argument(
            "Weber specialization has invalid target-power lifts");
    }
    if (mpz_fdiv_ui(auxiliary_prime.get_mpz_t(), 12U) != 11U) {
        throw std::invalid_argument(
            "Weber CM specialization requires p=11 mod 12");
    }
    if (orientation_relations.empty()) {
        throw std::invalid_argument(
            "Weber CM specialization has no orientation relations");
    }
    std::set<unsigned> orientation_levels;
    for (const SparseModularPolynomial& relation : orientation_relations) {
        validate_orientation_relation(relation, level, auxiliary_prime);
        if (!orientation_levels.insert(relation.level()).second) {
            throw std::invalid_argument(
                "Weber CM specialization repeats an orientation level");
        }
    }

    const Field field(auxiliary_prime);
    const std::size_t surface_count =
        surfaces.all_surface_invariants().size();
    if (surface_count >
            static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        surfaces.surface_curves().size() != surface_count ||
        surface_count < static_cast<std::size_t>(level) + 2U ||
        weber_surface_class_polynomial_mod_prime.field().modulus() !=
            auxiliary_prime ||
        weber_surface_class_polynomial_mod_prime.degree() !=
            static_cast<int>(surface_count) ||
        weber_surface_class_polynomial_mod_prime.leading_coefficient() != 1) {
        throw std::invalid_argument(
            "Weber surface class polynomial has invalid shape");
    }
    if (gcd(weber_surface_class_polynomial_mod_prime,
            weber_surface_class_polynomial_mod_prime.derivative())
            .degree() != 0) {
        throw std::invalid_argument(
            "Weber surface class polynomial is not square-free");
    }
    const std::vector<mpz_class> surface_weber_roots =
        linear_roots(weber_surface_class_polynomial_mod_prime);
    if (surface_weber_roots.size() != surface_count) {
        throw std::invalid_argument(
            "Weber surface class polynomial does not split completely");
    }

    std::set<mpz_class> expected_surface_j(
        surfaces.all_surface_invariants().begin(),
        surfaces.all_surface_invariants().end());
    InvariantMap surface_j_by_weber;
    InvariantMap surface_weber_by_j;
    for (const mpz_class& weber : surface_weber_roots) {
        const mpz_class j = j_from_weber_f(field, weber);
        const std::vector<mpz_class> lifts = weber_f_lifts(field, j);
        if (!expected_surface_j.contains(j) || lifts.size() != 2U ||
            lifts[0] != field.neg(lifts[1]) ||
            std::find(lifts.begin(), lifts.end(), weber) == lifts.end() ||
            !surface_j_by_weber.emplace(weber, j).second ||
            !surface_weber_by_j.emplace(j, weber).second) {
            throw std::runtime_error(
                "Weber class roots do not biject with the CM surface");
        }
    }
    if (surface_weber_by_j.size() != surface_count ||
        connected_components(field, surface_j_by_weber,
                             orientation_relations)
                .size() != 1U) {
        throw std::runtime_error(
            "small Weber relations do not connect the signed surface torsor");
    }

    std::map<mpz_class, std::size_t> floor_occurrences;
    for (const CmSurfaceCurve& surface : surfaces.surface_curves()) {
        if (surface.edges.size() != static_cast<std::size_t>(level) + 1U) {
            throw std::logic_error(
                "CM surface has an incomplete edge row");
        }
        for (const CmSurfaceEdge& edge : surface.edges) {
            const mpz_class neighbor = edge.isogeny.codomain.j_invariant();
            if (edge.codomain_on_surface) {
                if (!surface_weber_by_j.contains(neighbor)) {
                    throw std::logic_error(
                        "horizontal CM edge is absent from the Weber surface");
                }
            } else {
                ++floor_occurrences[neighbor];
            }
        }
    }
    const std::size_t descending_per_surface =
        static_cast<std::size_t>(level) + 1U -
        surfaces.horizontal_edges_per_surface();
    if (surface_count != 0U &&
        descending_per_surface >
            std::numeric_limits<std::size_t>::max() / surface_count) {
        throw std::overflow_error("Weber floor size overflows size_t");
    }
    const std::size_t floor_count = surface_count * descending_per_surface;
    if (floor_occurrences.size() != floor_count ||
        std::any_of(
            floor_occurrences.begin(), floor_occurrences.end(),
            [](const auto& entry) { return entry.second != 1U; })) {
        throw std::runtime_error(
            "CM descending edges do not biject with the floor torsor");
    }

    InvariantMap floor_j_by_weber;
    for (const auto& [j, occurrences] : floor_occurrences) {
        static_cast<void>(occurrences);
        const std::vector<mpz_class> lifts = weber_f_lifts(field, j);
        if (lifts.size() != 2U || lifts[0] != field.neg(lifts[1])) {
            throw std::runtime_error(
                "floor j-invariant does not have exactly the Weber +/- pair");
        }
        for (const mpz_class& weber : lifts) {
            if (!floor_j_by_weber.emplace(weber, j).second) {
                throw std::logic_error(
                    "distinct floor j-invariants share a Weber lift");
            }
        }
    }
    const std::vector<std::vector<mpz_class>> floor_components =
        connected_components(field, floor_j_by_weber,
                             orientation_relations);
    if (floor_components.size() != 2U) {
        throw std::runtime_error(
            "small Weber relations do not yield exactly two floor orientations");
    }
    const InvariantMap first_floor =
        component_map(floor_components[0], floor_j_by_weber, floor_count);
    const InvariantMap second_floor =
        component_map(floor_components[1], floor_j_by_weber, floor_count);
    for (const auto& [j, weber] : first_floor) {
        const auto found = second_floor.find(j);
        if (found == second_floor.end() ||
            found->second != field.neg(weber)) {
            throw std::runtime_error(
                "floor orientation components are not global negatives");
        }
    }

    std::vector<mpz_class> interpolation_points;
    interpolation_points.reserve(interpolation_count);
    for (std::size_t row = 0U; row < interpolation_count; ++row) {
        interpolation_points.push_back(
            surface_weber_by_j.at(
                surfaces.surface_curves()[row].j_invariant));
    }
    const std::vector<Poly> bases =
        lagrange_bases(field, interpolation_points);

    std::vector<std::vector<WeberInterpolationRow>> candidates;
    candidates.reserve(2U);
    for (const InvariantMap* floor_orientation :
         {&first_floor, &second_floor}) {
        std::vector<WeberInterpolationRow> rows;
        rows.reserve(interpolation_count);
        for (std::size_t row = 0U; row < interpolation_count; ++row) {
            const CmSurfaceCurve& surface =
                surfaces.surface_curves()[row];
            std::vector<mpz_class> neighbor_roots;
            neighbor_roots.reserve(static_cast<std::size_t>(level) + 1U);
            for (const CmSurfaceEdge& edge : surface.edges) {
                const mpz_class j = edge.isogeny.codomain.j_invariant();
                neighbor_roots.push_back(
                    edge.codomain_on_surface
                        ? surface_weber_by_j.at(j)
                        : floor_orientation->at(j));
            }
            rows.push_back(
                {surface_weber_by_j.at(surface.j_invariant),
                 neighbor_polynomial(field, neighbor_roots, level)});
        }
        candidates.push_back(std::move(rows));
    }

    const mpz_class required_sign = field.neg(1);
    std::size_t accepted = candidates.size();
    mpz_class accepted_coefficient = 0;
    for (std::size_t candidate = 0U; candidate < candidates.size();
         ++candidate) {
        const mpz_class coefficient = relative_sign_coefficient(
            field, level, candidates[candidate], bases);
        if (coefficient == required_sign) {
            if (accepted != candidates.size()) {
                throw std::runtime_error(
                    "both Weber surface/floor relative signs are ambiguous");
            }
            accepted = candidate;
            accepted_coefficient = coefficient;
        }
    }
    if (accepted == candidates.size()) {
        throw std::runtime_error(
            "neither Weber surface/floor relative sign is normalized");
    }

    return {
        specialize_rows(field, level, candidates[accepted], bases,
                        target_power_lifts),
        surface_count,
        floor_count,
        orientation_relations.size(),
        accepted_coefficient};
}

}  // namespace oneshotsea
