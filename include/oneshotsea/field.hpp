#pragma once

#include <gmpxx.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace oneshotsea {

mpz_class parse_integer(const std::string& text);

class Field {
public:
    explicit Field(mpz_class modulus);

    const mpz_class& modulus() const { return modulus_; }
    mpz_class normalize(const mpz_class& value) const;
    mpz_class add(const mpz_class& lhs, const mpz_class& rhs) const;
    mpz_class sub(const mpz_class& lhs, const mpz_class& rhs) const;
    mpz_class neg(const mpz_class& value) const;
    mpz_class mul(const mpz_class& lhs, const mpz_class& rhs) const;
    mpz_class square(const mpz_class& value) const;
    mpz_class pow(const mpz_class& base, const mpz_class& exponent) const;
    mpz_class inverse(const mpz_class& value) const;
    mpz_class divide(const mpz_class& numerator, const mpz_class& denominator) const;
    int legendre(const mpz_class& value) const;

private:
    mpz_class modulus_;

    bool is_normalized(const mpz_class& value) const;
};

std::uint64_t splitmix64(std::uint64_t value);
mpz_class deterministic_residue(const Field& field, std::uint64_t seed,
                                std::uint64_t index, std::uint64_t domain);

}  // namespace oneshotsea
