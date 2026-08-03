#include "oneshotsea/factor.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace oneshotsea {
namespace {

constexpr std::uint64_t kFactorSeed = UINT64_C(0x666163746f72637a);
constexpr std::uint64_t kFactorDomain = UINT64_C(0x73706c6974666163);
constexpr std::uint64_t kSplitAttempts = 256;

struct SquareFreeComponent {
    Poly polynomial;
    unsigned long multiplicity;
};

struct DegreeComponent {
    Poly polynomial;
    unsigned int factor_degree;
};

void require_probable_prime_field(const Poly& polynomial) {
    if (mpz_probab_prime_p(polynomial.field().modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "polynomial factorization requires probable-prime p");
    }
}

Poly exact_quotient(const Poly& numerator, const Poly& denominator) {
    const auto [quotient, remainder] = divmod(numerator, denominator);
    if (!remainder.is_zero()) {
        throw std::logic_error("polynomial factorization division was not exact");
    }
    return quotient;
}

unsigned long checked_multiplicity_product(unsigned long lhs,
                                             unsigned long rhs) {
    if (rhs != 0 && lhs > std::numeric_limits<unsigned long>::max() / rhs) {
        throw std::overflow_error("polynomial factor multiplicity overflow");
    }
    return lhs * rhs;
}

Poly prime_characteristic_root(const Poly& polynomial) {
    const mpz_class& characteristic_z = polynomial.field().modulus();
    if (characteristic_z > polynomial.degree()) {
        throw std::logic_error("nonconstant p-th power has degree below p");
    }
    const unsigned long characteristic = characteristic_z.get_ui();
    std::vector<mpz_class> coefficients(
        static_cast<std::size_t>(polynomial.degree()) / characteristic + 1U, 0);
    for (std::size_t index = 0; index < polynomial.coefficients().size(); ++index) {
        if (index % characteristic == 0U) {
            // In the prime field F_p, Frobenius fixes every coefficient.
            coefficients[index / characteristic] = polynomial.coefficient(index);
        } else if (polynomial.coefficient(index) != 0) {
            throw std::logic_error("derivative-zero polynomial was not a p-th power");
        }
    }
    return Poly(polynomial.field(), std::move(coefficients)).monic();
}

void square_free_decompose(const Poly& polynomial, unsigned long scale,
                           std::vector<SquareFreeComponent>& output) {
    if (polynomial.degree() <= 0) {
        return;
    }
    const Poly derivative = polynomial.derivative();
    if (derivative.is_zero()) {
        const unsigned long characteristic = polynomial.field().modulus().get_ui();
        square_free_decompose(
            prime_characteristic_root(polynomial),
            checked_multiplicity_product(scale, characteristic), output);
        return;
    }

    Poly repeated = gcd(polynomial, derivative);
    Poly remaining = exact_quotient(polynomial, repeated).monic();
    unsigned long multiplicity = 1;
    while (!remaining.is_one()) {
        const Poly shared = gcd(remaining, repeated);
        const Poly component = exact_quotient(remaining, shared).monic();
        if (!component.is_one()) {
            output.push_back({
                component,
                checked_multiplicity_product(scale, multiplicity),
            });
        }
        remaining = shared;
        repeated = exact_quotient(repeated, shared).monic();
        ++multiplicity;
        if (multiplicity > static_cast<unsigned long>(polynomial.degree()) + 1UL) {
            throw std::logic_error("square-free decomposition did not converge");
        }
    }

    if (!repeated.is_one()) {
        const unsigned long characteristic = polynomial.field().modulus().get_ui();
        square_free_decompose(
            prime_characteristic_root(repeated),
            checked_multiplicity_product(scale, characteristic), output);
    }
}

std::vector<DegreeComponent> distinct_degree_factorization(
    const Poly& square_free) {
    const Field& field = square_free.field();
    const Poly x = Poly::x(field);
    Poly remaining = square_free.monic();
    Poly frobenius = x;
    std::vector<DegreeComponent> output;

    unsigned int degree = 1;
    while (remaining.degree() >= 2 * static_cast<int>(degree)) {
        frobenius = powmod(frobenius, field.modulus(), remaining);
        const Poly component = gcd(remaining, sub(frobenius, x));
        if (!component.is_one()) {
            output.push_back({component.monic(), degree});
            remaining = exact_quotient(remaining, component).monic();
            if (remaining.is_one()) {
                break;
            }
            frobenius = mod(frobenius, remaining);
        }
        ++degree;
    }
    if (!remaining.is_one()) {
        output.push_back(
            {remaining.monic(), static_cast<unsigned int>(remaining.degree())});
    }
    return output;
}

std::uint64_t polynomial_fingerprint(const Poly& polynomial,
                                     unsigned int factor_degree) {
    std::uint64_t state = splitmix64(
        static_cast<std::uint64_t>(polynomial.degree()) ^
        (static_cast<std::uint64_t>(factor_degree) << 32U));
    for (std::size_t index = 0; index < polynomial.coefficients().size(); ++index) {
        const auto low = static_cast<std::uint64_t>(
            polynomial.coefficient(index).get_ui());
        const auto bits = static_cast<std::uint64_t>(mpz_sizeinbase(
            polynomial.coefficient(index).get_mpz_t(), 2));
        state = splitmix64(state ^ splitmix64(low) ^ splitmix64(bits + index));
    }
    return state;
}

Poly deterministic_candidate(const Poly& polynomial, unsigned int factor_degree,
                             std::uint64_t attempt) {
    const std::uint64_t fingerprint =
        polynomial_fingerprint(polynomial, factor_degree);
    std::vector<mpz_class> coefficients(
        static_cast<std::size_t>(polynomial.degree()), 0);
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        coefficients[index] = deterministic_residue(
            polynomial.field(), kFactorSeed ^ fingerprint,
            attempt * coefficients.size() + index,
            kFactorDomain ^ fingerprint ^ static_cast<std::uint64_t>(index));
    }
    return Poly(polynomial.field(), std::move(coefficients));
}

