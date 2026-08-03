#include "oneshotsea/field.hpp"

#include <algorithm>
#include <array>

namespace oneshotsea {
namespace {

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

}  // namespace

mpz_class parse_integer(const std::string& text) {
    mpz_class value;
    if (value.set_str(text, 0) != 0) {
        throw std::invalid_argument("invalid integer: " + text);
    }
    return value;
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

Field::Field(mpz_class modulus) : modulus_(std::move(modulus)) {
    if (modulus_ <= 2 || mpz_even_p(modulus_.get_mpz_t()) != 0) {
        throw std::invalid_argument("field modulus must be an odd integer greater than 2");
    }
}

bool Field::is_normalized(const mpz_class& value) const {
    return value >= 0 && value < modulus_;
}

mpz_class Field::normalize(const mpz_class& value) const {
    if (is_normalized(value)) {
        return value;
    }
    mpz_class result;
    mpz_mod(result.get_mpz_t(), value.get_mpz_t(), modulus_.get_mpz_t());
    return result;
}

mpz_class Field::add(const mpz_class& lhs, const mpz_class& rhs) const {
    if (is_normalized(lhs) && is_normalized(rhs)) {
        mpz_class result = lhs + rhs;
        if (result >= modulus_) {
            result -= modulus_;
        }
        return result;
    }
    return normalize(lhs + rhs);
}

mpz_class Field::sub(const mpz_class& lhs, const mpz_class& rhs) const {
    if (is_normalized(lhs) && is_normalized(rhs)) {
        if (lhs >= rhs) {
            return lhs - rhs;
        }
        return lhs + modulus_ - rhs;
    }
    return normalize(lhs - rhs);
}

mpz_class Field::neg(const mpz_class& value) const {
    if (is_normalized(value)) {
        return value == 0 ? mpz_class(0) : modulus_ - value;
    }
    return normalize(-value);
}

mpz_class Field::mul(const mpz_class& lhs, const mpz_class& rhs) const {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    if (lhs == 1) {
        return normalize(rhs);
    }
    if (rhs == 1) {
        return normalize(lhs);
    }
    mpz_class result;
    mpz_mul(result.get_mpz_t(), lhs.get_mpz_t(), rhs.get_mpz_t());
    mpz_mod(result.get_mpz_t(), result.get_mpz_t(), modulus_.get_mpz_t());
    return result;
}

mpz_class Field::square(const mpz_class& value) const {
    if (value == 0 || value == 1) {
        return normalize(value);
    }
    mpz_class result;
    mpz_mul(result.get_mpz_t(), value.get_mpz_t(), value.get_mpz_t());
    mpz_mod(result.get_mpz_t(), result.get_mpz_t(), modulus_.get_mpz_t());
    return result;
}

mpz_class Field::pow(const mpz_class& base, const mpz_class& exponent) const {
    if (exponent < 0) {
        throw std::invalid_argument("negative finite-field exponent");
    }
    mpz_class result;
    const mpz_class reduced = normalize(base);
    mpz_powm(result.get_mpz_t(), reduced.get_mpz_t(), exponent.get_mpz_t(),
             modulus_.get_mpz_t());
    return result;
}

mpz_class Field::inverse(const mpz_class& value) const {
    mpz_class result;
    const mpz_class reduced = normalize(value);
    if (mpz_invert(result.get_mpz_t(), reduced.get_mpz_t(), modulus_.get_mpz_t()) == 0) {
        throw std::domain_error("non-invertible finite-field element");
    }
    return result;
}

mpz_class Field::divide(const mpz_class& numerator, const mpz_class& denominator) const {
    return mul(numerator, inverse(denominator));
}

int Field::legendre(const mpz_class& value) const {
    return mpz_legendre(normalize(value).get_mpz_t(), modulus_.get_mpz_t());
}

std::uint64_t splitmix64(std::uint64_t value) {
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

mpz_class deterministic_residue(const Field& field, std::uint64_t seed,
                                std::uint64_t index, std::uint64_t domain) {
    const std::size_t bits = mpz_sizeinbase(field.modulus().get_mpz_t(), 2);
    const std::size_t limbs = std::max<std::size_t>(1, (bits + 63U) / 64U);
    mpz_class value = 0;
    std::uint64_t state = splitmix64(seed ^ splitmix64(index) ^ splitmix64(domain));
    for (std::size_t limb = 0; limb < limbs + 1U; ++limb) {
        state = splitmix64(state ^ static_cast<std::uint64_t>(limb) ^ domain);
        mpz_mul_2exp(value.get_mpz_t(), value.get_mpz_t(), 64U);
        mpz_class word;
        mpz_import(word.get_mpz_t(), 1, 1, sizeof(state), 0, 0, &state);
        value += word;
    }
    return field.normalize(value);
}

mpz_class least_quadratic_nonsquare(const Field& field) {
    if (mpz_probab_prime_p(field.modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "quadratic-nonsquare search requires prime characteristic");
    }
    for (mpz_class candidate = 2; candidate < field.modulus(); ++candidate) {
        if (field.legendre(candidate) == -1) {
            return candidate;
        }
    }
    throw std::logic_error("prime field has no quadratic nonsquare");
}

}  // namespace oneshotsea
