#include "oneshotsea/direct_modpoly.hpp"

#include <algorithm>
#include <array>
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

std::uint64_t multiply_mod_u64(std::uint64_t lhs, std::uint64_t rhs,
                               std::uint64_t modulus) {
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(lhs) * rhs) % modulus);
}

std::uint64_t power_mod_u64(std::uint64_t base, std::uint64_t exponent,
                            std::uint64_t modulus) {
    std::uint64_t result = 1U;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = multiply_mod_u64(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = multiply_mod_u64(base, base, modulus);
        }
    }
    return result;
}

bool is_prime_u64(std::uint64_t value) {
    if (value < 2U) {
        return false;
    }
    for (const std::uint64_t small :
         std::array<std::uint64_t, 12>{2U, 3U, 5U, 7U, 11U, 13U,
                                      17U, 19U, 23U, 29U, 31U, 37U}) {
        if (value == small) {
            return true;
        }
        if (value % small == 0U) {
            return false;
        }
    }
    std::uint64_t odd_part = value - 1U;
    unsigned shifts = 0U;
    while ((odd_part & 1U) == 0U) {
        odd_part >>= 1U;
        ++shifts;
    }
    // Deterministic for all unsigned 64-bit inputs.
    for (const std::uint64_t witness :
         std::array<std::uint64_t, 7>{2U, 325U, 9375U, 28178U, 450775U,
                                     9780504U, 1795265022U}) {
        const std::uint64_t base = witness % value;
        if (base == 0U) {
            continue;
        }
        std::uint64_t power = power_mod_u64(base, odd_part, value);
        if (power == 1U || power == value - 1U) {
            continue;
        }
        bool composite = true;
        for (unsigned round = 1U; round < shifts; ++round) {
            power = multiply_mod_u64(power, power, value);
            if (power == value - 1U) {
                composite = false;
                break;
            }
        }
        if (composite) {
            return false;
        }
    }
    return true;
}

bool export_u64(const mpz_class& value, std::uint64_t& output) {
    if (value < 0 || mpz_sizeinbase(value.get_mpz_t(), 2) > 64U) {
        return false;
    }
    output = 0U;
    std::size_t words = 0U;
    mpz_export(&output, &words, -1, sizeof(output), 0, 0,
               value.get_mpz_t());
    return words <= 1U;
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

std::vector<SutherlandCrtPrime> select_sutherland_crt_primes(
    const SutherlandSuitableOrder& order, const mpz_class& target_modulus,
    const mpz_class& coefficient_abs_bound,
    std::uint64_t maximum_candidates) {
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
    const mpz_class& source_x, const mpz_class& coefficient_abs_bound,
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
        select_sutherland_crt_primes(
            order, target_field.modulus(), coefficient_abs_bound,
            maximum_candidates);
    std::vector<mpz_class> primes;
    primes.reserve(selected.size());
    for (const SutherlandCrtPrime& record : selected) {
        primes.push_back(record.prime);
    }

    std::size_t next = 0U;
    CrtSpecializationResult result =
        reconstruct_specialization_explicit_crt(
            order.level(), target_field, source_x, coefficient_abs_bound,
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
