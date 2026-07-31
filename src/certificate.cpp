#include "oneshotsea/certificate.hpp"

#include "oneshotsea/field.hpp"
#include "oneshotsea/poly.hpp"

extern "C" {
#include "../third_party/oneshot_fast_ecpp/smooth.h"
}

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace oneshotsea {
namespace {

// The x-only formulas and assembly flow are adapted from ecpp/curve.c in
// AndrewVSutherland2/OneShotFastECPP at commit 88da82f (MIT).  Arithmetic is
// expressed through OneShotSEA's GMP-backed Field rather than the upstream
// fixed-limb and Cornacchia contexts.  See THIRD_PARTY_NOTICES.md.
constexpr std::size_t kFactorBatchCapacity = 64;
constexpr std::uint64_t kAssemblyDomain = UINT64_C(0x636572746173736d);

struct SmoothFactorization {
    std::vector<std::uint64_t> primes;
    std::vector<int> exponents;
};

mpz_class integer_from_u64(std::uint64_t value) {
    mpz_class result;
    mpz_import(result.get_mpz_t(), 1, 1, sizeof(value), 0, 0, &value);
    return result;
}

bool checked_n_bounds(const mpz_class& prime, unsigned long& bits,
                      std::uint64_t& n2, std::uint64_t& n4) {
    if (prime <= 0) {
        return false;
    }
    const std::size_t bit_count = mpz_sizeinbase(prime.get_mpz_t(), 2);
    if (bit_count > std::numeric_limits<unsigned long>::max()) {
        return false;
    }
    bits = static_cast<unsigned long>(bit_count);
    const std::uint64_t n = static_cast<std::uint64_t>(bits);
    if (n != 0 && n > std::numeric_limits<std::uint64_t>::max() / n) {
        return false;
    }
    n2 = n * n;
    if (n2 != 0 && n2 > std::numeric_limits<std::uint64_t>::max() / n2) {
        return false;
    }
    n4 = n2 * n2;
    return true;
}

std::optional<SmoothFactorization> factor_known_smooth(
    const mpz_class& value, std::uint64_t bound) {
    if (value <= 1) {
        return std::nullopt;
    }
    mpz_class remaining = value;
    SmoothFactorization result;
    while (remaining > 1) {
        std::array<std::uint64_t, kFactorBatchCapacity> primes{};
        std::array<int, kFactorBatchCapacity> ignored_exponents{};
        const int count = factor_smooth(
            remaining.get_mpz_t(), primes.data(), ignored_exponents.data(),
            static_cast<int>(primes.size()));
        if (count <= 0 || count > static_cast<int>(primes.size())) {
            return std::nullopt;
        }
        bool made_progress = false;
        for (int index = 0; index < count; ++index) {
            const std::uint64_t prime =
                primes[static_cast<std::size_t>(index)];
            if (prime < 2 || prime > bound) {
                return std::nullopt;
            }
            const mpz_class prime_z = integer_from_u64(prime);
            if (mpz_probab_prime_p(prime_z.get_mpz_t(), 25) == 0) {
                return std::nullopt;
            }
            int exponent = 0;
            while (mpz_divisible_p(remaining.get_mpz_t(),
                                   prime_z.get_mpz_t()) != 0) {
                mpz_divexact(remaining.get_mpz_t(), remaining.get_mpz_t(),
                             prime_z.get_mpz_t());
                if (exponent == std::numeric_limits<int>::max()) {
                    return std::nullopt;
                }
                ++exponent;
            }
            if (exponent != 0) {
                result.primes.push_back(prime);
                result.exponents.push_back(exponent);
                made_progress = true;
            }
        }
        if (!made_progress) {
            return std::nullopt;
        }
    }

    std::vector<std::size_t> permutation(result.primes.size());
    for (std::size_t index = 0; index < permutation.size(); ++index) {
        permutation[index] = index;
    }
    std::sort(permutation.begin(), permutation.end(),
              [&](std::size_t left, std::size_t right) {
                  return result.primes[left] < result.primes[right];
              });
    SmoothFactorization sorted;
    sorted.primes.reserve(result.primes.size());
    sorted.exponents.reserve(result.exponents.size());
    for (const std::size_t index : permutation) {
        if (!sorted.primes.empty() &&
            sorted.primes.back() == result.primes[index]) {
            return std::nullopt;
        }
        sorted.primes.push_back(result.primes[index]);
        sorted.exponents.push_back(result.exponents[index]);
    }
    return sorted;
}

MontgomeryXZ xdbl(const Field& field, const mpz_class& coefficient,
                   const MontgomeryXZ& point) {
    const mpz_class xx = field.square(point.x);
    const mpz_class zz = field.square(point.z);
    const mpz_class xz = field.mul(point.x, point.z);
    const mpz_class difference = field.sub(xx, zz);
    const mpz_class x2 = field.square(difference);
    const mpz_class inner = field.add(field.add(xx, field.mul(coefficient, xz)), zz);
    const mpz_class z2 = field.mul(4, field.mul(xz, inner));
    return {x2, z2};
}

MontgomeryXZ xadd(const Field& field, const MontgomeryXZ& left,
                  const MontgomeryXZ& right,
                  const MontgomeryXZ& difference) {
    const mpz_class a = field.mul(field.sub(left.x, left.z),
                                  field.add(right.x, right.z));
    const mpz_class b = field.mul(field.add(left.x, left.z),
                                  field.sub(right.x, right.z));
    return {
        field.mul(difference.z, field.square(field.add(a, b))),
        field.mul(difference.x, field.square(field.sub(a, b))),
    };
}

bool is_genuine_infinity(const MontgomeryXZ& point, const mpz_class& modulus) {
    mpz_class gcd;
    mpz_gcd(gcd.get_mpz_t(), point.x.get_mpz_t(), modulus.get_mpz_t());
    return point.z == 0 && gcd == 1;
}

bool has_unit_z(const MontgomeryXZ& point, const mpz_class& modulus) {
    mpz_class gcd;
    mpz_gcd(gcd.get_mpz_t(), point.z.get_mpz_t(), modulus.get_mpz_t());
    return gcd == 1;
}

bool check_order_tree(const Field& field, const mpz_class& coefficient,
                      const MontgomeryXZ& point,
                      const std::vector<std::uint64_t>& primes,
                      std::size_t begin, std::size_t end) {
    const std::size_t count = end - begin;
    if (count == 0) {
        return true;
    }
    if (count == 1) {
        return has_unit_z(point, field.modulus());
    }
    const std::size_t middle = begin + count / 2;
    mpz_class left_product = 1;
    mpz_class right_product = 1;
    for (std::size_t index = begin; index < middle; ++index) {
        left_product *= integer_from_u64(primes[index]);
    }
    for (std::size_t index = middle; index < end; ++index) {
        right_product *= integer_from_u64(primes[index]);
    }
    const MontgomeryXZ left_point = montgomery_ladder(
        field.modulus(), coefficient, left_product, point);
    const MontgomeryXZ right_point = montgomery_ladder(
        field.modulus(), coefficient, right_product, point);
    return check_order_tree(field, coefficient, left_point, primes, middle, end) &&
           check_order_tree(field, coefficient, right_point, primes, begin, middle);
}

bool validate_distinct_factors(const mpz_class& order,
                               const std::vector<std::uint64_t>& primes,
                               mpz_class& radical) {
    if (order <= 1 || primes.empty()) {
        return false;
    }
    radical = 1;
    std::uint64_t previous = 0;
    for (const std::uint64_t prime : primes) {
        if (prime <= previous || prime < 2 ||
            mpz_divisible_ui_p(order.get_mpz_t(),
                               static_cast<unsigned long>(prime)) == 0) {
            return false;
        }
        const mpz_class prime_z = integer_from_u64(prime);
        if (mpz_probab_prime_p(prime_z.get_mpz_t(), 25) == 0) {
            return false;
        }
        radical *= prime_z;
        previous = prime;
    }
    return mpz_divisible_p(order.get_mpz_t(), radical.get_mpz_t()) != 0;
}

unsigned long valuation(mpz_class value, std::uint64_t prime) {
    unsigned long exponent = 0;
    while (mpz_divisible_ui_p(value.get_mpz_t(),
                              static_cast<unsigned long>(prime)) != 0) {
        mpz_divexact_ui(value.get_mpz_t(), value.get_mpz_t(),
                        static_cast<unsigned long>(prime));
        ++exponent;
    }
    return exponent;
}

mpz_class integer_power(std::uint64_t base, unsigned long exponent) {
    const mpz_class base_z = integer_from_u64(base);
    mpz_class result;
    mpz_pow_ui(result.get_mpz_t(), base_z.get_mpz_t(), exponent);
    return result;
}

// Project a random point to exact order m without assuming the group is cyclic.
// N/m can over-project a desired q-primary component when q divides both cyclic
// factors of E(F_p).  Instead remove every prime outside m, measure each retained
// q-component, and trim only the excess above v_q(m).
std::optional<MontgomeryXZ> project_to_candidate_order(
    const Field& field, const mpz_class& coefficient,
    const MontgomeryXZ& input, const CertificateCandidate& candidate) {
    std::vector<unsigned long> order_exponents;
    std::vector<unsigned long> target_exponents;
    std::vector<mpz_class> primary_powers;
    order_exponents.reserve(candidate.distinct_prime_divisors.size());
    target_exponents.reserve(candidate.distinct_prime_divisors.size());
    primary_powers.reserve(candidate.distinct_prime_divisors.size());
    mpz_class retained_primary_bound = 1;
    for (const std::uint64_t prime : candidate.distinct_prime_divisors) {
        const unsigned long order_exponent = valuation(candidate.order, prime);
        const unsigned long target_exponent =
            valuation(candidate.point_order, prime);
        if (target_exponent == 0 || order_exponent < target_exponent) {
            return std::nullopt;
        }
        const mpz_class power = integer_power(prime, order_exponent);
        retained_primary_bound *= power;
        order_exponents.push_back(order_exponent);
        target_exponents.push_back(target_exponent);
        primary_powers.push_back(power);
    }
    if (candidate.order % retained_primary_bound != 0) {
        return std::nullopt;
    }
    MontgomeryXZ projected = montgomery_ladder(
        field.modulus(), coefficient,
        candidate.order / retained_primary_bound, input);
    if (!has_unit_z(projected, field.modulus())) {
        return std::nullopt;
    }

    mpz_class adjustment = 1;
    for (std::size_t index = 0;
         index < candidate.distinct_prime_divisors.size(); ++index) {
        MontgomeryXZ component = montgomery_ladder(
            field.modulus(), coefficient,
            retained_primary_bound / primary_powers[index], projected);
        unsigned long component_exponent = 0;
        if (!is_genuine_infinity(component, field.modulus())) {
            for (component_exponent = 1;
                 component_exponent <= order_exponents[index];
                 ++component_exponent) {
                component = montgomery_ladder(
                    field.modulus(), coefficient,
                    integer_from_u64(candidate.distinct_prime_divisors[index]),
                    component);
                if (is_genuine_infinity(component, field.modulus())) {
                    break;
                }
            }
            if (component_exponent > order_exponents[index]) {
                return std::nullopt;
            }
        }
        if (component_exponent < target_exponents[index]) {
            return std::nullopt;
        }
        adjustment *= integer_power(
            candidate.distinct_prime_divisors[index],
            component_exponent - target_exponents[index]);
    }
    projected = montgomery_ladder(
        field.modulus(), coefficient, adjustment, projected);
    if (!has_unit_z(projected, field.modulus())) {
        return std::nullopt;
    }
    return projected;
}

std::optional<mpz_class> sqrt_mod_prime(const Field& field,
                                        const mpz_class& value) {
    const mpz_class a = field.normalize(value);
    if (a == 0) {
        return mpz_class(0);
    }
    if (field.legendre(a) != 1) {
        return std::nullopt;
    }
    const mpz_class& p = field.modulus();
    if (mpz_fdiv_ui(p.get_mpz_t(), 4) == 3) {
        return field.pow(a, (p + 1) / 4);
    }

    mpz_class q = p - 1;
    unsigned long s = 0;
    while (mpz_even_p(q.get_mpz_t()) != 0) {
        q /= 2;
        ++s;
    }
    mpz_class nonresidue = 2;
    while (field.legendre(nonresidue) != -1) {
        ++nonresidue;
    }
    mpz_class c = field.pow(nonresidue, q);
    mpz_class root = field.pow(a, (q + 1) / 2);
    mpz_class t = field.pow(a, q);
    unsigned long m = s;
    while (t != 1) {
        mpz_class square = t;
        unsigned long index = 0;
        for (index = 1; index < m; ++index) {
            square = field.square(square);
            if (square == 1) {
                break;
            }
        }
        if (index == m) {
            return std::nullopt;
        }
        mpz_class exponent = 1;
        mpz_mul_2exp(exponent.get_mpz_t(), exponent.get_mpz_t(),
                     m - index - 1);
        const mpz_class b = field.pow(c, exponent);
        root = field.mul(root, b);
        c = field.square(b);
        t = field.mul(t, c);
        m = index;
    }
    return root;
}

int desired_legendre(MontgomerySide side) {
    if (side == MontgomerySide::curve) {
        return 1;
    }
    if (side == MontgomerySide::twist) {
        return -1;
    }
    return 0;
}

CertificateCandidate candidate_from_factor_exponents(
    const mpz_class& prime, const mpz_class& order,
    const mpz_class& smooth_part, const mpz_class& point_order,
    const SmoothFactorization& smooth_factorization,
    const std::vector<int>& selected_exponents, std::uint64_t n2,
    std::uint64_t n4) {
    std::vector<std::uint64_t> distinct;
    std::vector<std::uint64_t> large;
    distinct.reserve(smooth_factorization.primes.size());
    large.reserve(smooth_factorization.primes.size());
    for (std::size_t index = 0;
         index < smooth_factorization.primes.size(); ++index) {
        if (selected_exponents[index] == 0) {
            continue;
        }
        const std::uint64_t factor = smooth_factorization.primes[index];
        distinct.push_back(factor);
        if (factor > n2 && factor < n4) {
            large.push_back(factor);
        }
    }
    return {prime, order, smooth_part, point_order, std::move(distinct),
            std::move(large)};
}

}  // namespace

CertificateBounds canonical_certificate_bounds(const mpz_class& prime) {
    if (prime <= 3 || mpz_even_p(prime.get_mpz_t()) != 0) {
        throw std::invalid_argument("certificate modulus must be odd and greater than 3");
    }
    unsigned long bits = 0;
    std::uint64_t n2 = 0;
    std::uint64_t n4 = 0;
    if (!checked_n_bounds(prime, bits, n2, n4)) {
        throw std::overflow_error("bit_length(p)^4 does not fit uint64_t");
    }
    mpz_class lower;
    mpz_class hasse;
    unsigned long upstream_bits = 0;
    std::uint64_t upstream_n2 = 0;
    std::uint64_t upstream_n4 = 0;
    cert_bounds(prime.get_mpz_t(), lower.get_mpz_t(), hasse.get_mpz_t(),
                &upstream_bits, &upstream_n2, &upstream_n4);
    if (bits != upstream_bits || n2 != upstream_n2 || n4 != upstream_n4) {
        throw std::logic_error("certificate-bound implementation mismatch");
    }
    return {bits, n2, n4, lower, hasse};
}

const char* candidate_failure_name(CandidateFailure failure) {
    switch (failure) {
        case CandidateFailure::none:
            return "none";
        case CandidateFailure::invalid_modulus:
            return "invalid_modulus";
        case CandidateFailure::unsupported_bit_length:
            return "unsupported_bit_length";
        case CandidateFailure::invalid_order:
            return "invalid_order";
        case CandidateFailure::invalid_smooth_part:
            return "invalid_smooth_part";
        case CandidateFailure::smooth_part_not_divisor:
            return "smooth_part_not_divisor";
        case CandidateFailure::insufficient_smooth_part:
            return "insufficient_smooth_part";
        case CandidateFailure::no_admissible_divisor:
            return "no_admissible_divisor";
    }
    return "unknown";
}

CandidateResult prepare_certificate_candidate(
    const mpz_class& prime, const mpz_class& order,
    const mpz_class& smooth_part, bool odd_only) {
    if (prime <= 3 || mpz_even_p(prime.get_mpz_t()) != 0) {
        return {CandidateFailure::invalid_modulus, std::nullopt};
    }
    CertificateBounds bounds;
    try {
        bounds = canonical_certificate_bounds(prime);
    } catch (const std::overflow_error&) {
        return {CandidateFailure::unsupported_bit_length, std::nullopt};
    }

    const mpz_class hasse_radius = bounds.hasse_upper_bound - prime - 1;
    const mpz_class hasse_lower_bound = prime + 1 - hasse_radius;
    if (order < hasse_lower_bound || order > bounds.hasse_upper_bound) {
        return {CandidateFailure::invalid_order, std::nullopt};
    }
    if (smooth_part <= 0 || smooth_part > order) {
        return {CandidateFailure::invalid_smooth_part, std::nullopt};
    }
    if (order % smooth_part != 0) {
        return {CandidateFailure::smooth_part_not_divisor, std::nullopt};
    }
    if (smooth_part <= bounds.lower_order_bound) {
        return {CandidateFailure::insufficient_smooth_part, std::nullopt};
    }

    mpz_class point_order;
    std::array<std::uint64_t, kFactorBatchCapacity> upstream_large{};
    int upstream_large_count = 0;
    const int built = build_m2(
        point_order.get_mpz_t(), upstream_large.data(), &upstream_large_count,
        smooth_part.get_mpz_t(), bounds.lower_order_bound.get_mpz_t(),
        bounds.n2, bounds.n4, odd_only ? 1 : 0);
    if (built == 0 || upstream_large_count < 0 ||
        upstream_large_count > static_cast<int>(upstream_large.size()) ||
        point_order <= bounds.lower_order_bound ||
        point_order > bounds.hasse_upper_bound ||
        order % point_order != 0 || smooth_part % point_order != 0) {
        return {CandidateFailure::no_admissible_divisor, std::nullopt};
    }

    const auto factorization = factor_known_smooth(point_order, bounds.n4);
    if (!factorization.has_value()) {
        return {CandidateFailure::no_admissible_divisor, std::nullopt};
    }
    const std::uint64_t least_prime = factorization->primes.front();
    if (point_order >=
        bounds.lower_order_bound * integer_from_u64(least_prime)) {
        return {CandidateFailure::no_admissible_divisor, std::nullopt};
    }
    std::vector<std::uint64_t> large;
    for (const std::uint64_t factor : factorization->primes) {
        if (factor > bounds.n2 && factor < bounds.n4) {
            large.push_back(factor);
        }
    }
    if (large.size() != static_cast<std::size_t>(upstream_large_count) ||
        !std::equal(large.begin(), large.end(), upstream_large.begin())) {
        return {CandidateFailure::no_admissible_divisor, std::nullopt};
    }

    return {
        CandidateFailure::none,
        CertificateCandidate{prime, order, smooth_part, point_order,
                             factorization->primes, std::move(large)},
    };
}

CandidateEnumerationResult enumerate_certificate_candidates(
    const mpz_class& prime, const mpz_class& order,
    const mpz_class& smooth_part,
    const CertificateCandidateVisitor& visitor) {
    if (!visitor) {
        throw std::invalid_argument("certificate candidate visitor is empty");
    }

    // Use the compatibility entry point for validation and to retain its
    // established first-choice ordering.  A no-admissible-divisor result does
    // not end enumeration: build_m2 is intentionally only a selector.
    const CandidateResult preferred = prepare_certificate_candidate(
        prime, order, smooth_part, false);
    if (preferred.failure != CandidateFailure::none &&
        preferred.failure != CandidateFailure::no_admissible_divisor) {
        return {preferred.failure, 0, false};
    }

    CertificateBounds bounds;
    try {
        bounds = canonical_certificate_bounds(prime);
    } catch (const std::overflow_error&) {
        return {CandidateFailure::unsupported_bit_length, 0, false};
    }
    const auto factorization = factor_known_smooth(smooth_part, bounds.n4);
    if (!factorization.has_value()) {
        return {CandidateFailure::no_admissible_divisor, 0, false};
    }

    CandidateEnumerationResult result;
    std::optional<mpz_class> first_preferred_order;
    std::optional<mpz_class> second_preferred_order;
    auto visit = [&](const CertificateCandidate& candidate,
                     CandidateOrigin origin) {
        ++result.candidates_visited;
        if (!visitor(candidate, origin)) {
            result.stopped_early = true;
            return false;
        }
        return true;
    };

    if (preferred) {
        first_preferred_order = preferred.candidate->point_order;
        if (!visit(*preferred.candidate, CandidateOrigin::preferred)) {
            return result;
        }
    }
    const CandidateResult preferred_odd = prepare_certificate_candidate(
        prime, order, smooth_part, true);
    if (preferred_odd &&
        (!first_preferred_order.has_value() ||
         preferred_odd.candidate->point_order != *first_preferred_order)) {
        second_preferred_order = preferred_odd.candidate->point_order;
        if (!visit(*preferred_odd.candidate,
                   CandidateOrigin::preferred_odd_only)) {
            return result;
        }
    }

    const std::size_t factor_count = factorization->primes.size();
    std::vector<mpz_class> suffix_maximum(factor_count + 1U, 1);
    for (std::size_t offset = 0; offset < factor_count; ++offset) {
        const std::size_t index = factor_count - offset - 1U;
        suffix_maximum[index] = suffix_maximum[index + 1U];
        for (int exponent = 0;
             exponent < factorization->exponents[index]; ++exponent) {
            suffix_maximum[index] *=
                integer_from_u64(factorization->primes[index]);
        }
    }

    std::vector<int> selected_exponents(factor_count, 0);
    bool keep_going = true;
    for (std::size_t least_index = 0;
         least_index < factor_count && keep_going; ++least_index) {
        const mpz_class upper_bound =
            bounds.lower_order_bound *
            integer_from_u64(factorization->primes[least_index]);

        std::function<void(std::size_t, const mpz_class&)> descend;
        descend = [&](std::size_t index, const mpz_class& partial) {
            if (!keep_going || partial >= upper_bound ||
                partial * suffix_maximum[index] <=
                    bounds.lower_order_bound) {
                return;
            }
            if (index == factor_count) {
                if (partial <= bounds.lower_order_bound ||
                    (first_preferred_order.has_value() &&
                     partial == *first_preferred_order) ||
                    (second_preferred_order.has_value() &&
                     partial == *second_preferred_order)) {
                    return;
                }
                const CertificateCandidate candidate =
                    candidate_from_factor_exponents(
                        prime, order, smooth_part, partial, *factorization,
                        selected_exponents, bounds.n2, bounds.n4);
                keep_going = visit(candidate, CandidateOrigin::exhaustive);
                return;
            }

            const int minimum_exponent =
                index == least_index ? 1 : 0;
            mpz_class power = 1;
            for (int exponent = 0; exponent < minimum_exponent; ++exponent) {
                power *= integer_from_u64(factorization->primes[index]);
            }
            for (int exponent = minimum_exponent;
                 exponent <= factorization->exponents[index]; ++exponent) {
                const mpz_class next = partial * power;
                if (next >= upper_bound) {
                    break;
                }
                selected_exponents[index] = exponent;
                descend(index + 1U, next);
                if (!keep_going) {
                    return;
                }
                power *= integer_from_u64(factorization->primes[index]);
            }
            selected_exponents[index] = 0;
        };
        descend(least_index, 1);
    }
    if (result.candidates_visited == 0U) {
        result.failure = CandidateFailure::no_admissible_divisor;
    }
    return result;
}

MontgomeryXZ montgomery_ladder(
    const mpz_class& modulus, const mpz_class& coefficient,
    const mpz_class& scalar, const MontgomeryXZ& point) {
    if (scalar < 0) {
        throw std::invalid_argument("Montgomery ladder scalar must be nonnegative");
    }
    const Field field(modulus);
    const mpz_class a = field.normalize(coefficient);
    const MontgomeryXZ base{field.normalize(point.x), field.normalize(point.z)};
    if (scalar == 0) {
        return {1, 0};
    }
    if (scalar == 1) {
        return base;
    }
    MontgomeryXZ lower = base;
    MontgomeryXZ upper = xdbl(field, a, base);
    const std::size_t bits = mpz_sizeinbase(scalar.get_mpz_t(), 2);
    for (std::size_t offset = 1; offset < bits; ++offset) {
        const mp_bitcnt_t bit = static_cast<mp_bitcnt_t>(bits - offset - 1);
        if (mpz_tstbit(scalar.get_mpz_t(), bit) == 0) {
            upper = xadd(field, lower, upper, base);
            lower = xdbl(field, a, lower);
        } else {
            lower = xadd(field, lower, upper, base);
            upper = xdbl(field, a, upper);
        }
    }
    return lower;
}

bool montgomery_has_exact_order(
    const mpz_class& modulus, const mpz_class& coefficient,
    const MontgomeryXZ& point, const mpz_class& order,
    const std::vector<std::uint64_t>& distinct_prime_divisors) {
    if (modulus <= 3 || mpz_even_p(modulus.get_mpz_t()) != 0 || order <= 1) {
        return false;
    }
    const Field field(modulus);
    const mpz_class a = field.normalize(coefficient);
    mpz_class radical;
    if (!validate_distinct_factors(order, distinct_prime_divisors, radical)) {
        return false;
    }
    const MontgomeryXZ multiple = montgomery_ladder(
        modulus, a, order, point);
    if (!is_genuine_infinity(multiple, modulus)) {
        return false;
    }
    const MontgomeryXZ base = montgomery_ladder(
        modulus, a, order / radical, point);
    return check_order_tree(field, a, base, distinct_prime_divisors, 0,
                            distinct_prime_divisors.size());
}

std::string MontgomeryCertificate::line() const {
    std::ostringstream output;
    output << prime << ' ' << coefficient << ' ' << x << ' ' << order;
    for (const std::uint64_t divisor : large_prime_divisors) {
        output << ' ' << divisor;
    }
    return output.str();
}

std::optional<MontgomeryCertificate> assemble_montgomery_certificate(
    const CertificateCandidate& candidate, const mpz_class& coefficient,
    AssemblyOptions options) {
    if (options.attempts_per_coefficient == 0 ||
        mpz_probab_prime_p(candidate.prime.get_mpz_t(), 25) == 0 ||
        candidate.order <= 0 || candidate.point_order <= 1 ||
        candidate.order % candidate.point_order != 0) {
        return std::nullopt;
    }
    const Field field(candidate.prime);
    const mpz_class a = field.normalize(coefficient);
    if (field.square(a) == 4) {
        return std::nullopt;
    }
    const int target_legendre = desired_legendre(options.side);
    for (std::size_t attempt = 0;
         attempt < options.attempts_per_coefficient; ++attempt) {
        const mpz_class x = deterministic_residue(
            field, options.seed, static_cast<std::uint64_t>(attempt),
            kAssemblyDomain ^ static_cast<std::uint64_t>(a.get_ui()));
        const mpz_class rhs = field.mul(
            x, field.add(field.add(field.square(x), field.mul(a, x)), 1));
        const int legendre = field.legendre(rhs);
        if (legendre == 0 ||
            (target_legendre != 0 && legendre != target_legendre)) {
            continue;
        }
        const auto projected = project_to_candidate_order(
            field, a, {x, 1}, candidate);
        if (!projected.has_value()) {
            continue;
        }
        if (!montgomery_has_exact_order(
                candidate.prime, a, *projected, candidate.point_order,
                candidate.distinct_prime_divisors)) {
            continue;
        }
        const mpz_class affine_x = field.mul(projected->x,
                                             field.inverse(projected->z));
        MontgomeryCertificate certificate{
            candidate.prime, a, affine_x, candidate.point_order,
            candidate.large_prime_divisors,
        };
        if (validate_montgomery_certificate(certificate)) {
            return certificate;
        }
    }
    return std::nullopt;
}

std::vector<mpz_class> montgomery_coefficients_from_j(
    const mpz_class& prime, const mpz_class& j_invariant) {
    if (prime <= 3 || mpz_even_p(prime.get_mpz_t()) != 0 ||
        mpz_probab_prime_p(prime.get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "j-to-Montgomery conversion requires an odd probable prime");
    }
    const Field field(prime);
    const mpz_class j = field.normalize(j_invariant);
    const mpz_class inv256 = field.inverse(256);
    const mpz_class c1 = field.mul(field.sub(6912, j), inv256);
    const mpz_class c0 = field.mul(field.sub(field.mul(4, j), 6912), inv256);
    const Poly cubic(field, {c0, c1, field.neg(9), 1});
    const std::vector<mpz_class> u_roots = linear_roots(cubic);
    std::vector<mpz_class> coefficients;
    for (const mpz_class& u : u_roots) {
        if (u == 4) {
            continue;
        }
        const auto square_root = sqrt_mod_prime(field, u);
        if (!square_root.has_value()) {
            continue;
        }
        const mpz_class positive = field.normalize(*square_root);
        const mpz_class negative = field.neg(positive);
        if (field.square(positive) != 4) {
            coefficients.push_back(positive);
        }
        if (negative != positive && field.square(negative) != 4) {
            coefficients.push_back(negative);
        }
    }
    std::sort(coefficients.begin(), coefficients.end());
    coefficients.erase(std::unique(coefficients.begin(), coefficients.end()),
                       coefficients.end());
    for (const mpz_class& coefficient : coefficients) {
        const mpz_class a2 = field.square(coefficient);
        const mpz_class recovered_j = field.divide(
            field.mul(256, field.mul(field.sub(a2, 3),
                                     field.square(field.sub(a2, 3)))),
            field.sub(a2, 4));
        if (recovered_j != j) {
            throw std::logic_error("j-to-Montgomery conversion validation failed");
        }
    }
    return coefficients;
}

std::optional<MontgomeryCertificate> assemble_montgomery_certificate_from_j(
    const CertificateCandidate& candidate, const mpz_class& j_invariant,
    AssemblyOptions options) {
    const std::vector<mpz_class> coefficients = montgomery_coefficients_from_j(
        candidate.prime, j_invariant);
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        AssemblyOptions coefficient_options = options;
        coefficient_options.seed = splitmix64(
            options.seed ^ static_cast<std::uint64_t>(index) ^
            static_cast<std::uint64_t>(coefficients[index].get_ui()));
        const auto result = assemble_montgomery_certificate(
            candidate, coefficients[index], coefficient_options);
        if (result.has_value()) {
            return result;
        }
    }
    return std::nullopt;
}

