#include "oneshotsea/direct_modpoly.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>

namespace oneshotsea {
namespace {

bool is_probable_prime(const mpz_class& value) {
    return value >= 2 &&
        mpz_probab_prime_p(value.get_mpz_t(), 25) != 0;
}

bool is_proven_auxiliary_prime(const mpz_class& value) {
    std::uint64_t encoded = 0U;
    return export_u64(value, encoded) && is_prime_u64(encoded);
}

mpz_class encode_u64(std::uint64_t value) {
    return mpz_class(std::to_string(value));
}

void validate_odd_prime_level(unsigned level) {
    if (level < 3U || (level & 1U) == 0U || !is_prime_u64(level)) {
        throw std::invalid_argument("level must be an odd prime");
    }
}

bool is_squarefree(std::uint64_t value) {
    if (value == 0U) {
        return false;
    }
    for (std::uint64_t prime = 2U; prime <= value / prime; ++prime) {
        const std::uint64_t square = prime * prime;
        if (value % square == 0U) {
            return false;
        }
    }
    return true;
}

bool is_fundamental_discriminant(const mpz_class& discriminant) {
    if (discriminant >= 0) {
        return false;
    }
    const mpz_class absolute = -discriminant;
    if (!mpz_fits_ulong_p(absolute.get_mpz_t())) {
        return false;
    }
    mpz_class squarefree_part;
    const unsigned long residue = mpz_fdiv_ui(discriminant.get_mpz_t(), 4U);
    if (residue == 1U) {
        squarefree_part = -discriminant;
    } else if (residue == 0U) {
        const mpz_class quotient = discriminant / 4;
        const unsigned long quotient_residue =
            mpz_fdiv_ui(quotient.get_mpz_t(), 4U);
        if (quotient_residue != 2U && quotient_residue != 3U) {
            return false;
        }
        squarefree_part = -quotient;
    } else {
        return false;
    }
    if (!mpz_fits_ulong_p(squarefree_part.get_mpz_t())) {
        return false;
    }
    return is_squarefree(
        static_cast<std::uint64_t>(squarefree_part.get_ui()));
}

bool conductor_prime_factors_are_bounded(std::uint64_t conductor,
                                         std::uint64_t bound) {
    std::uint64_t remaining = conductor;
    for (std::uint64_t prime = 2U; prime <= remaining / prime; ++prime) {
        if (remaining % prime != 0U) {
            continue;
        }
        if (prime > bound) {
            return false;
        }
        do {
            remaining /= prime;
        } while (remaining % prime == 0U);
    }
    return remaining == 1U || remaining <= bound;
}

std::vector<std::uint64_t> distinct_prime_factors(std::uint64_t value) {
    std::vector<std::uint64_t> output;
    std::uint64_t remaining = value;
    for (std::uint64_t candidate = 2U;
         candidate <= remaining / candidate; ++candidate) {
        if (remaining % candidate != 0U) {
            continue;
        }
        output.push_back(candidate);
        do {
            remaining /= candidate;
        } while (remaining % candidate == 0U);
    }
    if (remaining > 1U) {
        output.push_back(remaining);
    }
    return output;
}

std::uint64_t order_class_number_from_conductor_formula(
    const mpz_class& fundamental_discriminant,
    std::uint64_t fundamental_class_number, std::uint64_t conductor) {
    mpz_class result =
        encode_u64(fundamental_class_number) * encode_u64(conductor);
    for (const std::uint64_t prime : distinct_prime_factors(conductor)) {
        const mpz_class encoded_prime = encode_u64(prime);
        const int symbol = mpz_kronecker(
            fundamental_discriminant.get_mpz_t(),
            encoded_prime.get_mpz_t());
        result *= encoded_prime - symbol;
        if (!mpz_divisible_p(result.get_mpz_t(),
                             encoded_prime.get_mpz_t())) {
            throw std::logic_error(
                "ring-class number formula produced a noninteger");
        }
        result /= encoded_prime;
    }
    std::uint64_t encoded_result = 0U;
    if (!export_u64(result, encoded_result)) {
        throw std::overflow_error(
            "ring-class number exceeds the discovery reference range");
    }
    return encoded_result;
}

std::uint64_t ceil_sqrt_quotient(const mpz_class& numerator,
                                 std::uint64_t denominator) {
    const mpz_class encoded_denominator = encode_u64(denominator);
    mpz_class quotient = numerator / encoded_denominator;
    if (numerator % encoded_denominator != 0) {
        ++quotient;
    }
    mpz_class root;
    mpz_sqrt(root.get_mpz_t(), quotient.get_mpz_t());
    if (root * root < quotient) {
        ++root;
    }
    std::uint64_t output = 0U;
    if (!export_u64(root, output)) {
        throw std::overflow_error(
            "minimum suitable-order conductor exceeds 64 bits");
    }
    return output;
}

std::uint64_t floor_sqrt_quotient(const mpz_class& numerator,
                                  std::uint64_t denominator) {
    const mpz_class quotient = numerator / encode_u64(denominator);
    mpz_class root;
    mpz_sqrt(root.get_mpz_t(), quotient.get_mpz_t());
    std::uint64_t output = 0U;
    if (!export_u64(root, output)) {
        throw std::overflow_error(
            "maximum suitable-order conductor exceeds 64 bits");
    }
    return output;
}

std::uint64_t class_number_with_limit(const mpz_class& discriminant,
                                      std::uint64_t maximum) {
    if (discriminant >= 0 ||
        (mpz_fdiv_ui(discriminant.get_mpz_t(), 4U) != 0U &&
         mpz_fdiv_ui(discriminant.get_mpz_t(), 4U) != 1U)) {
        throw std::invalid_argument(
            "class number requires a negative quadratic discriminant");
    }
    const mpz_class absolute = -discriminant;
    if (!mpz_fits_ulong_p(absolute.get_mpz_t())) {
        throw std::overflow_error(
            "class-number reference discriminant exceeds unsigned long");
    }
    const mpz_class root_input = absolute / 3U;
    mpz_class root;
    mpz_sqrt(root.get_mpz_t(), root_input.get_mpz_t());
    const std::uint64_t root_limit =
        static_cast<std::uint64_t>(root.get_ui());

    std::uint64_t count = 0U;
    for (std::uint64_t a = 1U; a <= root_limit; ++a) {
        const std::int64_t signed_a = static_cast<std::int64_t>(a);
        for (std::int64_t b = -signed_a; b <= signed_a; ++b) {
            const std::uint64_t absolute_b =
                static_cast<std::uint64_t>(b < 0 ? -b : b);
            if (absolute_b % 2U !=
                mpz_fdiv_ui(discriminant.get_mpz_t(), 2U)) {
                continue;
            }
            const mpz_class encoded_b(static_cast<long>(b));
            const mpz_class numerator =
                encoded_b * encoded_b - discriminant;
            const mpz_class denominator =
                mpz_class(4U) * static_cast<unsigned long>(a);
            if (!mpz_divisible_p(numerator.get_mpz_t(),
                                 denominator.get_mpz_t())) {
                continue;
            }
            const mpz_class c_integer = numerator / denominator;
            if (!mpz_fits_ulong_p(c_integer.get_mpz_t())) {
                throw std::overflow_error(
                    "class-number form coefficient exceeds unsigned long");
            }
            const std::uint64_t c =
                static_cast<std::uint64_t>(c_integer.get_ui());
            if (a > c ||
                ((static_cast<std::uint64_t>(b < 0 ? -b : b) == a ||
                  a == c) &&
                 b < 0)) {
                continue;
            }
            if (std::gcd(std::gcd(a, absolute_b), c) != 1U) {
                continue;
            }
            if (count == maximum) {
                return maximum + 1U;
            }
            ++count;
        }
    }
    return count;
}

void validate_residue_coefficients(
    const std::vector<mpz_class>& coefficients,
    const mpz_class& prime, std::size_t expected_size,
    const char* label) {
    if (coefficients.size() != expected_size) {
        throw std::invalid_argument(
            std::string(label) + " has the wrong coefficient count");
    }
    for (const mpz_class& coefficient : coefficients) {
        if (coefficient < 0 || coefficient >= prime) {
            throw std::invalid_argument(
                std::string(label) + " contains a noncanonical residue");
        }
    }
}

}  // namespace

SutherlandSuitableOrder::SutherlandSuitableOrder(
    unsigned level, mpz_class fundamental_discriminant,
    mpz_class conductor, mpz_class discriminant,
    std::uint64_t class_number)
    : level_(level),
      fundamental_discriminant_(std::move(fundamental_discriminant)),
      conductor_(std::move(conductor)),
      discriminant_(std::move(discriminant)),
      class_number_(class_number) {}

bool SutherlandSuitableOrder::weber_f_order_congruences_hold() const {
    return mpz_fdiv_ui(discriminant_.get_mpz_t(), 8U) == 1U &&
        mpz_fdiv_ui(discriminant_.get_mpz_t(), 3U) != 0U;
}

std::uint64_t negative_order_class_number(const mpz_class& discriminant) {
    return class_number_with_limit(
        discriminant, std::numeric_limits<std::uint64_t>::max() - 1U);
}

SutherlandSuitableOrder validate_sutherland_suitable_order(
    unsigned level, const mpz_class& fundamental_discriminant,
    const mpz_class& conductor, std::uint64_t c1_numerator,
    std::uint64_t c1_denominator, std::uint64_t c2) {
    validate_odd_prime_level(level);
    if (c1_numerator <= c1_denominator || c1_denominator == 0U || c2 <= 1U) {
        throw std::invalid_argument("invalid suitable-order constants");
    }
    if (!is_fundamental_discriminant(fundamental_discriminant) ||
        fundamental_discriminant >= -4) {
        throw std::invalid_argument(
            "invalid fundamental quadratic discriminant");
    }
    if (conductor <= 0 || !mpz_fits_ulong_p(conductor.get_mpz_t())) {
        throw std::invalid_argument(
            "suitable-order conductor is outside the reference range");
    }
    const mpz_class absolute_fundamental = -fundamental_discriminant;
    const mpz_class encoded_c2 = encode_u64(c2);
    const mpz_class c2_squared = encoded_c2 * encoded_c2;
    if (absolute_fundamental <= 4 || absolute_fundamental > c2_squared) {
        throw std::invalid_argument(
            "fundamental discriminant violates suitable-order bounds");
    }
    const mpz_class discriminant =
        fundamental_discriminant * conductor * conductor;
    const mpz_class absolute_discriminant = -discriminant;
    const mpz_class level_integer(std::to_string(level));
    const mpz_class level_squared = level_integer * level_integer;
    if (absolute_discriminant < level_squared ||
        absolute_discriminant > c2_squared * level_squared) {
        throw std::invalid_argument(
            "order discriminant violates suitable-order bounds");
    }
    mpz_class conductor_gcd;
    const mpz_class coprimality_modulus =
        2 * level_integer * absolute_fundamental;
    mpz_gcd(conductor_gcd.get_mpz_t(), conductor.get_mpz_t(),
            coprimality_modulus.get_mpz_t());
    if (conductor_gcd != 1) {
        throw std::invalid_argument(
            "order conductor is not coprime to 2*ell*D0");
    }
    const std::uint64_t conductor_u =
        static_cast<std::uint64_t>(conductor.get_ui());
    if (!conductor_prime_factors_are_bounded(
            conductor_u, std::min(c2, static_cast<std::uint64_t>(level)))) {
        throw std::invalid_argument(
            "order conductor has an excessive prime divisor");
    }
    const unsigned __int128 maximum_wide =
        static_cast<unsigned __int128>(c1_numerator) * level /
        c1_denominator;
    if (maximum_wide >= std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("suitable-order class bound overflows");
    }
    const std::uint64_t maximum_class_number =
        static_cast<std::uint64_t>(maximum_wide);
    const std::uint64_t class_number =
        class_number_with_limit(discriminant, maximum_class_number);
    if (class_number < static_cast<std::uint64_t>(level) + 2U ||
        class_number > maximum_class_number) {
        throw std::invalid_argument(
            "order class number violates suitable-order bounds");
    }
    return SutherlandSuitableOrder(
        level, fundamental_discriminant, conductor, discriminant,
        class_number);
}

SutherlandOrderSearchResult discover_sutherland_suitable_order(
    unsigned level, const SutherlandOrderSearchOptions& options) {
    validate_odd_prime_level(level);
    if (options.c1_denominator == 0U ||
        options.c1_numerator <= options.c1_denominator ||
        options.c2 <= 1U || options.maximum_fundamental_abs < 5U ||
        options.maximum_conductor_candidates == 0U) {
        throw std::invalid_argument(
            "invalid suitable-order discovery options");
    }
    const unsigned __int128 maximum_class_wide =
        static_cast<unsigned __int128>(options.c1_numerator) * level /
        options.c1_denominator;
    if (maximum_class_wide <
        static_cast<unsigned __int128>(level) + 2U) {
        throw std::invalid_argument(
            "suitable-order class-number interval is empty");
    }
    if (maximum_class_wide >=
        std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "suitable-order discovery class bound overflows");
    }
    const std::uint64_t maximum_class_number =
        static_cast<std::uint64_t>(maximum_class_wide);
    const mpz_class ell = encode_u64(level);
    const mpz_class ell_squared = ell * ell;
    const mpz_class c2 = encode_u64(options.c2);
    const mpz_class c2_squared = c2 * c2;
    const mpz_class maximum_discriminant = c2_squared * ell_squared;
    const mpz_class requested_maximum_fundamental =
        encode_u64(options.maximum_fundamental_abs);
    const mpz_class maximum_fundamental =
        requested_maximum_fundamental < c2_squared
            ? requested_maximum_fundamental
            : c2_squared;
    if (maximum_fundamental ==
        encode_u64(std::numeric_limits<std::uint64_t>::max())) {
        throw std::overflow_error(
            "fundamental-discriminant search bound cannot be UINT64_MAX");
    }
    std::uint64_t maximum_fundamental_u64 = 0U;
    if (!export_u64(maximum_fundamental, maximum_fundamental_u64)) {
        throw std::overflow_error(
            "fundamental-discriminant search bound exceeds 64 bits");
    }

