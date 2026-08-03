#include "oneshotsea/poly.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace oneshotsea {
namespace {

void require_same_field(const Poly& lhs, const Poly& rhs) {
    if (lhs.field().modulus() != rhs.field().modulus()) {
        throw std::invalid_argument("polynomials belong to different fields");
    }
}

void require_probable_prime_field(const Poly& polynomial) {
    if (mpz_probab_prime_p(polynomial.field().modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("polynomial root extraction requires probable-prime p");
    }
}

constexpr std::size_t kKaratsubaCoefficientThreshold = 32U;
#if defined(ONESHOTSEA_KRONECKER_COEFFICIENT_THRESHOLD)
constexpr std::size_t kKroneckerCoefficientThreshold =
    static_cast<std::size_t>(ONESHOTSEA_KRONECKER_COEFFICIENT_THRESHOLD);
#else
constexpr std::size_t kKroneckerCoefficientThreshold = 48U;
#endif
#if defined(ONESHOTSEA_RECIPROCAL_REDUCTION_DEGREE_THRESHOLD)
constexpr std::size_t kReciprocalReductionDegreeThreshold =
    static_cast<std::size_t>(
        ONESHOTSEA_RECIPROCAL_REDUCTION_DEGREE_THRESHOLD);
#else
constexpr std::size_t kReciprocalReductionDegreeThreshold = 96U;
#endif
constexpr unsigned int kMaximumPowmodWindowBits = 5U;

struct PowmodWindowStep {
    std::size_t squarings;
    unsigned int odd_power;
};

struct PowmodPlan {
    bool exponent_is_zero = true;
    unsigned int window_bits = 1U;
    unsigned int first_odd_power = 0U;
    unsigned int maximum_odd_power = 0U;
    std::vector<PowmodWindowStep> steps;
    std::size_t trailing_squarings = 0U;
    std::size_t operation_count = 0U;
};

PowmodPlan powmod_plan_for_width(const mpz_class& exponent,
                                 unsigned int window_bits) {
    PowmodPlan plan;
    plan.window_bits = window_bits;
    if (exponent == 0) {
        return plan;
    }
    plan.exponent_is_zero = false;
    const std::size_t bit_count = mpz_sizeinbase(exponent.get_mpz_t(), 2);
    std::size_t cursor = bit_count;
    bool first = true;
    std::size_t pending_zero_squarings = 0U;
    while (cursor > 0U) {
        const std::size_t high_bit = cursor - 1U;
        if (mpz_tstbit(exponent.get_mpz_t(), high_bit) == 0) {
            ++pending_zero_squarings;
            --cursor;
            continue;
        }
        const std::size_t low_limit =
            cursor > window_bits ? cursor - window_bits : 0U;
        std::size_t low_bit = low_limit;
        while (low_bit < high_bit &&
               mpz_tstbit(exponent.get_mpz_t(), low_bit) == 0) {
            ++low_bit;
        }
        unsigned int odd_power = 0U;
        for (std::size_t bit = high_bit + 1U; bit-- > low_bit;) {
            odd_power = static_cast<unsigned int>(
                (odd_power << 1U) |
                static_cast<unsigned int>(
                    mpz_tstbit(exponent.get_mpz_t(), bit) != 0));
        }
        const std::size_t width = high_bit - low_bit + 1U;
        plan.maximum_odd_power =
            std::max(plan.maximum_odd_power, odd_power);
        if (first) {
            plan.first_odd_power = odd_power;
            first = false;
        } else {
            plan.steps.push_back(
                {pending_zero_squarings + width, odd_power});
        }
        pending_zero_squarings = 0U;
        cursor = low_bit;
    }
    plan.trailing_squarings = pending_zero_squarings;

    plan.operation_count = plan.trailing_squarings + plan.steps.size();
    for (const PowmodWindowStep& step : plan.steps) {
        plan.operation_count += step.squarings;
    }
    if (plan.maximum_odd_power > 1U) {
        // One square for base^2, then one multiply for every odd table entry.
        plan.operation_count +=
            1U + static_cast<std::size_t>(
                       (plan.maximum_odd_power - 1U) / 2U);
    }
    return plan;
}

PowmodPlan make_powmod_plan(const mpz_class& exponent) {
    PowmodPlan best = powmod_plan_for_width(exponent, 1U);
    for (unsigned int width = 2U; width <= kMaximumPowmodWindowBits;
         ++width) {
        PowmodPlan candidate = powmod_plan_for_width(exponent, width);
        // Exact operation-count selection prevents table setup from regressing
        // sparse or small arbitrary exponents. Ties retain the smaller table.
        if (candidate.operation_count < best.operation_count) {
            best = std::move(candidate);
        }
    }
    return best;
}

std::vector<mpz_class> schoolbook_product(
    std::span<const mpz_class> lhs, std::span<const mpz_class> rhs) {
    if (lhs.empty() || rhs.empty()) {
        return {};
    }
    std::vector<mpz_class> output(lhs.size() + rhs.size() - 1U, 0);
    for (std::size_t lhs_index = 0; lhs_index < lhs.size(); ++lhs_index) {
        for (std::size_t rhs_index = 0; rhs_index < rhs.size(); ++rhs_index) {
            mpz_addmul(output[lhs_index + rhs_index].get_mpz_t(),
                       lhs[lhs_index].get_mpz_t(),
                       rhs[rhs_index].get_mpz_t());
        }
    }
    return output;
}

std::vector<mpz_class> schoolbook_square(
    std::span<const mpz_class> value) {
    if (value.empty()) {
        return {};
    }
    std::vector<mpz_class> output(2U * value.size() - 1U, 0);
    for (std::size_t left = 0; left < value.size(); ++left) {
        for (std::size_t right = left + 1U; right < value.size(); ++right) {
            mpz_addmul(output[left + right].get_mpz_t(),
                       value[left].get_mpz_t(), value[right].get_mpz_t());
        }
    }
    for (mpz_class& coefficient : output) {
        mpz_mul_2exp(coefficient.get_mpz_t(), coefficient.get_mpz_t(), 1U);
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        mpz_addmul(output[2U * index].get_mpz_t(),
                   value[index].get_mpz_t(), value[index].get_mpz_t());
    }
    return output;
}

std::size_t ceiling_log2(std::size_t value) {
    if (value == 0U) {
        throw std::invalid_argument("logarithm requires a positive value");
    }
    std::size_t result = 0U;
    --value;
    while (value != 0U) {
        ++result;
        value >>= 1U;
    }
    return result;
}

// Pack nonnegative coefficients into fixed-width, limb-aligned digits.  The
// digit width is chosen so that an entire convolution coefficient is strictly
// smaller than the radix, making integer multiplication an exact polynomial
// convolution with no inter-digit carries (Kronecker substitution).
std::size_t kronecker_digit_limbs(const Field& field,
                                  std::size_t maximum_summands) {
    const std::size_t modulus_bits =
        mpz_sizeinbase(field.modulus().get_mpz_t(), 2);
    const std::size_t summand_bits = ceiling_log2(maximum_summands);
    if (modulus_bits >
        (std::numeric_limits<std::size_t>::max() - summand_bits) / 2U) {
        throw std::overflow_error("Kronecker digit width overflow");
    }
    const std::size_t digit_bits = 2U * modulus_bits + summand_bits;
    const std::size_t limb_bits = static_cast<std::size_t>(GMP_NUMB_BITS);
    return digit_bits / limb_bits +
           static_cast<std::size_t>(digit_bits % limb_bits != 0U);
}

mpz_class kronecker_pack(std::span<const mpz_class> coefficients,
                         std::size_t digit_limbs) {
    if (coefficients.empty()) {
        return 0;
    }
    if (digit_limbs == 0U ||
        coefficients.size() >
            std::numeric_limits<std::size_t>::max() / digit_limbs) {
        throw std::overflow_error("Kronecker packed size overflow");
    }
    const std::size_t total_limbs = coefficients.size() * digit_limbs;
    if (total_limbs >
        static_cast<std::size_t>(std::numeric_limits<mp_size_t>::max())) {
        throw std::overflow_error("Kronecker packed value is too large");
    }

    mpz_class packed;
    mp_ptr destination =
        mpz_limbs_write(packed.get_mpz_t(), static_cast<mp_size_t>(total_limbs));
    std::fill_n(destination, total_limbs, static_cast<mp_limb_t>(0));
    for (std::size_t index = 0U; index < coefficients.size(); ++index) {
        if (mpz_sgn(coefficients[index].get_mpz_t()) < 0) {
            throw std::logic_error(
                "Kronecker substitution requires normalized coefficients");
        }
        const std::size_t coefficient_limbs =
            mpz_size(coefficients[index].get_mpz_t());
        if (coefficient_limbs > digit_limbs) {
            throw std::logic_error(
                "Kronecker coefficient exceeds its packed digit");
        }
        if (coefficient_limbs != 0U) {
            std::copy_n(mpz_limbs_read(coefficients[index].get_mpz_t()),
                        coefficient_limbs,
                        destination + index * digit_limbs);
        }
    }
    mpz_limbs_finish(packed.get_mpz_t(),
                     static_cast<mp_size_t>(total_limbs));
    return packed;
}

std::vector<mpz_class> kronecker_unpack(const mpz_class& packed,
                                        std::size_t digit_limbs,
                                        std::size_t coefficient_count) {
    std::vector<mpz_class> coefficients(coefficient_count);
    const std::size_t packed_limbs = mpz_size(packed.get_mpz_t());
    const mp_srcptr source = mpz_limbs_read(packed.get_mpz_t());
    for (std::size_t index = 0U; index < coefficient_count; ++index) {
        if (index > std::numeric_limits<std::size_t>::max() / digit_limbs) {
            throw std::overflow_error("Kronecker unpack offset overflow");
        }
        const std::size_t offset = index * digit_limbs;
        if (offset >= packed_limbs) {
            break;
        }
        const std::size_t count =
            std::min(digit_limbs, packed_limbs - offset);
        mp_ptr destination = mpz_limbs_write(
            coefficients[index].get_mpz_t(), static_cast<mp_size_t>(count));
        std::copy_n(source + offset, count, destination);
        mpz_limbs_finish(coefficients[index].get_mpz_t(),
                         static_cast<mp_size_t>(count));
    }
    return coefficients;
}

std::vector<mpz_class> kronecker_product(
    std::span<const mpz_class> lhs, std::span<const mpz_class> rhs,
    const Field& field) {
    if (lhs.empty() || rhs.empty()) {
        return {};
    }
    if (lhs.size() >
        std::numeric_limits<std::size_t>::max() - rhs.size() + 1U) {
        throw std::overflow_error("Kronecker coefficient count overflow");
    }
    const std::size_t digit_limbs =
        kronecker_digit_limbs(field, std::min(lhs.size(), rhs.size()));
    const std::size_t coefficient_count = lhs.size() + rhs.size() - 1U;
    if (coefficient_count >
        static_cast<std::size_t>(std::numeric_limits<mp_size_t>::max()) /
            digit_limbs) {
        throw std::overflow_error("Kronecker product size overflow");
    }
    const mpz_class packed_lhs = kronecker_pack(lhs, digit_limbs);
    const mpz_class packed_rhs = kronecker_pack(rhs, digit_limbs);
    return kronecker_unpack(packed_lhs * packed_rhs, digit_limbs,
                            coefficient_count);
}

std::vector<mpz_class> add_coefficient_slices(
    std::span<const mpz_class> low, std::span<const mpz_class> high) {
    std::vector<mpz_class> sum(std::max(low.size(), high.size()), 0);
    for (std::size_t index = 0; index < low.size(); ++index) {
        sum[index] += low[index];
    }
    for (std::size_t index = 0; index < high.size(); ++index) {
        sum[index] += high[index];
    }
    return sum;
}

void add_shifted_coefficients(std::vector<mpz_class>& output,
                              std::span<const mpz_class> value,
                              std::size_t shift, int sign) {
    if (shift > output.size() || value.size() > output.size() - shift) {
        throw std::logic_error("Karatsuba coefficient range overflow");
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (sign > 0) {
            output[shift + index] += value[index];
        } else {
            output[shift + index] -= value[index];
        }
    }
}

std::vector<mpz_class> karatsuba_product(
    std::span<const mpz_class> lhs, std::span<const mpz_class> rhs) {
    if (lhs.empty() || rhs.empty()) {
        return {};
    }
    const std::size_t smaller = std::min(lhs.size(), rhs.size());
    const std::size_t larger = std::max(lhs.size(), rhs.size());
    // Very unbalanced products do not benefit from zero-padded Karatsuba.
    // Retaining schoolbook here also bounds recursive temporary storage.
    if (smaller <= kKaratsubaCoefficientThreshold ||
        larger / smaller >= 2U) {
        return schoolbook_product(lhs, rhs);
    }

    const std::size_t split = larger / 2U;
    const std::size_t lhs_split = std::min(split, lhs.size());
    const std::size_t rhs_split = std::min(split, rhs.size());
    const std::span<const mpz_class> lhs_low = lhs.first(lhs_split);
    const std::span<const mpz_class> lhs_high = lhs.subspan(lhs_split);
    const std::span<const mpz_class> rhs_low = rhs.first(rhs_split);
    const std::span<const mpz_class> rhs_high = rhs.subspan(rhs_split);

    std::vector<mpz_class> low = karatsuba_product(lhs_low, rhs_low);
    std::vector<mpz_class> high = karatsuba_product(lhs_high, rhs_high);
    const std::vector<mpz_class> lhs_sum =
        add_coefficient_slices(lhs_low, lhs_high);
    const std::vector<mpz_class> rhs_sum =
        add_coefficient_slices(rhs_low, rhs_high);
    std::vector<mpz_class> middle = karatsuba_product(lhs_sum, rhs_sum);

    std::vector<mpz_class> output(lhs.size() + rhs.size() - 1U, 0);
    add_shifted_coefficients(output, low, 0U, 1);
    add_shifted_coefficients(output, middle, split, 1);
    add_shifted_coefficients(output, low, split, -1);
    add_shifted_coefficients(output, high, split, -1);
    add_shifted_coefficients(output, high, 2U * split, 1);
    return output;
}

std::vector<mpz_class> karatsuba_square(
    std::span<const mpz_class> value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() <= kKaratsubaCoefficientThreshold) {
        return schoolbook_square(value);
    }

    const std::size_t split = value.size() / 2U;
    const std::span<const mpz_class> low_input = value.first(split);
    const std::span<const mpz_class> high_input = value.subspan(split);
    std::vector<mpz_class> low = karatsuba_square(low_input);
    std::vector<mpz_class> high = karatsuba_square(high_input);
    const std::vector<mpz_class> sum =
        add_coefficient_slices(low_input, high_input);
    std::vector<mpz_class> middle = karatsuba_square(sum);

    std::vector<mpz_class> output(2U * value.size() - 1U, 0);
    add_shifted_coefficients(output, low, 0U, 1);
    add_shifted_coefficients(output, middle, split, 1);
    add_shifted_coefficients(output, low, split, -1);
    add_shifted_coefficients(output, high, split, -1);
    add_shifted_coefficients(output, high, 2U * split, 1);
    return output;
}

std::vector<mpz_class> coefficient_product(
    std::span<const mpz_class> lhs, std::span<const mpz_class> rhs,
    const Field& field) {
    if (!lhs.empty() && !rhs.empty()) {
        const std::size_t smaller = std::min(lhs.size(), rhs.size());
        const std::size_t larger = std::max(lhs.size(), rhs.size());
        if (kKroneckerCoefficientThreshold != 0U &&
            smaller >= kKroneckerCoefficientThreshold &&
            larger / smaller < 2U) {
            return kronecker_product(lhs, rhs, field);
        }
    }
    return karatsuba_product(lhs, rhs);
}

std::vector<mpz_class> coefficient_square(
    std::span<const mpz_class> value, const Field& field) {
    if (kKroneckerCoefficientThreshold != 0U &&
        value.size() >= kKroneckerCoefficientThreshold) {
        return kronecker_product(value, value, field);
    }
    return karatsuba_square(value);
}

void split_linear_factors(const Poly& polynomial, std::vector<mpz_class>& roots) {
    if (polynomial.degree() <= 0) {
        return;
    }
    const Field& field = polynomial.field();
    if (polynomial.degree() == 1) {
        roots.push_back(field.divide(field.neg(polynomial.coefficient(0)),
                                     polynomial.coefficient(1)));
        return;
    }

    const Poly x = Poly::x(field);
    const mpz_class exponent = (field.modulus() - 1) / 2;
    constexpr std::uint64_t kSplitAttempts = 256;
    for (std::uint64_t attempt = 0; attempt < kSplitAttempts; ++attempt) {
        const mpz_class offset = deterministic_residue(
            field, UINT64_C(0x726f6f7473706c74), attempt,
            static_cast<std::uint64_t>(polynomial.degree()));
        const Poly character = powmod(
            add(x, Poly::constant(field, offset)), exponent, polynomial);
        const Poly candidates[] = {
            gcd(polynomial, character),
            gcd(polynomial, sub(character, Poly::constant(field, 1))),
            gcd(polynomial, add(character, Poly::constant(field, 1))),
        };
        for (const Poly& factor : candidates) {
            if (factor.degree() <= 0 || factor.degree() >= polynomial.degree()) {
                continue;
            }
            const auto [cofactor, remainder] = divmod(polynomial, factor);
            if (!remainder.is_zero()) {
                throw std::logic_error("root splitter produced a non-factor");
            }
            split_linear_factors(factor.monic(), roots);
            split_linear_factors(cofactor.monic(), roots);
            return;
        }
    }
    throw std::runtime_error("deterministic linear-root splitting attempt limit reached");
}

struct LinearRootAnalysis {
    Poly frobenius_x;
    std::vector<mpz_class> roots;
};

LinearRootAnalysis analyze_linear_roots(const Poly& polynomial) {
    if (polynomial.is_zero()) {
        throw std::invalid_argument(
            "zero polynomial has every field element as a root");
    }
    require_probable_prime_field(polynomial);
    const Field& field = polynomial.field();
    const Poly x = Poly::x(field);
    Poly frobenius_x = powmod(x, field.modulus(), polynomial);
    const Poly split = gcd(polynomial, sub(frobenius_x, x));
    std::vector<mpz_class> roots;
    roots.reserve(static_cast<std::size_t>(std::max(0, split.degree())));
    split_linear_factors(split, roots);
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());