bool validate_montgomery_certificate(
    const MontgomeryCertificate& certificate) {
    if (certificate.prime <= 3 ||
        mpz_even_p(certificate.prime.get_mpz_t()) != 0 ||
        certificate.coefficient < 0 ||
        certificate.coefficient >= certificate.prime || certificate.x < 0 ||
        certificate.x >= certificate.prime) {
        return false;
    }
    CertificateBounds bounds;
    try {
        bounds = canonical_certificate_bounds(certificate.prime);
    } catch (const std::exception&) {
        return false;
    }
    if (certificate.order <= bounds.lower_order_bound ||
        certificate.order > bounds.hasse_upper_bound) {
        return false;
    }
    mpz_class discriminant =
        (certificate.coefficient * certificate.coefficient - 4) %
        certificate.prime;
    if (discriminant < 0) {
        discriminant += certificate.prime;
    }
    mpz_class discriminant_gcd;
    mpz_gcd(discriminant_gcd.get_mpz_t(), discriminant.get_mpz_t(),
            certificate.prime.get_mpz_t());
    if (discriminant_gcd != 1) {
        return false;
    }
    const auto factorization = factor_known_smooth(certificate.order, bounds.n4);
    if (!factorization.has_value()) {
        return false;
    }
    std::vector<std::uint64_t> expected_large;
    for (const std::uint64_t prime : factorization->primes) {
        if (prime > bounds.n2 && prime < bounds.n4) {
            expected_large.push_back(prime);
        }
    }
    if (expected_large != certificate.large_prime_divisors ||
        certificate.order >= bounds.lower_order_bound *
                                 integer_from_u64(factorization->primes.front())) {
        return false;
    }
    return montgomery_has_exact_order(
        certificate.prime, certificate.coefficient, {certificate.x, 1},
        certificate.order, factorization->primes);
}

}  // namespace oneshotsea