    std::uint64_t fundamental_tested = 0U;
    std::uint64_t conductor_tested = 0U;
    for (std::uint64_t absolute_fundamental = 5U;
         absolute_fundamental <= maximum_fundamental_u64;
         ++absolute_fundamental) {
        const mpz_class fundamental_discriminant =
            -encode_u64(absolute_fundamental);
        if (!is_fundamental_discriminant(fundamental_discriminant)) {
            continue;
        }
        if (fundamental_tested == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error(
                "fundamental-discriminant counter overflow");
        }
        ++fundamental_tested;
        if (options.require_weber_f_order_congruences &&
            (mpz_fdiv_ui(fundamental_discriminant.get_mpz_t(), 8U) != 1U ||
             mpz_divisible_ui_p(
                 fundamental_discriminant.get_mpz_t(), 3U) != 0)) {
            continue;
        }
        const std::uint64_t fundamental_class_number =
            negative_order_class_number(fundamental_discriminant);
        const std::uint64_t minimum_conductor =
            std::max<std::uint64_t>(
                1U,
                ceil_sqrt_quotient(ell_squared, absolute_fundamental));
        const std::uint64_t maximum_conductor =
            floor_sqrt_quotient(maximum_discriminant,
                                absolute_fundamental);
        if (minimum_conductor > maximum_conductor) {
            continue;
        }
        const std::uint64_t conductor_factor_bound =
            std::min(options.c2, static_cast<std::uint64_t>(level));
        for (std::uint64_t conductor = minimum_conductor;; ++conductor) {
            if (conductor_tested ==
                options.maximum_conductor_candidates) {
                throw std::runtime_error(
                    "suitable-order discovery exhausted its conductor cap");
            }
            ++conductor_tested;
            const bool structurally_admissible =
                std::gcd(conductor, static_cast<std::uint64_t>(2U)) == 1U &&
                std::gcd(conductor, static_cast<std::uint64_t>(level)) == 1U &&
                std::gcd(conductor, absolute_fundamental) == 1U &&
                (!options.require_weber_f_order_congruences ||
                 conductor % 3U != 0U) &&
                conductor_prime_factors_are_bounded(
                    conductor, conductor_factor_bound);
            if (structurally_admissible) {
                const std::uint64_t class_number =
                    order_class_number_from_conductor_formula(
                        fundamental_discriminant,
                        fundamental_class_number, conductor);
                if (class_number >=
                        static_cast<std::uint64_t>(level) + 2U &&
                    class_number <= maximum_class_number) {
                    SutherlandSuitableOrder order =
                        validate_sutherland_suitable_order(
                            level, fundamental_discriminant,
                            encode_u64(conductor), options.c1_numerator,
                            options.c1_denominator, options.c2);
                    if (order.class_number() != class_number) {
                        throw std::logic_error(
                            "ring-class formula and form enumeration disagree");
                    }
                    if (options.require_weber_f_order_congruences &&
                        !order.weber_f_order_congruences_hold()) {
                        throw std::logic_error(
                            "discovered order lost the Weber-f congruences");
                    }
                    return {std::move(order), fundamental_tested,
                            conductor_tested};
                }
            }
            if (conductor == maximum_conductor) {
                break;
            }
        }
    }
    throw std::runtime_error(
        "no suitable order found inside the explicit discovery bounds");
}