    Poly reconstructed = Poly::constant(field, 1);
    for (const mpz_class& root : roots) {
        if (polynomial.evaluate(root) != 0) {
            throw std::logic_error("linear-root validation failed");
        }
        reconstructed = mul(reconstructed,
                            sub(x, Poly::constant(field, root)));
    }
    if (!equal(reconstructed.monic(), split.monic())) {
        throw std::logic_error("linear-root reconstruction failed");
    }
    return {std::move(frobenius_x), std::move(roots)};
}

}  // namespace

Poly::Poly(const Field& field) : field_(field) {}

Poly::Poly(const Field& field, std::vector<mpz_class> coefficients)
    : field_(field), coefficients_(std::move(coefficients)) {
    for (auto& coefficient : coefficients_) {
        coefficient = field_.normalize(coefficient);
    }
    trim();
}

Poly::Poly(const Field& field, std::vector<mpz_class> coefficients,
           NormalizedCoefficientsTag)
    : field_(field), coefficients_(std::move(coefficients)) {
    trim();
}

Poly Poly::constant(const Field& field, const mpz_class& value) {
    return Poly(field, {value});
}

Poly Poly::x(const Field& field) {
    return Poly(field, {0, 1});
}

int Poly::degree() const {
    return coefficients_.empty() ? -1 : static_cast<int>(coefficients_.size() - 1U);
}

