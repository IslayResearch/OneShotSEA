#include "oneshotsea/isogeny.hpp"

#include "oneshotsea/elkies.hpp"
#include "oneshotsea/torsion.hpp"
#include "oneshotsea/weber.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oneshotsea {
namespace {

bool is_odd_prime(std::uint64_t value) {
    if (value < 3U || (value & 1U) == 0U) {
        return false;
    }
    const mpz_class integer(std::to_string(value));
    return mpz_probab_prime_p(integer.get_mpz_t(), 25) != 0;
}

mpz_class size_as_field_element(const Field& field, std::size_t value) {
    return field.normalize(mpz_class(std::to_string(value)));
}

mpz_class convolution_coefficient(const Field& field,
                                  const std::vector<mpz_class>& lhs,
                                  const std::vector<mpz_class>& rhs,
                                  std::size_t degree) {
    if (lhs.empty() || rhs.empty()) {
        return 0;
    }
    mpz_class result = 0;
    const std::size_t first =
        degree < rhs.size() ? 0U : degree - rhs.size() + 1U;
    const std::size_t last = std::min(degree, lhs.size() - 1U);
    for (std::size_t index = first; index <= last; ++index) {
        const std::size_t other = degree - index;
        if (other < rhs.size()) {
            result = field.add(result, field.mul(lhs[index], rhs[other]));
        }
    }
    return result;
}

std::vector<mpz_class> solve_isogeny_series(
    const Curve& source, const Curve& codomain, std::size_t precision) {
    const Field& field = source.field();
    std::vector<mpz_class> series(precision, 0);
    std::vector<mpz_class> square(precision, 0);
    std::vector<mpz_class> fourth(precision, 0);
    std::vector<mpz_class> sixth(precision, 0);
    std::vector<mpz_class> derivative_square(precision, 0);
    series[1] = 1;
    derivative_square[0] = 1;

    for (std::size_t degree = 1; degree + 1U < precision; ++degree) {
        square[degree] =
            convolution_coefficient(field, series, series, degree);
        fourth[degree] =
            convolution_coefficient(field, square, square, degree);
        sixth[degree] =
            convolution_coefficient(field, fourth, square, degree);

        mpz_class known_derivative_square = 0;
        for (std::size_t index = 1; index < degree; ++index) {
            const mpz_class left = field.mul(
                size_as_field_element(field, index + 1U), series[index + 1U]);
            const std::size_t other = degree - index;
            const mpz_class right = field.mul(
                size_as_field_element(field, other + 1U), series[other + 1U]);
            known_derivative_square = field.add(
                known_derivative_square, field.mul(left, right));
        }

        mpz_class left_known = known_derivative_square;
        if (degree >= 4U) {
            left_known = field.add(
                left_known,
                field.mul(source.a(), derivative_square[degree - 4U]));
        }
        if (degree >= 6U) {
            left_known = field.add(
                left_known,
                field.mul(source.b(), derivative_square[degree - 6U]));
        }
        const mpz_class right = field.add(
            field.mul(codomain.a(), fourth[degree]),
            field.mul(codomain.b(), sixth[degree]));
        const mpz_class denominator = size_as_field_element(
            field, 2U * (degree + 1U));
        series[degree + 1U] =
            field.divide(field.sub(right, left_known), denominator);
        derivative_square[degree] = field.add(
            known_derivative_square,
            field.mul(denominator, series[degree + 1U]));
    }
    return series;
}

std::vector<mpz_class> reciprocal_series(
    const Field& field, const std::vector<mpz_class>& input) {
    if (input.empty() || input[0] == 0) {
        throw std::invalid_argument("power series is not invertible");
    }
    std::vector<mpz_class> result(input.size(), 0);
    const mpz_class inverse_constant = field.inverse(input[0]);
    result[0] = inverse_constant;
    for (std::size_t degree = 1; degree < input.size(); ++degree) {
        mpz_class sum = 0;
        for (std::size_t index = 1; index <= degree; ++index) {
            sum = field.add(
                sum, field.mul(input[index], result[degree - index]));
        }
        result[degree] = field.neg(field.mul(inverse_constant, sum));
    }
    return result;
}

std::pair<Poly, Poly> pade_reconstruct(
    const Field& field, const std::vector<mpz_class>& series,
    std::size_t numerator_degree, std::size_t denominator_degree) {
    const std::size_t required =
        numerator_degree + denominator_degree + 1U;
    if (series.size() < required) {
        throw std::invalid_argument("insufficient series precision for Pade reconstruction");
    }
    std::vector<mpz_class> modulus_coefficients(required + 1U, 0);
    modulus_coefficients.back() = 1;
    Poly old_remainder(field, std::move(modulus_coefficients));
    Poly remainder(field, std::vector<mpz_class>(
                              series.begin(), series.begin() +
                                                  static_cast<std::ptrdiff_t>(required)));
    Poly old_cofactor = Poly::constant(field, 0);
    Poly cofactor = Poly::constant(field, 1);
    while (remainder.degree() > static_cast<int>(numerator_degree)) {
        const auto [quotient, next_remainder] =
            divmod(old_remainder, remainder);
        Poly next_cofactor = sub(old_cofactor, mul(quotient, cofactor));
        old_remainder = std::move(remainder);
        remainder = next_remainder;
        old_cofactor = std::move(cofactor);
        cofactor = std::move(next_cofactor);
    }
    if (cofactor.is_zero() || cofactor.coefficient(0) == 0 ||
        cofactor.degree() > static_cast<int>(denominator_degree)) {
        throw std::runtime_error("Pade reconstruction has invalid denominator");
    }
    const mpz_class scale = field.inverse(cofactor.coefficient(0));
    Poly numerator = scalar_mul(remainder, scale);
    Poly denominator = scalar_mul(cofactor, scale);
    if (numerator.degree() > static_cast<int>(numerator_degree)) {
        throw std::runtime_error("Pade reconstruction has invalid numerator");
    }

    for (std::size_t degree = 0; degree < series.size(); ++degree) {
        mpz_class product = 0;
        const std::size_t maximum = std::min(
            degree, static_cast<std::size_t>(std::max(0, denominator.degree())));
        for (std::size_t index = 0; index <= maximum; ++index) {
            product = field.add(
                product,
                field.mul(denominator.coefficient(index),
                          series[degree - index]));
        }
        if (product != numerator.coefficient(degree)) {
            throw std::runtime_error(
                "Pade reconstruction failed extra-precision validation");
        }
    }
    return {std::move(numerator), std::move(denominator)};
}

Poly reverse_polynomial(const Poly& polynomial, std::size_t degree) {
    if (polynomial.degree() != static_cast<int>(degree)) {
        throw std::invalid_argument("polynomial has unexpected reversal degree");
    }
    std::vector<mpz_class> coefficients(degree + 1U, 0);
    for (std::size_t index = 0; index <= degree; ++index) {
        coefficients[index] = polynomial.coefficient(degree - index);
    }
    return Poly(polynomial.field(), std::move(coefficients));
}

Poly square_root_reversed_denominator(const Poly& denominator,
                                      std::size_t kernel_degree) {
    const Field& field = denominator.field();
    if (denominator.degree() != static_cast<int>(2U * kernel_degree) ||
        denominator.coefficient(0) != 1) {
        throw std::runtime_error("isogeny denominator has invalid degree or normalization");
    }
    std::vector<mpz_class> reversed_root(kernel_degree + 1U, 0);
    reversed_root[0] = 1;
    for (std::size_t degree = 1; degree <= kernel_degree; ++degree) {
        mpz_class known = 0;
        for (std::size_t index = 1; index < degree; ++index) {
            known = field.add(
                known,
                field.mul(reversed_root[index], reversed_root[degree - index]));
        }
        reversed_root[degree] =
            field.divide(field.sub(denominator.coefficient(degree), known), 2);
    }
    const Poly root(field, reversed_root);
    if (!equal(mul(root, root), denominator)) {
        throw std::runtime_error("isogeny denominator is not a polynomial square");
    }
    std::reverse(reversed_root.begin(), reversed_root.end());
    return Poly(field, std::move(reversed_root));
}

void validate_rational_isogeny(const Curve& source, const Curve& codomain,
                               const Poly& numerator, const Poly& denominator) {
    const Field& field = source.field();
    const Poly x = Poly::x(field);
    const Poly source_rhs = add(
        add(mul(mul(x, x), x), scalar_mul(x, source.a())),
        Poly::constant(field, source.b()));
    const Poly derivative_numerator = sub(
        mul(numerator.derivative(), denominator),
        mul(numerator, denominator.derivative()));
    const Poly left =
        mul(source_rhs, mul(derivative_numerator, derivative_numerator));
    const Poly denominator_squared = mul(denominator, denominator);
    const Poly codomain_rhs_numerator = add(
        add(mul(mul(numerator, numerator), numerator),
            scalar_mul(mul(numerator, denominator_squared), codomain.a())),
        scalar_mul(mul(denominator_squared, denominator), codomain.b()));
    const Poly right = mul(denominator, codomain_rhs_numerator);
    if (!equal(left, right)) {
        throw std::runtime_error("reconstructed rational map fails the isogeny equation");
    }
}

Curve codomain_from_neighbor_derivative(
    const Curve& source, std::uint64_t ell, const mpz_class& neighbor_j,
    const mpz_class& neighbor_derivative) {
    const Field& field = source.field();
    const mpz_class normalized_neighbor = field.normalize(neighbor_j);
    if (normalized_neighbor == 0 ||
        normalized_neighbor == field.normalize(1728) ||
        neighbor_derivative == 0) {
        throw std::domain_error(
            "exceptional neighbor in normalized codomain formula");
    }
    const mpz_class mu = field.divide(neighbor_derivative, normalized_neighbor);
    const mpz_class kappa = field.divide(
        neighbor_derivative, field.sub(1728, normalized_neighbor));
    const mpz_class ell_element = field.normalize(mpz_class(std::to_string(ell)));
    const mpz_class ell_squared = field.square(ell_element);
    const mpz_class ell_fourth = field.square(ell_squared);
    const mpz_class ell_sixth = field.mul(ell_fourth, ell_squared);
    const mpz_class codomain_a = field.divide(
        field.mul(ell_fourth, field.mul(mu, kappa)), 48);
    const mpz_class codomain_b = field.divide(
        field.mul(ell_sixth, field.mul(field.square(mu), kappa)), 864);
    Curve result(field, codomain_a, codomain_b);
    if (result.is_singular() || result.j_invariant() != normalized_neighbor) {
        throw std::runtime_error(
            "normalized codomain does not have the requested j-invariant");
    }
    return result;
}

}  // namespace