namespace {

std::vector<SutherlandCrtPrime> select_sutherland_crt_primes_impl(
    const SutherlandSuitableOrder& order, const mpz_class& target_modulus,
    const mpz_class& coefficient_abs_bound,
    std::uint64_t maximum_candidates, bool require_weber_two_roots) {
    if (!is_probable_prime(target_modulus)) {
        throw std::invalid_argument("CRT target modulus must be prime");
    }
    if (coefficient_abs_bound < 1) {
        throw std::invalid_argument("CRT coefficient bound must be positive");
    }
    if (maximum_candidates == 0U) {
        throw std::invalid_argument("CRT prime candidate cap must be positive");
    }
    const mpz_class required_product = 4 * coefficient_abs_bound;
    const mpz_class ell(std::to_string(order.level()));
    const mpz_class v =
        mpz_fdiv_ui(order.discriminant().get_mpz_t(), 8U) == 1U ? 2 : 1;
    if (mpz_divisible_p(v.get_mpz_t(), ell.get_mpz_t())) {
        throw std::logic_error("fixed volcano parameter is divisible by ell");
    }
    const mpz_class fixed_term =
        ell * ell * v * v * order.discriminant();
    const mpz_class trace_step = 2 * ell;
    const bool trace_must_be_odd =
        mpz_odd_p(v.get_mpz_t()) != 0 &&
        mpz_odd_p(order.discriminant().get_mpz_t()) != 0;
    mpz_class trace = trace_must_be_odd ? ell + 2 : mpz_class(2);
    mpz_class product = 1;
    std::vector<SutherlandCrtPrime> output;
    for (std::uint64_t candidate = 0U; candidate < maximum_candidates;
         ++candidate, trace += trace_step) {
        const mpz_class numerator = trace * trace - fixed_term;
        if (!mpz_divisible_ui_p(numerator.get_mpz_t(), 4U)) {
            throw std::logic_error(
                "suitable-prime numerator has the wrong parity");
        }
        const mpz_class prime = numerator / 4;
        if (prime == target_modulus ||
            (require_weber_two_roots &&
             mpz_fdiv_ui(prime.get_mpz_t(), 12U) != 11U) ||
            mpz_divisible_p(order.discriminant().get_mpz_t(),
                            prime.get_mpz_t()) != 0 ||
            !is_proven_auxiliary_prime(prime)) {
            continue;
        }
        if (mpz_fdiv_ui(prime.get_mpz_t(), order.level()) != 1U) {
            throw std::logic_error(
                "selected CRT prime is not 1 modulo ell");
        }
        output.push_back({prime, trace, v});
        product *= prime;
        if (product > required_product) {
            return output;
        }
    }
    throw std::runtime_error(
        "suitable-prime search exhausted before the CRT height bound");
}

}  // namespace