bool Poly::is_zero() const {
    return coefficients_.empty();
}

bool Poly::is_one() const {
    return degree() == 0 && coefficients_[0] == 1;
}

mpz_class Poly::coefficient(std::size_t index) const {
    return index < coefficients_.size() ? coefficients_[index] : mpz_class(0);
}

mpz_class Poly::leading_coefficient() const {
    if (is_zero()) {
        throw std::domain_error("zero polynomial has no leading coefficient");
    }
    return coefficients_.back();
}

mpz_class Poly::evaluate(const mpz_class& value) const {
    mpz_class result = 0;
    for (auto it = coefficients_.rbegin(); it != coefficients_.rend(); ++it) {
        result = field_.add(field_.mul(result, value), *it);
    }
    return result;
}

Poly Poly::derivative() const {
    if (degree() <= 0) {
        return Poly(field_);
    }
    std::vector<mpz_class> output(coefficients_.size() - 1U);
    for (std::size_t i = 1; i < coefficients_.size(); ++i) {
        output[i - 1U] = field_.mul(coefficients_[i], mpz_class(i));
    }
    return Poly(field_, std::move(output), NormalizedCoefficientsTag{});
}

Poly Poly::monic() const {
    if (is_zero()) {
        return *this;
    }
    return scalar_mul(*this, field_.inverse(leading_coefficient()));
}