Curve normalized_codomain_from_classical_modpoly(
    const Curve& source, const SparseModularPolynomial& modular_polynomial,
    const mpz_class& neighbor_j) {
    if (source.is_singular()) {
        throw std::invalid_argument("cannot construct an isogeny from a singular curve");
    }
    const std::uint64_t ell = modular_polynomial.level();
    if (!is_odd_prime(ell)) {
        throw std::invalid_argument("modular-polynomial level must be an odd prime");
    }
    const Field& field = source.field();
    if (mpz_probab_prime_p(field.modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "normalized codomain requires prime characteristic");
    }
    if (mpz_cmp_ui(field.modulus().get_mpz_t(), ell) == 0) {
        throw std::invalid_argument("isogeny degree must differ from the characteristic");
    }
    const mpz_class j = source.j_invariant();
    const mpz_class normalized_neighbor = field.normalize(neighbor_j);
    if (source.a() == 0 || source.b() == 0 || j == 0 ||
        j == field.normalize(1728) || normalized_neighbor == 0 ||
        normalized_neighbor == field.normalize(1728)) {
        throw std::domain_error("exceptional j-invariant in normalized codomain formula");
    }
    const BivariateEvaluation evaluation =
        modular_polynomial.evaluate_with_derivatives(field, j, normalized_neighbor);
    if (evaluation.value != 0) {
        throw std::invalid_argument("neighbor is not a root of the modular polynomial");
    }
    if (evaluation.x_derivative == 0 || evaluation.y_derivative == 0) {
        throw std::domain_error("zero modular derivative in normalized codomain formula");
    }

    const mpz_class j_derivative = field.divide(
        field.mul(field.mul(18, source.b()), j), source.a());
    const mpz_class neighbor_derivative = field.neg(field.divide(
        field.mul(evaluation.x_derivative, j_derivative),
        field.mul(mpz_class(std::to_string(ell)), evaluation.y_derivative)));
    return codomain_from_neighbor_derivative(
        source, ell, normalized_neighbor, neighbor_derivative);
}