std::vector<SutherlandCrtPrime> select_sutherland_crt_primes(
    const SutherlandSuitableOrder& order, const mpz_class& target_modulus,
    const mpz_class& coefficient_abs_bound,
    std::uint64_t maximum_candidates) {
    return select_sutherland_crt_primes_impl(
        order, target_modulus, coefficient_abs_bound, maximum_candidates,
        false);
}

std::vector<SutherlandCrtPrime> select_sutherland_weber_crt_primes(
    const SutherlandSuitableOrder& order, const mpz_class& target_modulus,
    const mpz_class& coefficient_abs_bound,
    std::uint64_t maximum_candidates) {
    if (!order.weber_f_order_congruences_hold()) {
        throw std::invalid_argument(
            "Weber CRT prime selection received an incompatible order");
    }
    return select_sutherland_crt_primes_impl(
        order, target_modulus, coefficient_abs_bound, maximum_candidates,
        true);
}

std::vector<mpz_class> lifted_target_powers(
    const Field& target_field, const mpz_class& source_x,
    unsigned maximum_degree) {
    const mpz_class normalized_source = target_field.normalize(source_x);
    std::vector<mpz_class> powers(
        static_cast<std::size_t>(maximum_degree) + 1U, 1);
    for (std::size_t index = 1U; index < powers.size(); ++index) {
        powers[index] = target_field.mul(powers[index - 1U],
                                         normalized_source);
    }
    return powers;
}