void Poly::trim() {
    while (!coefficients_.empty() && coefficients_.back() == 0) {
        coefficients_.pop_back();
    }
}

Poly add(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    const std::size_t size = std::max(lhs.coefficients().size(), rhs.coefficients().size());
    std::vector<mpz_class> output(size);
    for (std::size_t i = 0; i < size; ++i) {
        output[i] = lhs.field().add(lhs.coefficient(i), rhs.coefficient(i));
    }
    return Poly(lhs.field(), std::move(output),
                Poly::NormalizedCoefficientsTag{});
}

Poly sub(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    const std::size_t size = std::max(lhs.coefficients().size(), rhs.coefficients().size());
    std::vector<mpz_class> output(size);
    for (std::size_t i = 0; i < size; ++i) {
        output[i] = lhs.field().sub(lhs.coefficient(i), rhs.coefficient(i));
    }
    return Poly(lhs.field(), std::move(output),
                Poly::NormalizedCoefficientsTag{});
}

Poly neg(const Poly& value) {
    std::vector<mpz_class> output = value.coefficients();
    for (auto& coefficient : output) {
        coefficient = value.field().neg(coefficient);
    }
    return Poly(value.field(), std::move(output),
                Poly::NormalizedCoefficientsTag{});
}

Poly mul(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    if (lhs.is_zero() || rhs.is_zero()) {
        return Poly(lhs.field());
    }
    std::vector<mpz_class> output = coefficient_product(
        lhs.coefficients(), rhs.coefficients(), lhs.field());
    return Poly(lhs.field(), std::move(output));
}