Curve normalized_codomain_from_weber_modpoly(
    const Curve& source, const SparseModularPolynomial& weber_modular_polynomial,
    const mpz_class& source_weber_f, const mpz_class& neighbor_weber_f) {
    if (source.is_singular()) {
        throw std::invalid_argument("cannot construct an isogeny from a singular curve");
    }
    const std::uint64_t ell = weber_modular_polynomial.level();
    if (!is_odd_prime(ell)) {
        throw std::invalid_argument("Weber modular-polynomial level must be an odd prime");
    }
    const Field& field = source.field();
    if (mpz_probab_prime_p(field.modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "normalized codomain requires prime characteristic");
    }
    if (mpz_cmp_ui(field.modulus().get_mpz_t(), ell) == 0) {
        throw std::invalid_argument("isogeny degree must differ from the characteristic");
    }
    const mpz_class j = source.j_invariant();
    const mpz_class normalized_source_f = field.normalize(source_weber_f);
    const mpz_class normalized_neighbor_f = field.normalize(neighbor_weber_f);
    if (source.a() == 0 || source.b() == 0 || j == 0 ||
        j == field.normalize(1728) ||
        j_from_weber_f(field, normalized_source_f) != j) {
        throw std::domain_error("exceptional or invalid source Weber-f lift");
    }
    const mpz_class neighbor_j =
        j_from_weber_f(field, normalized_neighbor_f);
    const BivariateEvaluation evaluation =
        weber_modular_polynomial.evaluate_with_derivatives(
            field, normalized_source_f, normalized_neighbor_f);
    if (evaluation.value != 0) {
        throw std::invalid_argument(
            "Weber neighbor is not a root of the modular polynomial");
    }
    const mpz_class source_f_derivative =
        j_derivative_from_weber_f(field, normalized_source_f);
    const mpz_class neighbor_f_derivative =
        j_derivative_from_weber_f(field, normalized_neighbor_f);
    if (evaluation.x_derivative == 0 || evaluation.y_derivative == 0 ||
        source_f_derivative == 0 || neighbor_f_derivative == 0) {
        throw std::domain_error(
            "zero Weber or j-map derivative in normalized codomain formula");
    }
    const mpz_class j_derivative = field.divide(
        field.mul(field.mul(18, source.b()), j), source.a());
    const mpz_class neighbor_derivative = field.neg(field.divide(
        field.mul(
            field.mul(evaluation.x_derivative, neighbor_f_derivative),
            j_derivative),
        field.mul(
            field.mul(mpz_class(std::to_string(ell)),
                      evaluation.y_derivative),
            source_f_derivative)));
    return codomain_from_neighbor_derivative(
        source, ell, neighbor_j, neighbor_derivative);
}