CrtCoefficientBound::CrtCoefficientBound(
    mpz_class absolute_bound, CrtCoefficientBoundEvidence evidence)
    : absolute_bound_(std::move(absolute_bound)), evidence_(evidence) {
    if (absolute_bound_ < 1) {
        throw std::invalid_argument(
            "CRT coefficient bound must be positive");
    }
}

CrtCoefficientBound
derive_exact_crt_coefficient_bound_from_table_reference(
    const SparseModularPolynomial& modular_polynomial,
    const std::vector<mpz_class>& target_power_lifts) {
    unsigned maximum_x_degree = 0U;
    for (const BivariateTerm& term : modular_polynomial.terms()) {
        maximum_x_degree = std::max(maximum_x_degree, term.x_degree);
    }
    if (target_power_lifts.size() <= maximum_x_degree) {
        throw std::invalid_argument(
            "height derivation is missing target power lifts");
    }
    for (const mpz_class& power : target_power_lifts) {
        if (power < 0) {
            throw std::invalid_argument(
                "height derivation requires nonnegative target power lifts");
        }
    }
    const std::size_t coefficient_count =
        static_cast<std::size_t>(modular_polynomial.level()) + 2U;
    std::vector<mpz_class> value(coefficient_count, 0);
    std::vector<mpz_class> x_derivative(coefficient_count, 0);
    for (const BivariateTerm& term : modular_polynomial.terms()) {
        if (term.y_degree >= coefficient_count) {
            throw std::invalid_argument(
                "height derivation saw an excessive Y degree");
        }
        value[term.y_degree] +=
            term.coefficient * target_power_lifts[term.x_degree];
        if (term.x_degree != 0U) {
            x_derivative[term.y_degree] +=
                term.coefficient * term.x_degree *
                target_power_lifts[term.x_degree - 1U];
        }
    }
    mpz_class maximum = 1;
    for (const mpz_class& coefficient : value) {
        maximum = std::max(
            maximum, coefficient < 0 ? -coefficient : coefficient);
    }
    for (const mpz_class& coefficient : x_derivative) {
        maximum = std::max(
            maximum, coefficient < 0 ? -coefficient : coefficient);
    }
    return CrtCoefficientBound(
        maximum, CrtCoefficientBoundEvidence::exact_table_reference);
}