Poly scalar_mul(const Poly& value, const mpz_class& scalar) {
    std::vector<mpz_class> output = value.coefficients();
    for (auto& coefficient : output) {
        coefficient = value.field().mul(coefficient, scalar);
    }
    return Poly(value.field(), std::move(output),
                Poly::NormalizedCoefficientsTag{});
}

std::pair<Poly, Poly> divmod(const Poly& numerator, const Poly& denominator) {
    require_same_field(numerator, denominator);
    if (denominator.is_zero()) {
        throw std::domain_error("polynomial division by zero");
    }
    if (numerator.degree() < denominator.degree()) {
        return {Poly(numerator.field()), numerator};
    }
    const Field& field = numerator.field();
    std::vector<mpz_class> remainder = numerator.coefficients();
    const std::vector<mpz_class>& denominator_coefficients =
        denominator.coefficients();
    std::vector<mpz_class> quotient(
        static_cast<std::size_t>(numerator.degree() - denominator.degree() + 1), 0);
    const std::size_t denominator_degree =
        static_cast<std::size_t>(denominator.degree());
    const bool monic = denominator.leading_coefficient() == 1;
    const mpz_class inverse_lead =
        monic ? mpz_class(1) : field.inverse(denominator.leading_coefficient());
    while (remainder.size() > denominator_degree) {
        const std::size_t shift = remainder.size() - denominator_degree - 1U;
        const mpz_class factor = monic
            ? remainder.back()
            : field.mul(remainder.back(), inverse_lead);
        quotient[shift] = factor;
        for (std::size_t index = 0; index <= denominator_degree; ++index) {
            remainder[shift + index] = field.sub(
                remainder[shift + index],
                field.mul(factor, denominator_coefficients[index]));
        }
        while (!remainder.empty() && remainder.back() == 0) {
            remainder.pop_back();
        }
    }
    return {Poly(field, std::move(quotient),
                 Poly::NormalizedCoefficientsTag{}),
            Poly(field, std::move(remainder),
                 Poly::NormalizedCoefficientsTag{})};
}

Poly mod(const Poly& numerator, const Poly& denominator) {
    return divmod(numerator, denominator).second;
}

namespace {

void reduce_product_coefficients(std::vector<mpz_class>& coefficients,
                                 const Poly& modulus) {
    const Field& field = modulus.field();
    const std::vector<mpz_class>& modulus_coefficients =
        modulus.coefficients();
    const std::size_t modulus_degree =
        static_cast<std::size_t>(modulus.degree());
    const bool monic = modulus.leading_coefficient() == 1;
    const mpz_class inverse_lead =
        monic ? mpz_class(1) : field.inverse(modulus.leading_coefficient());
    for (std::size_t degree = coefficients.size(); degree-- > modulus_degree;) {
        if (coefficients[degree] == 0) {
            continue;
        }
        // Keep the intermediate remainder coefficients unreduced while
        // eliminating high terms.  Reducing every multiply/subtract here
        // performs O(n^2) divisions by p in the modular-powering hot path;
        // mpz_submul can accumulate the exact integer representative and each
        // coefficient only needs normalization when it becomes a pivot (or
        // once at the end).
        const mpz_class factor =
            monic ? field.normalize(coefficients[degree])
                  : field.mul(coefficients[degree], inverse_lead);
        const std::size_t shift = degree - modulus_degree;
        for (std::size_t index = 0; index < modulus_degree; ++index) {
            mpz_submul(coefficients[shift + index].get_mpz_t(),
                       factor.get_mpz_t(),
                       modulus_coefficients[index].get_mpz_t());
        }
        coefficients[degree] = 0;
    }
    coefficients.resize(modulus_degree);
    for (mpz_class& coefficient : coefficients) {
        coefficient = field.normalize(coefficient);
    }
}

const Poly& reduced_operand(const Poly& value, const Poly& modulus,
                            Poly& storage) {
    if (value.degree() >= modulus.degree()) {
        storage = mod(value, modulus);
        return storage;
    }
    return value;
}

}  // namespace