BmssIsogenyResult bmss_isogeny_reference(
    const Curve& source, const Curve& normalized_codomain, std::uint64_t ell) {
    if (source.field().modulus() != normalized_codomain.field().modulus()) {
        throw std::invalid_argument("isogeny curves use different fields");
    }
    if (source.is_singular() || normalized_codomain.is_singular()) {
        throw std::invalid_argument("BMSS requires nonsingular curves");
    }
    if (!is_odd_prime(ell)) {
        throw std::invalid_argument("BMSS degree must be an odd prime");
    }
    if (mpz_probab_prime_p(source.field().modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("BMSS requires prime characteristic");
    }
    if (ell > 31U) {
        throw std::invalid_argument(
            "reference BMSS validation is capped at ell=31");
    }
    if (ell > (std::numeric_limits<std::size_t>::max() - 4U) / 4U) {
        throw std::overflow_error("BMSS precision does not fit size_t");
    }
    const std::size_t ell_size = static_cast<std::size_t>(ell);
    const std::size_t precision = 4U * ell_size + 4U;
    const mpz_class largest_denominator(
        std::to_string(2U * (precision - 1U)));
    if (source.field().modulus() <= largest_denominator) {
        throw std::invalid_argument(
            "characteristic is too small for BMSS power-series divisions");
    }

    const Field& field = source.field();
    const std::vector<mpz_class> isogeny_series =
        solve_isogeny_series(source, normalized_codomain, precision);
    if (isogeny_series[5] !=
            field.divide(field.sub(normalized_codomain.a(), source.a()), 10) ||
        isogeny_series[7] !=
            field.divide(field.sub(normalized_codomain.b(), source.b()), 14)) {
        throw std::logic_error("BMSS power series has incorrect initial coefficients");
    }
    for (std::size_t degree = 0; degree < isogeny_series.size(); degree += 2U) {
        if (isogeny_series[degree] != 0) {
            throw std::runtime_error("BMSS power series lost odd parity");
        }
    }

    std::vector<mpz_class> t_series;
    t_series.reserve(2U * ell_size + 2U);
    for (std::size_t degree = 1; degree < isogeny_series.size(); degree += 2U) {
        t_series.push_back(isogeny_series[degree]);
    }
    std::vector<mpz_class> t_squared(t_series.size(), 0);
    for (std::size_t degree = 0; degree < t_squared.size(); ++degree) {
        t_squared[degree] =
            convolution_coefficient(field, t_series, t_series, degree);
    }
    const std::vector<mpz_class> u_series =
        reciprocal_series(field, t_squared);
    auto [reversed_numerator, reversed_denominator] = pade_reconstruct(
        field, u_series, ell_size, ell_size - 1U);
    if (reversed_numerator.degree() != static_cast<int>(ell_size) ||
        reversed_denominator.degree() != static_cast<int>(ell_size - 1U)) {
        throw std::runtime_error("BMSS reconstruction returned deficient degrees");
    }

    Poly numerator = reverse_polynomial(reversed_numerator, ell_size);
    Poly denominator = reverse_polynomial(reversed_denominator, ell_size - 1U);
    const std::size_t kernel_degree = (ell_size - 1U) / 2U;
    Poly kernel = square_root_reversed_denominator(
        reversed_denominator, kernel_degree);
    if (!equal(mul(kernel, kernel), denominator) ||
        kernel.degree() != static_cast<int>(kernel_degree) ||
        kernel.leading_coefficient() != 1 ||
        gcd(kernel, kernel.derivative()).degree() != 0) {
        throw std::runtime_error("BMSS kernel failed degree or square-free validation");
    }

    const Poly psi_ell = division_polynomial_reference(source, ell);
    if (!divmod(psi_ell, kernel).second.is_zero()) {
        throw std::runtime_error("BMSS kernel does not divide the division polynomial");
    }
    const Curve velu_codomain = velu_codomain_reference(source, kernel, ell);
    if (velu_codomain.a() != normalized_codomain.a() ||
        velu_codomain.b() != normalized_codomain.b()) {
        throw std::runtime_error("BMSS kernel yields the wrong normalized codomain");
    }
    validate_rational_isogeny(
        source, normalized_codomain, numerator, denominator);
    return {std::move(kernel), std::move(numerator), std::move(denominator)};
}

}  // namespace oneshotsea