CrtSpecializationResult reconstruct_specialization_explicit_crt(
    unsigned level, const Field& target_field, const mpz_class& source_x,
    const mpz_class& coefficient_abs_bound,
    const std::vector<mpz_class>& auxiliary_primes,
    const CrtSpecializationResidueProvider& provider) {
    validate_odd_prime_level(level);
    if (!is_probable_prime(target_field.modulus())) {
        throw std::invalid_argument("CRT target field modulus must be prime");
    }
    const mpz_class normalized_source = target_field.normalize(source_x);
    if (normalized_source != source_x) {
        throw std::invalid_argument("CRT source coordinate is not canonical");
    }
    if (coefficient_abs_bound < 1) {
        throw std::invalid_argument("CRT coefficient bound must be positive");
    }
    if (auxiliary_primes.empty() || !provider) {
        throw std::invalid_argument("CRT reconstruction has no residue source");
    }
    std::set<mpz_class> distinct_primes;
    mpz_class product = 1;
    for (const mpz_class& prime : auxiliary_primes) {
        if (prime <= 2 || !is_proven_auxiliary_prime(prime) ||
            prime == target_field.modulus() ||
            !distinct_primes.insert(prime).second) {
            throw std::invalid_argument(
                "CRT auxiliary moduli must be distinct odd primes away from q");
        }
        product *= prime;
    }
    if (product <= 4 * coefficient_abs_bound) {
        throw std::runtime_error(
            "CRT prime product does not exceed four times the height bound");
    }

    const std::size_t coefficient_count =
        static_cast<std::size_t>(level) + 2U;
    const std::size_t channel_count = 2U * coefficient_count;
    std::vector<mpz_class> target_sums(channel_count, 0);
    std::vector<mpz_class> rounding_numerators(channel_count, 0);
    for (const mpz_class& prime : auxiliary_primes) {
        const mpz_class partial_product = product / prime;
        mpz_class inverse;
        if (mpz_invert(inverse.get_mpz_t(), partial_product.get_mpz_t(),
                       prime.get_mpz_t()) == 0) {
            throw std::logic_error("CRT cofactor is not invertible");
        }
        const mpz_class partial_mod_target =
            target_field.normalize(partial_product);
        const CrtSpecializationResidue residue = provider(prime);
        if (residue.prime != prime) {
            throw std::invalid_argument(
                "CRT residue provider returned the wrong prime");
        }
        validate_residue_coefficients(
            residue.value_coefficients, prime, coefficient_count,
            "specialized value");
        validate_residue_coefficients(
            residue.x_derivative_coefficients, prime, coefficient_count,
            "specialized X derivative");
        if (residue.value_coefficients.back() != 1 ||
            residue.x_derivative_coefficients.back() != 0) {
            throw std::invalid_argument(
                "CRT specialization has invalid leading coefficients");
        }
        for (std::size_t channel = 0U; channel < channel_count; ++channel) {
            const mpz_class& coefficient =
                channel < coefficient_count
                    ? residue.value_coefficients[channel]
                    : residue.x_derivative_coefficients[
                          channel - coefficient_count];
            mpz_class scaled = (coefficient * inverse) % prime;
            if (scaled < 0) {
                scaled += prime;
            }
            rounding_numerators[channel] += scaled * partial_product;
            target_sums[channel] = target_field.add(
                target_sums[channel],
                target_field.mul(scaled, partial_mod_target));
        }
    }

    const mpz_class product_mod_target = target_field.normalize(product);
    std::vector<mpz_class> reconstructed(channel_count);
    for (std::size_t channel = 0U; channel < channel_count; ++channel) {
        const mpz_class numerator = rounding_numerators[channel];
        const mpz_class rounding_integer =
            (2 * numerator + product) / (2 * product);
        const mpz_class centered = numerator - rounding_integer * product;
        const mpz_class absolute_centered = centered < 0 ? -centered : centered;
        if (absolute_centered > coefficient_abs_bound) {
            throw std::runtime_error(
                "centered CRT coefficient exceeds the declared height bound");
        }
        reconstructed[channel] = target_field.sub(
            target_sums[channel],
            target_field.mul(rounding_integer, product_mod_target));
        if (reconstructed[channel] != target_field.normalize(centered)) {
            throw std::logic_error(
                "explicit CRT and centered reconstruction disagree");
        }
    }
    std::vector<mpz_class> value(
        reconstructed.begin(), reconstructed.begin() +
            static_cast<std::ptrdiff_t>(coefficient_count));
    std::vector<mpz_class> x_derivative(
        reconstructed.begin() + static_cast<std::ptrdiff_t>(coefficient_count),
        reconstructed.end());
    return {
        ModularPolynomialSpecialization(
            level, normalized_source,
            Poly(target_field, std::move(value)),
            Poly(target_field, std::move(x_derivative))),
        product, coefficient_abs_bound, auxiliary_primes.size()};
}

