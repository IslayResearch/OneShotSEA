#include "oneshotsea/weber_curve_generator.hpp"

#include "oneshotsea/weber.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace oneshotsea {
namespace {

constexpr std::uint64_t kWeberSearchDomain =
    UINT64_C(0x5745424552464356);  // "WEBERFCV"

void require_search_prime(const mpz_class& prime) {
    if (prime <= 7 || mpz_even_p(prime.get_mpz_t()) != 0 ||
        mpz_probab_prime_p(prime.get_mpz_t(), 25) == 0) {
        throw std::invalid_argument(
            "Weber curve generation requires an odd probable prime greater than 7");
    }
}

mpz_class least_quadratic_nonsquare(const Field& field) {
    for (mpz_class candidate = 2; candidate < field.modulus(); ++candidate) {
        if (field.legendre(candidate) == -1) {
            return candidate;
        }
    }
    throw std::logic_error("prime field has no quadratic nonsquare");
}

WeberCurvePair pair_from_admitted_f(const Field& field,
                                    const mpz_class& normalized_f,
                                    std::uint64_t rejected_samples) {
    if (normalized_f == 0) {
        throw std::domain_error("a Weber-f search sample cannot be zero");
    }
    const mpz_class j = j_from_weber_f(field, normalized_f);
    if (j == 0 || j == field.normalize(1728)) {
        throw std::domain_error(
            "ramified Weber image j=0 or j=1728 is not a search curve");
    }

    // For k=j/(1728-j), y^2=x^3+3kx+2k has invariant
    // 1728*k/(k+1)=j.  The exclusions above make every denominator and the
    // discriminant nonzero in characteristic greater than 3.
    const mpz_class k = field.divide(j, field.sub(1728, j));
    Curve curve(field, field.mul(3, k), field.mul(2, k));
    if (curve.is_singular() || curve.j_invariant() != j) {
        throw std::logic_error("Weber-derived curve validation failed");
    }

    const mpz_class nonsquare = least_quadratic_nonsquare(field);
    Curve twist = curve.quadratic_twist(nonsquare);
    if (twist.is_singular() || twist.j_invariant() != j) {
        throw std::logic_error("Weber-derived twist validation failed");
    }

    return {normalized_f, j, nonsquare, std::move(curve), std::move(twist),
            rejected_samples};
}

}  // namespace

WeberCurvePair weber_curve_pair_from_f(const Field& field,
                                       const mpz_class& weber_f) {
    require_search_prime(field.modulus());
    return pair_from_admitted_f(field, field.normalize(weber_f), 0);
}

WeberCurvePair deterministic_weber_curve_pair(const mpz_class& prime,
                                               std::uint64_t seed,
                                               std::uint64_t global_index) {
    require_search_prime(prime);
    const Field field(prime);
    for (std::uint64_t rejected = 0;; ++rejected) {
        const std::uint64_t attempt_domain =
            splitmix64(kWeberSearchDomain ^ rejected);
        const mpz_class weber_f = deterministic_residue(
            field, seed, global_index, attempt_domain);
        if (weber_f != 0) {
            const mpz_class j = j_from_weber_f(field, weber_f);
            // A counted order can reach the canonical certificate only when
            // this j-invariant has a base-field Montgomery coefficient.
            // Roughly half of the p125-congruence Weber images do not, so
            // reject them before spending an SEA point count.
            if (j != 0 && j != field.normalize(1728) &&
                has_montgomery_model_from_j(field, j)) {
                return pair_from_admitted_f(field, weber_f, rejected);
            }
        }
        if (rejected == std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error("exhausted deterministic Weber-f retry space");
        }
    }
}

}  // namespace oneshotsea