// Reusable quotient-ring arithmetic.  For sufficiently large monic moduli,
// reverse-polynomial division replaces the quadratic high-coefficient
// elimination loop by two balanced coefficient convolutions.  The inverse is
// bound to an owned copy of the modulus and can be shared by all arithmetic in
// one SEA kernel quotient.
PolyModContext::PolyModContext(const Poly& modulus) : modulus_(modulus) {
    if (kReciprocalReductionDegreeThreshold == 0U ||
        modulus_.degree() <= 0 || modulus_.leading_coefficient() != 1) {
        return;
    }
    const std::size_t degree = static_cast<std::size_t>(modulus_.degree());
    if (degree < kReciprocalReductionDegreeThreshold) {
        return;
    }

    // If M is monic of degree d, reverse(M) has constant coefficient one.
    // Compute its inverse modulo x^d by the exact coefficient recurrence.
    reciprocal_.assign(degree, 0);
    reciprocal_[0] = 1;
    const Field& field = modulus_.field();
    const std::vector<mpz_class>& coefficients = modulus_.coefficients();
    for (std::size_t output_degree = 1U; output_degree < degree;
         ++output_degree) {
        mpz_class accumulated = 0;
        for (std::size_t input_degree = 1U;
             input_degree <= output_degree; ++input_degree) {
            const mpz_class& reversed_modulus_coefficient =
                coefficients[degree - input_degree];
            if (reversed_modulus_coefficient != 0 &&
                reciprocal_[output_degree - input_degree] != 0) {
                mpz_addmul(
                    accumulated.get_mpz_t(),
                    reversed_modulus_coefficient.get_mpz_t(),
                    reciprocal_[output_degree - input_degree].get_mpz_t());
            }
        }
        reciprocal_[output_degree] = field.normalize(-accumulated);
    }
}

Poly PolyModContext::reduce(const Poly& value) const {
    require_same_field(value, modulus_);
    return mod(value, modulus_);
}

Poly PolyModContext::multiply(const Poly& lhs, const Poly& rhs) const {
    if (reciprocal_.empty()) {
        return mulmod(lhs, rhs, modulus_);
    }
    require_same_field(lhs, rhs);
    require_same_field(lhs, modulus_);
    Poly lhs_storage(lhs.field());
    Poly rhs_storage(lhs.field());
    const Poly& reduced_lhs = reduced_operand(lhs, modulus_, lhs_storage);
    const Poly& reduced_rhs = reduced_operand(rhs, modulus_, rhs_storage);
    if (reduced_lhs.is_zero() || reduced_rhs.is_zero()) {
        return Poly(lhs.field());
    }
    std::vector<mpz_class> output = coefficient_product(
        reduced_lhs.coefficients(), reduced_rhs.coefficients(), lhs.field());
    reduce_coefficients(output);
    return Poly(lhs.field(), std::move(output),
                Poly::NormalizedCoefficientsTag{});
}

Poly PolyModContext::square(const Poly& value) const {
    if (reciprocal_.empty()) {
        return squaremod(value, modulus_);
    }
    require_same_field(value, modulus_);
    Poly storage(value.field());
    const Poly& reduced = reduced_operand(value, modulus_, storage);
    if (reduced.is_zero()) {
        return Poly(value.field());
    }
    std::vector<mpz_class> output =
        coefficient_square(reduced.coefficients(), value.field());
    reduce_coefficients(output);
    return Poly(value.field(), std::move(output),
                Poly::NormalizedCoefficientsTag{});
}

void PolyModContext::reduce_coefficients(
    std::vector<mpz_class>& coefficients) const {
    const std::size_t modulus_degree =
        static_cast<std::size_t>(modulus_.degree());
    if (coefficients.size() <= modulus_degree ||
        coefficients.size() > 2U * modulus_degree - 1U) {
        reduce_product_coefficients(coefficients, modulus_);
        return;
    }

    const Field& field = modulus_.field();
    const std::size_t quotient_size = coefficients.size() - modulus_degree;
    if (quotient_size > reciprocal_.size()) {
        throw std::logic_error(
            "reciprocal quotient exceeds its prepared precision");
    }

    // Fast monic division: reverse the high part of C, multiply by
    // reverse(M)^-1 modulo x^k, then reverse the first k coefficients to
    // obtain the quotient.  Raw convolution coefficients are normalized only
    // at the F_p boundaries required by the division identity.
    std::vector<mpz_class> reversed_high(quotient_size);
    for (std::size_t index = 0U; index < quotient_size; ++index) {
        reversed_high[index] = field.normalize(
            coefficients[coefficients.size() - 1U - index]);
    }
    std::vector<mpz_class> reversed_quotient = coefficient_product(
        reversed_high,
        std::span<const mpz_class>(reciprocal_.data(), quotient_size), field);
    std::vector<mpz_class> quotient(quotient_size);
    for (std::size_t index = 0U; index < quotient_size; ++index) {
        quotient[quotient_size - 1U - index] =
            field.normalize(reversed_quotient[index]);
    }
    const std::vector<mpz_class> quotient_times_modulus =
        coefficient_product(quotient, modulus_.coefficients(), field);
    for (std::size_t index = 0U; index < modulus_degree; ++index) {
        coefficients[index] = field.normalize(
            coefficients[index] - quotient_times_modulus[index]);
    }
    coefficients.resize(modulus_degree);
}