CrtSpecializationResult reconstruct_weber_specialization_algorithm1(
    const SutherlandSuitableOrder& order, const Field& target_field,
    const mpz_class& source_x,
    const CrtCoefficientBound& coefficient_bound,
    std::uint64_t maximum_candidates,
    const SutherlandSpecializationResidueProvider& provider) {
    if (!order.weber_f_order_congruences_hold()) {
        throw std::invalid_argument(
            "suitable order does not satisfy the Weber-f congruences");
    }
    if (!provider) {
        throw std::invalid_argument(
            "Algorithm 1 has no per-prime specialization provider");
    }
    if (target_field.normalize(source_x) != source_x) {
        throw std::invalid_argument(
            "Algorithm 1 source coordinate is not canonical");
    }
    const std::vector<mpz_class> target_power_lifts =
        lifted_target_powers(target_field, source_x, order.level() + 1U);
    const std::vector<SutherlandCrtPrime> selected =
        select_sutherland_weber_crt_primes(
            order, target_field.modulus(),
            coefficient_bound.absolute_bound(),
            maximum_candidates);
    std::vector<mpz_class> primes;
    primes.reserve(selected.size());
    for (const SutherlandCrtPrime& record : selected) {
        primes.push_back(record.prime);
    }

    std::size_t next = 0U;
    CrtSpecializationResult result =
        reconstruct_specialization_explicit_crt(
            order.level(), target_field, source_x,
            coefficient_bound.absolute_bound(),
            primes,
            [&selected, &provider, &target_power_lifts, &next](
                const mpz_class& prime) {
                if (next >= selected.size() ||
                    selected[next].prime != prime) {
                    throw std::logic_error(
                        "Algorithm 1 CRT prime stream lost synchronization");
                }
                return provider(selected[next++], target_power_lifts);
            });
    if (next != selected.size()) {
        throw std::logic_error(
            "Algorithm 1 did not consume every selected CRT prime");
    }
    return result;
}