void equal_degree_factorization(const Poly& polynomial,
                                unsigned int factor_degree,
                                std::vector<Poly>& output) {
    if (polynomial.degree() == static_cast<int>(factor_degree)) {
        output.push_back(polynomial.monic());
        return;
    }
    if (factor_degree == 0U ||
        polynomial.degree() % static_cast<int>(factor_degree) != 0) {
        throw std::logic_error("invalid equal-degree factorization component");
    }

    mpz_class extension_size;
    mpz_pow_ui(extension_size.get_mpz_t(),
               polynomial.field().modulus().get_mpz_t(), factor_degree);
    const mpz_class exponent = (extension_size - 1) / 2;
    const Poly one = Poly::constant(polynomial.field(), 1);
    for (std::uint64_t attempt = 0; attempt < kSplitAttempts; ++attempt) {
        const Poly candidate =
            deterministic_candidate(polynomial, factor_degree, attempt);
        const Poly character = powmod(candidate, exponent, polynomial);
        const Poly divisor = gcd(polynomial, sub(character, one));
        if (divisor.degree() <= 0 || divisor.degree() >= polynomial.degree()) {
            continue;
        }
        const Poly cofactor = exact_quotient(polynomial, divisor).monic();
        equal_degree_factorization(divisor.monic(), factor_degree, output);
        equal_degree_factorization(cofactor, factor_degree, output);
        return;
    }
    throw std::runtime_error(
        "deterministic Cantor-Zassenhaus split attempt limit reached");
}

bool is_irreducible(const Poly& polynomial) {
    if (polynomial.degree() <= 0) {
        return false;
    }
    const Poly monic = polynomial.monic();
    const Poly x_mod = mod(Poly::x(monic.field()), monic);
    const PolyModContext modular(monic);
    const Poly frobenius_map = modular.pow(
        x_mod, monic.field().modulus());
    Poly frobenius = x_mod;
    for (int iteration = 1; iteration <= monic.degree(); ++iteration) {
        frobenius = modular.compose(frobenius, frobenius_map);
        if (iteration <= monic.degree() / 2 &&
            !gcd(monic, sub(frobenius, x_mod)).is_one()) {
            return false;
        }
    }
    return equal(frobenius, x_mod);
}

bool polynomial_less(const Poly& lhs, const Poly& rhs) {
    if (lhs.degree() != rhs.degree()) {
        return lhs.degree() < rhs.degree();
    }
    return std::lexicographical_compare(
        lhs.coefficients().begin(), lhs.coefficients().end(),
        rhs.coefficients().begin(), rhs.coefficients().end());
}

std::vector<unsigned int> distinct_prime_divisors(unsigned int value) {
    std::vector<unsigned int> divisors;
    for (unsigned int divisor = 2U;
         divisor <= value / divisor; ++divisor) {
        if (value % divisor != 0U) {
            continue;
        }
        divisors.push_back(divisor);
        do {
            value /= divisor;
        } while (value % divisor == 0U);
    }
    if (value > 1U) {
        divisors.push_back(value);
    }
    return divisors;
}

