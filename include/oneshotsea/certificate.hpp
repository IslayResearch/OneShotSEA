#pragma once

#include <gmpxx.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace oneshotsea {

// Integer bounds used by the canonical OneShotPrimalityProofs/voneshot.py
// verifier.  The smoothness bound is bit_length(p)^4.
struct CertificateBounds {
    unsigned long bit_length;
    std::uint64_t n2;
    std::uint64_t n4;
    mpz_class lower_order_bound;
    mpz_class hasse_upper_bound;
};

CertificateBounds canonical_certificate_bounds(const mpz_class& prime);

enum class CandidateFailure {
    none,
    invalid_modulus,
    unsupported_bit_length,
    invalid_order,
    invalid_smooth_part,
    smooth_part_not_divisor,
    insufficient_smooth_part,
    no_admissible_divisor,
};

const char* candidate_failure_name(CandidateFailure failure);

// The result of turning an exact n^4-smooth part S of a known curve order N
// into the canonical verifier's order m and large-prime list.  The complete
// distinct factor list is retained for exact point-order testing; only the
// factors in (n^2,n^4) are serialized.
struct CertificateCandidate {
    mpz_class prime;
    mpz_class order;
    mpz_class smooth_part;
    mpz_class point_order;
    std::vector<std::uint64_t> distinct_prime_divisors;
    std::vector<std::uint64_t> large_prime_divisors;
};

struct CandidateResult {
    CandidateFailure failure = CandidateFailure::none;
    std::optional<CertificateCandidate> candidate;

    explicit operator bool() const { return candidate.has_value(); }
};

// smooth_part must be the exact n^4-smooth part of order.  The returned m is
// independently checked to divide both values and to satisfy every canonical
// size/factor-list condition.  odd_only requests build_m2's fallback that omits
// 2 when the full 2-primary part is known not to divide the group exponent.
CandidateResult prepare_certificate_candidate(
    const mpz_class& prime, const mpz_class& order,
    const mpz_class& smooth_part, bool odd_only = false);

// The exhaustive iterator preserves build_m2's choices as the first preferred
// candidates, then streams every other divisor m of smooth_part satisfying the
// canonical L < m < L*r window (r is m's least prime divisor).  The callback
// returns true to continue or false to stop.  No candidate collection is kept;
// enumeration uses O(number of distinct smooth factors) working memory.
enum class CandidateOrigin {
    preferred,
    preferred_odd_only,
    exhaustive,
};

using CertificateCandidateVisitor = std::function<bool(
    const CertificateCandidate&, CandidateOrigin)>;

struct CandidateEnumerationResult {
    CandidateFailure failure = CandidateFailure::none;
    std::size_t candidates_visited = 0;
    std::size_t search_nodes_visited = 0;
    bool stopped_early = false;
    enum class Limit {
        none,
        candidates,
        search_nodes,
    } limit = Limit::none;
};

struct CandidateEnumerationLimits {
    std::size_t max_candidates = std::numeric_limits<std::size_t>::max();
    std::size_t max_search_nodes = std::numeric_limits<std::size_t>::max();
};

CandidateEnumerationResult enumerate_certificate_candidates(
    const mpz_class& prime, const mpz_class& order,
    const mpz_class& smooth_part,
    const CertificateCandidateVisitor& visitor,
    CandidateEnumerationLimits limits = {});

struct MontgomeryXZ {
    mpz_class x;
    mpz_class z;
};

// Native implementation of the same x-only Montgomery ladder used by the
// canonical verifier.  Inputs and outputs are reduced modulo modulus.
MontgomeryXZ montgomery_ladder(
    const mpz_class& modulus, const mpz_class& coefficient,
    const mpz_class& scalar, const MontgomeryXZ& point);

// Tests exact order using the canonical genuine-infinity condition and a
// product tree over the supplied complete list of distinct prime divisors.
// This works projectively and does not assume that point.z == 1.
bool montgomery_has_exact_order(
    const mpz_class& modulus, const mpz_class& coefficient,
    const MontgomeryXZ& point, const mpz_class& order,
    const std::vector<std::uint64_t>& distinct_prime_divisors);

enum class MontgomerySide {
    curve,  // x^3+A*x^2+x is a nonzero quadratic residue
    twist,  // x^3+A*x^2+x is a quadratic nonresidue
    either,
};

struct AssemblyOptions {
    std::uint64_t seed = 1;
    std::size_t attempts_per_coefficient = 400;
    MontgomerySide side = MontgomerySide::either;
};

struct MontgomeryCertificate {
    mpz_class prime;
    mpz_class coefficient;
    mpz_class x;
    mpz_class order;
    std::vector<std::uint64_t> large_prime_divisors;

    std::string line() const;
};

// Assemble on a known Montgomery model.  The input order is used only for the
// cofactor projection; exact point order is rechecked before success.
std::optional<MontgomeryCertificate> assemble_montgomery_certificate(
    const CertificateCandidate& candidate, const mpz_class& coefficient,
    AssemblyOptions options = {});

// Solve 256(A^2-3)^3 = j(A^2-4) over F_p and return every nonsingular
// Montgomery coefficient, canonically sorted.  This is the j-to-Montgomery
// portion of the permissively licensed OneShotFastECPP certificate tail,
// implemented on OneShotSEA's field/polynomial backend.
std::vector<mpz_class> montgomery_coefficients_from_j(
    const mpz_class& prime, const mpz_class& j_invariant);

std::optional<MontgomeryCertificate> assemble_montgomery_certificate_from_j(
    const CertificateCandidate& candidate, const mpz_class& j_invariant,
    AssemblyOptions options = {});

// Native defense-in-depth validation of an assembled line.  Final certificates
// must still be run through the unmodified pinned canonical verifier.
bool validate_montgomery_certificate(const MontgomeryCertificate& certificate);

}  // namespace oneshotsea