CrtSpecializationResidue specialize_sparse_modpoly_for_crt_reference(
    const SparseModularPolynomial& modular_polynomial,
    const std::vector<mpz_class>& target_power_lifts,
    const mpz_class& auxiliary_prime) {
    if (!is_proven_auxiliary_prime(auxiliary_prime)) {
        throw std::invalid_argument(
            "reference specialization requires an auxiliary prime");
    }
    unsigned maximum_x_degree = 0U;
    for (const BivariateTerm& term : modular_polynomial.terms()) {
        maximum_x_degree = std::max(maximum_x_degree, term.x_degree);
    }
    if (target_power_lifts.size() <= maximum_x_degree) {
        throw std::invalid_argument(
            "reference specialization is missing target power lifts");
    }
    for (const mpz_class& power : target_power_lifts) {
        if (power < 0) {
            throw std::invalid_argument(
                "target power lifts must be nonnegative integers");
        }
    }
    const Field auxiliary_field(auxiliary_prime);
    const std::size_t coefficient_count =
        static_cast<std::size_t>(modular_polynomial.level()) + 2U;
    std::vector<mpz_class> value(coefficient_count, 0);
    std::vector<mpz_class> x_derivative(coefficient_count, 0);
    for (const BivariateTerm& term : modular_polynomial.terms()) {
        if (term.y_degree >= coefficient_count) {
            throw std::invalid_argument(
                "modular-polynomial Y degree exceeds ell+1");
        }
        value[term.y_degree] = auxiliary_field.add(
            value[term.y_degree],
            auxiliary_field.mul(term.coefficient,
                                target_power_lifts[term.x_degree]));
        if (term.x_degree != 0U) {
            x_derivative[term.y_degree] = auxiliary_field.add(
                x_derivative[term.y_degree],
                auxiliary_field.mul(
                    auxiliary_field.mul(term.coefficient, term.x_degree),
                    target_power_lifts[term.x_degree - 1U]));
        }
    }
    return {auxiliary_prime, std::move(value), std::move(x_derivative)};
}

}  // namespace oneshotsea