namespace {

Poly apply_powmod_plan(const Poly& base, const PowmodPlan& plan,
                       const PolyModContext& context) {
    if (plan.exponent_is_zero) {
        return Poly::constant(base.field(), 1);
    }
    std::vector<Poly> odd_powers;
    odd_powers.reserve(
        static_cast<std::size_t>((plan.maximum_odd_power + 1U) / 2U));
    odd_powers.push_back(base);
    if (plan.maximum_odd_power > 1U) {
        const Poly base_squared = context.square(base);
        for (unsigned int odd = 3U; odd <= plan.maximum_odd_power; odd += 2U) {
            odd_powers.push_back(
                context.multiply(odd_powers.back(), base_squared));
        }
    }
    const auto table_value = [&odd_powers](unsigned int odd_power)
                                 -> const Poly& {
        return odd_powers.at(
            static_cast<std::size_t>((odd_power - 1U) / 2U));
    };
    Poly result = table_value(plan.first_odd_power);
    for (const PowmodWindowStep& step : plan.steps) {
        for (std::size_t count = 0U; count < step.squarings; ++count) {
            result = context.square(result);
        }
        result = context.multiply(result, table_value(step.odd_power));
    }
    for (std::size_t count = 0U; count < plan.trailing_squarings; ++count) {
        result = context.square(result);
    }
    return result;
}

}  // namespace

Poly PolyModContext::pow(Poly base, mpz_class exponent) const {
    require_same_field(base, modulus_);
    if (modulus_.is_zero()) {
        throw std::domain_error("polynomial modulus is zero");
    }
    if (exponent < 0) {
        throw std::invalid_argument("negative polynomial exponent");
    }
    base = reduce(base);
    return apply_powmod_plan(base, make_powmod_plan(exponent), *this);
}

namespace {

std::size_t composition_block_width(std::size_t coefficient_count) {
    if (coefficient_count == 0U) {
        throw std::invalid_argument(
            "composition coefficient bound must be positive");
    }
    std::size_t block_width = 1U;
    while (block_width < coefficient_count / block_width ||
           (block_width == coefficient_count / block_width &&
            coefficient_count % block_width != 0U)) {
        ++block_width;
    }
    return block_width;
}

std::size_t composition_block_count(std::size_t coefficient_count,
                                    std::size_t block_width) {
    return coefficient_count / block_width +
           static_cast<std::size_t>(
               coefficient_count % block_width != 0U);
}

std::vector<Poly> prepare_composition_inner_powers(
    const PolyModContext& context, const Poly& inner,
    std::size_t maximum_outer_coefficients, std::size_t block_width) {
    const Field& field = context.modulus().field();
    const std::size_t block_count = composition_block_count(
        maximum_outer_coefficients, block_width);
    const std::size_t maximum_power = block_count > 1U
        ? block_width
        : maximum_outer_coefficients - 1U;
    std::vector<Poly> inner_powers;
    inner_powers.reserve(maximum_power + 1U);
    inner_powers.push_back(Poly::constant(field, 1));
    const Poly reduced_inner = context.reduce(inner);
    for (std::size_t exponent = 1U; exponent <= maximum_power; ++exponent) {
        inner_powers.push_back(context.multiply(
            inner_powers.back(), reduced_inner));
    }
    return inner_powers;
}

Poly apply_composition_inner_powers(
    const PolyModContext& context, const Poly& outer,
    std::size_t maximum_outer_coefficients, std::size_t block_width,
    const std::vector<Poly>& inner_powers) {
    const Poly& modulus = context.modulus();
    require_same_field(outer, modulus);
    const Field& field = modulus.field();
    if (outer.coefficients().size() > maximum_outer_coefficients) {
        throw std::invalid_argument(
            "composition outer polynomial exceeds its prepared bound");
    }
    if (modulus.degree() == 0 || outer.is_zero()) {
        return Poly(field);
    }

    const std::size_t coefficient_count = outer.coefficients().size();
    const std::size_t block_count = composition_block_count(
        coefficient_count, block_width);
    Poly result(field);
    for (std::size_t block = block_count; block-- > 0U;) {
        if (!result.is_zero()) {
            result = context.multiply(result, inner_powers[block_width]);
        }
        std::vector<mpz_class> block_coefficients(
            static_cast<std::size_t>(modulus.degree()), 0);
        const std::size_t begin = block * block_width;
        const std::size_t remaining = coefficient_count - begin;
        const std::size_t end = remaining < block_width
            ? coefficient_count
            : begin + block_width;
        for (std::size_t index = begin; index < end; ++index) {
            const mpz_class& coefficient = outer.coefficients()[index];
            if (coefficient == 0) {
                continue;
            }
            const std::vector<mpz_class>& power_coefficients =
                inner_powers[index - begin].coefficients();
            for (std::size_t power_index = 0U;
                 power_index < power_coefficients.size(); ++power_index) {
                if (power_coefficients[power_index] != 0) {
                    mpz_addmul(
                        block_coefficients[power_index].get_mpz_t(),
                        power_coefficients[power_index].get_mpz_t(),
                        coefficient.get_mpz_t());
                }
            }
        }
        result = add(
            result, Poly(field, std::move(block_coefficients)));
    }
    return result;
}

}  // namespace

Poly PolyModContext::compose(const Poly& outer, const Poly& inner) const {
    require_same_field(outer, inner);
    require_same_field(outer, modulus_);
    if (modulus_.is_zero()) {
        throw std::domain_error("polynomial composition modulus is zero");
    }
    const Field& field = modulus_.field();
    if (modulus_.degree() == 0 || outer.is_zero()) {
        return Poly(field);
    }

    const std::size_t coefficient_count = outer.coefficients().size();
    const std::size_t block_width =
        composition_block_width(coefficient_count);
    const std::vector<Poly> inner_powers =
        prepare_composition_inner_powers(
            *this, inner, coefficient_count, block_width);
    return apply_composition_inner_powers(
        *this, outer, coefficient_count, block_width, inner_powers);
}