void validate_factorization(const Poly& input,
                            const std::vector<IrreducibleFactor>& factors) {
    Poly reconstructed = Poly::constant(input.field(), 1);
    for (std::size_t index = 0; index < factors.size(); ++index) {
        const IrreducibleFactor& factor = factors[index];
        if (factor.multiplicity == 0 ||
            factor.polynomial.leading_coefficient() != 1 ||
            !is_irreducible(factor.polynomial)) {
            throw std::logic_error("invalid irreducible factor produced");
        }
        if (index > 0 &&
            !polynomial_less(factors[index - 1].polynomial, factor.polynomial)) {
            throw std::logic_error("duplicate or unsorted polynomial factor");
        }
        for (unsigned long copy = 0; copy < factor.multiplicity; ++copy) {
            reconstructed = mul(reconstructed, factor.polynomial);
        }
    }
    if (!equal(reconstructed, input.monic())) {
        throw std::logic_error("polynomial factorization reconstruction failed");
    }
}

}  // namespace

std::vector<IrreducibleFactor> factor_polynomial(const Poly& polynomial) {
    if (polynomial.is_zero()) {
        throw std::invalid_argument("zero polynomial has no finite factorization");
    }
    require_probable_prime_field(polynomial);
    const Poly monic = polynomial.monic();
    if (monic.degree() == 0) {
        return {};
    }

    std::vector<SquareFreeComponent> square_free_components;
    square_free_decompose(monic, 1, square_free_components);

    std::vector<IrreducibleFactor> factors;
    for (const SquareFreeComponent& square_free : square_free_components) {
        const auto degree_components =
            distinct_degree_factorization(square_free.polynomial);
        for (const DegreeComponent& component : degree_components) {
            std::vector<Poly> irreducibles;
            equal_degree_factorization(
                component.polynomial, component.factor_degree, irreducibles);
            for (Poly& irreducible : irreducibles) {
                factors.push_back(
                    {irreducible.monic(), square_free.multiplicity});
            }
        }
    }

    std::sort(factors.begin(), factors.end(),
              [](const IrreducibleFactor& lhs, const IrreducibleFactor& rhs) {
                  return polynomial_less(lhs.polynomial, rhs.polynomial);
              });
    validate_factorization(monic, factors);
    return factors;
}

std::optional<unsigned int> uniform_irreducible_factor_degree(
    const Poly& polynomial) {
    if (polynomial.is_zero()) {
        throw std::invalid_argument(
            "zero polynomial has no uniform factor degree");
    }
    require_probable_prime_field(polynomial);
    const Poly monic = polynomial.monic();
    if (monic.degree() <= 0) {
        return std::nullopt;
    }
    if (!gcd(monic, monic.derivative()).is_one()) {
        return std::nullopt;
    }

    const Poly x_mod = mod(Poly::x(monic.field()), monic);
    const PolyModContext modular(monic);
    const Poly frobenius_map = modular.pow(
        x_mod, monic.field().modulus());
    const unsigned int polynomial_degree =
        static_cast<unsigned int>(monic.degree());
    std::vector<std::optional<Poly>> divisor_powers(
        static_cast<std::size_t>(polynomial_degree) + 1U);
    Poly frobenius = x_mod;
    unsigned int common_multiple = 0U;
    for (unsigned int degree = 1U; degree <= polynomial_degree; ++degree) {
        frobenius = modular.compose(frobenius, frobenius_map);
        if (polynomial_degree % degree == 0U) {
            divisor_powers[degree] = frobenius;
        }
        if (equal(frobenius, x_mod)) {
            common_multiple = degree;
            break;
        }
    }
    if (common_multiple == 0U ||
        polynomial_degree % common_multiple != 0U) {
        return std::nullopt;
    }

    // X^(p^r)-X vanishing modulo f proves every irreducible factor degree
    // divides r.  For each prime q|r, excluding factors dividing r/q proves
    // that no proper divisor of r occurs.  Square-freeness above then makes
    // the common factor-degree claim exact without constructing any factor.
    for (const unsigned int prime :
         distinct_prime_divisors(common_multiple)) {
        const unsigned int proper = common_multiple / prime;
        if (!divisor_powers[proper].has_value()) {
            throw std::logic_error(
                "uniform factor-degree proof lost a Frobenius divisor");
        }
        if (!gcd(monic, sub(*divisor_powers[proper], x_mod)).is_one()) {
            return std::nullopt;
        }
    }
    return common_multiple;
}

}  // namespace oneshotsea
