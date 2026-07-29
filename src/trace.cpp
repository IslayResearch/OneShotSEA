#include "oneshotsea/trace.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace oneshotsea {
namespace {

std::uint64_t mul_mod(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t modulus) {
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(lhs) * static_cast<unsigned __int128>(rhs)) % modulus);
}

std::uint64_t pow_mod(std::uint64_t base, std::uint64_t exponent, std::uint64_t modulus) {
    std::uint64_t result = 1;
    while (exponent != 0) {
        if ((exponent & 1U) != 0U) {
            result = mul_mod(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0) {
            base = mul_mod(base, base, modulus);
        }
    }
    return result;
}

int legendre_small(std::uint64_t value, std::uint64_t prime) {
    value %= prime;
    if (value == 0) {
        return 0;
    }
    const std::uint64_t symbol = pow_mod(value, (prime - 1U) / 2U, prime);
    if (symbol == 1) {
        return 1;
    }
    if (symbol == prime - 1U) {
        return -1;
    }
    throw std::invalid_argument("ell must be prime");
}

mpz_class first_congruent_at_least(const mpz_class& lower, const mpz_class& residue,
                                   const mpz_class& modulus) {
    mpz_class quotient;
    const mpz_class numerator = lower - residue;
    mpz_cdiv_q(quotient.get_mpz_t(), numerator.get_mpz_t(), modulus.get_mpz_t());
    return residue + quotient * modulus;
}

}  // namespace

TraceConstraints::TraceConstraints(mpz_class prime)
    : prime_(std::move(prime)), modulus_(1), residues_{mpz_class(0)} {
    if (prime_ <= 3 || mpz_even_p(prime_.get_mpz_t()) != 0) {
        throw std::invalid_argument("trace constraints require an odd p greater than 3");
    }
    const mpz_class four_p = 4 * prime_;
    mpz_sqrt(hasse_radius_.get_mpz_t(), four_p.get_mpz_t());
}

void TraceConstraints::refine(std::uint64_t ell,
                              const std::vector<std::uint64_t>& allowed) {
    if (ell < 2) {
        throw std::invalid_argument("invalid SEA modulus");
    }
    if (ell > std::numeric_limits<unsigned long>::max()) {
        throw std::invalid_argument("SEA modulus does not fit unsigned long");
    }
    const mpz_class ell_integer(static_cast<unsigned long>(ell));
    mpz_class common_divisor;
    mpz_gcd(common_divisor.get_mpz_t(), modulus_.get_mpz_t(), ell_integer.get_mpz_t());
    if (common_divisor != 1) {
        throw std::invalid_argument("SEA modulus is not coprime to accumulated modulus");
    }
    std::vector<std::uint64_t> canonical_allowed;
    canonical_allowed.reserve(allowed.size());
    for (const std::uint64_t residue : allowed) {
        canonical_allowed.push_back(residue % ell);
    }
    std::sort(canonical_allowed.begin(), canonical_allowed.end());
    canonical_allowed.erase(
        std::unique(canonical_allowed.begin(), canonical_allowed.end()),
        canonical_allowed.end());

    mpz_class inverse;
    const mpz_class modulus_mod_ell = modulus_ % ell_integer;
    if (mpz_invert(inverse.get_mpz_t(), modulus_mod_ell.get_mpz_t(),
                   ell_integer.get_mpz_t()) == 0) {
        throw std::logic_error("CRT inverse does not exist");
    }
    const mpz_class new_modulus = modulus_ * ell_integer;
    std::set<mpz_class> merged;
    for (const mpz_class& old_residue : residues_) {
        for (const std::uint64_t small_residue : canonical_allowed) {
            mpz_class delta = mpz_class(static_cast<unsigned long>(small_residue)) - old_residue;
            mpz_mod(delta.get_mpz_t(), delta.get_mpz_t(), ell_integer.get_mpz_t());
            mpz_class step = delta * inverse;
            mpz_mod(step.get_mpz_t(), step.get_mpz_t(), ell_integer.get_mpz_t());
            mpz_class combined = old_residue + modulus_ * step;
            mpz_mod(combined.get_mpz_t(), combined.get_mpz_t(), new_modulus.get_mpz_t());
            merged.insert(std::move(combined));
        }
    }
    modulus_ = new_modulus;
    residues_.assign(merged.begin(), merged.end());
}

mpz_class TraceConstraints::candidate_count() const {
    const mpz_class lower = -hasse_radius_;
    const mpz_class upper = hasse_radius_;
    mpz_class count = 0;
    for (const mpz_class& residue : residues_) {
        const mpz_class first = first_congruent_at_least(lower, residue, modulus_);
        if (first > upper) {
            continue;
        }
        mpz_class quotient;
        const mpz_class width = upper - first;
        mpz_fdiv_q(quotient.get_mpz_t(), width.get_mpz_t(), modulus_.get_mpz_t());
        count += quotient + 1;
    }
    return count;
}

std::optional<std::vector<mpz_class>> TraceConstraints::enumerate(std::size_t cap) const {
    const mpz_class count = candidate_count();
    if (!mpz_fits_ulong_p(count.get_mpz_t()) || count.get_ui() > cap) {
        return std::nullopt;
    }
    std::vector<mpz_class> traces;
    traces.reserve(count.get_ui());
    const mpz_class lower = -hasse_radius_;
    const mpz_class upper = hasse_radius_;
    for (const mpz_class& residue : residues_) {
        for (mpz_class trace = first_congruent_at_least(lower, residue, modulus_);
             trace <= upper; trace += modulus_) {
            traces.push_back(trace);
        }
    }
    std::sort(traces.begin(), traces.end());
    return traces;
}

std::vector<std::uint64_t> trace_residues_from_classification(
    std::uint64_t ell, const mpz_class& prime, bool has_rational_isogeny) {
    if (ell < 3 || (ell & 1U) == 0U) {
        throw std::invalid_argument("classification requires an odd prime ell");
    }
    const std::uint64_t p_mod_ell = mpz_fdiv_ui(prime.get_mpz_t(), ell);
    std::vector<std::uint64_t> residues;
    for (std::uint64_t trace = 0; trace < ell; ++trace) {
        const std::uint64_t trace_squared = mul_mod(trace, trace, ell);
        const std::uint64_t four_p = mul_mod(4U % ell, p_mod_ell, ell);
        const std::uint64_t discriminant = (trace_squared + ell - four_p) % ell;
        const int symbol = legendre_small(discriminant, ell);
        if ((has_rational_isogeny && symbol >= 0) ||
            (!has_rational_isogeny && symbol < 0)) {
            residues.push_back(trace);
        }
    }
    return residues;
}

}  // namespace oneshotsea