PolyModCompositionPlan PolyModContext::prepare_composition(
    const Poly& inner,
    std::size_t maximum_outer_coefficients) const {
    require_same_field(inner, modulus_);
    if (modulus_.is_zero()) {
        throw std::domain_error("polynomial composition modulus is zero");
    }
    return PolyModCompositionPlan(
        *this, inner, maximum_outer_coefficients);
}

PolyModCompositionPlan::PolyModCompositionPlan(
    PolyModContext context, const Poly& inner,
    std::size_t maximum_outer_coefficients)
    : context_(std::move(context)),
      maximum_outer_coefficients_(maximum_outer_coefficients),
      block_width_(composition_block_width(maximum_outer_coefficients)) {
    require_same_field(inner, context_.modulus());
    if (context_.modulus().is_zero()) {
        throw std::domain_error("polynomial composition modulus is zero");
    }
    if (context_.modulus().degree() > 0) {
        inner_powers_ = prepare_composition_inner_powers(
            context_, inner, maximum_outer_coefficients_, block_width_);
    }
}

Poly PolyModCompositionPlan::compose(const Poly& outer) const {
    require_same_field(outer, context_.modulus());
    return apply_composition_inner_powers(
        context_, outer, maximum_outer_coefficients_, block_width_,
        inner_powers_);
}

Poly mulmod(const Poly& lhs, const Poly& rhs, const Poly& modulus) {
    require_same_field(lhs, rhs);
    require_same_field(lhs, modulus);
    if (modulus.is_zero()) {
        throw std::domain_error("polynomial modulus is zero");
    }
    if (modulus.degree() == 0 || lhs.is_zero() || rhs.is_zero()) {
        return Poly(lhs.field());
    }
    Poly lhs_storage(lhs.field());
    Poly rhs_storage(lhs.field());
    const Poly& reduced_lhs = reduced_operand(lhs, modulus, lhs_storage);
    const Poly& reduced_rhs = reduced_operand(rhs, modulus, rhs_storage);
    if (reduced_lhs.is_zero() || reduced_rhs.is_zero()) {
        return Poly(lhs.field());
    }
    std::vector<mpz_class> output = coefficient_product(
        reduced_lhs.coefficients(), reduced_rhs.coefficients(), lhs.field());
    reduce_product_coefficients(output, modulus);
    return Poly(lhs.field(), std::move(output),
                Poly::NormalizedCoefficientsTag{});
}

Poly squaremod(const Poly& value, const Poly& modulus) {
    require_same_field(value, modulus);
    if (modulus.is_zero()) {
        throw std::domain_error("polynomial modulus is zero");
    }
    if (modulus.degree() == 0 || value.is_zero()) {
        return Poly(value.field());
    }
    Poly storage(value.field());
    const Poly& reduced = reduced_operand(value, modulus, storage);
    if (reduced.is_zero()) {
        return Poly(value.field());
    }
    std::vector<mpz_class> output =
        coefficient_square(reduced.coefficients(), value.field());
    reduce_product_coefficients(output, modulus);
    return Poly(value.field(), std::move(output),
                Poly::NormalizedCoefficientsTag{});
}

Poly gcd(Poly lhs, Poly rhs) {
    require_same_field(lhs, rhs);
    while (!rhs.is_zero()) {
        Poly remainder = mod(lhs, rhs);
        lhs = std::move(rhs);
        rhs = std::move(remainder);
    }
    return lhs.monic();
}

Poly powmod(Poly base, mpz_class exponent, const Poly& modulus) {
    return PolyModContext(modulus).pow(std::move(base), std::move(exponent));
}

int rational_root_count(const Poly& polynomial) {
    if (polynomial.is_zero()) {
        throw std::invalid_argument("zero polynomial has every field element as a root");
    }
    require_probable_prime_field(polynomial);
    const Poly xp = powmod(Poly::x(polynomial.field()), polynomial.field().modulus(), polynomial);
    const Poly split = gcd(polynomial, sub(xp, Poly::x(polynomial.field())));
    return split.degree();
}

std::vector<mpz_class> linear_roots(const Poly& polynomial) {
    return analyze_linear_roots(polynomial).roots;
}

CertifiedLinearRoots::CertifiedLinearRoots(
    std::shared_ptr<const Poly> polynomial, Poly frobenius_x,
    std::vector<mpz_class> roots)
    : polynomial_(std::move(polynomial)),
      frobenius_x_(std::move(frobenius_x)), roots_(std::move(roots)) {}

CertifiedLinearRoots certify_linear_roots(const Poly& polynomial) {
    return certify_linear_roots(std::make_shared<const Poly>(polynomial));
}

CertifiedLinearRoots certify_linear_roots(
    std::shared_ptr<const Poly> polynomial) {
    if (!polynomial) {
        throw std::invalid_argument(
            "linear-root certification received no polynomial");
    }
    LinearRootAnalysis analysis = analyze_linear_roots(*polynomial);
    return CertifiedLinearRoots(
        std::move(polynomial), std::move(analysis.frobenius_x),
        std::move(analysis.roots));
}

bool equal(const Poly& lhs, const Poly& rhs) {
    require_same_field(lhs, rhs);
    return lhs.coefficients() == rhs.coefficients();
}

}  // namespace oneshotsea
