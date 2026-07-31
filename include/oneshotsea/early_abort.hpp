#pragma once

#include "oneshotsea/trace.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace oneshotsea {

// A divisor of the order known to be supported on primes at most n^4, but
// obtained before the full smoothness ladder has completed.
struct PartialN4SmoothPart {
    mpz_class value;
};

// The exact part of the order supported on every prime at most n^4.  Creating
// this evidence is an explicit assertion that the full ladder was completed.
struct ExactN4SmoothPart {
    mpz_class value;
};

using SmoothPartEvidence = std::variant<PartialN4SmoothPart, ExactN4SmoothPart>;

struct OrderCandidate {
    mpz_class trace;
    bool twist;
    mpz_class order;
    SmoothPartEvidence smooth_part;
};

struct SmoothScreen {
    std::size_t trace_count;
    std::vector<OrderCandidate> survivors;

    bool rejects_curve() const { return survivors.empty(); }
};

using SmoothPartExtractor = std::function<SmoothPartEvidence(const mpz_class&)>;

// Enumerate the complete Hasse-compatible trace set represented by constraints,
// then screen both curve and twist orders.  Partial evidence greater than the
// lower bound is sufficient to retain a candidate, but partial evidence at or
// below the bound is inconclusive and must also be retained.  Only exact
// full-n^4 evidence at or below the bound can discard a candidate.  Trace
// constraints are never mutated based on smoothness.
std::optional<SmoothScreen> screen_order_candidates(
    const TraceConstraints& constraints, std::size_t trace_cap,
    const mpz_class& certificate_lower_bound, const SmoothPartExtractor& extractor);

// Definition-level helper for small tests and differential oracles. It is not
// viable at the production bound near 2^35; the batched Bernstein engine will
// replace it there.
mpz_class trial_smooth_part(mpz_class value, std::uint64_t bound);
mpz_class certificate_lower_bound(const mpz_class& prime);

}  // namespace oneshotsea
