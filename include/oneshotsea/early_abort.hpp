#pragma once

#include "oneshotsea/trace.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace oneshotsea {

struct OrderCandidate {
    mpz_class trace;
    bool twist;
    mpz_class order;
    mpz_class smooth_part;
};

struct SmoothScreen {
    std::size_t trace_count;
    std::vector<OrderCandidate> survivors;

    bool rejects_curve() const { return survivors.empty(); }
};

using SmoothPartExtractor = std::function<mpz_class(const mpz_class&)>;

// Enumerate the complete Hasse-compatible trace set represented by constraints,
// then screen both curve and twist orders. The result is sound only when the
// supplied extractor returns the exact n^4-smooth part; this function never
// labels the extractor and never mutates trace constraints based on smoothness.
std::optional<SmoothScreen> screen_order_candidates(
    const TraceConstraints& constraints, std::size_t trace_cap,
    const mpz_class& certificate_lower_bound, const SmoothPartExtractor& extractor);

// Definition-level helper for small tests and differential oracles. It is not
// viable at the production bound near 2^35; the batched Bernstein engine will
// replace it there.
mpz_class trial_smooth_part(mpz_class value, std::uint64_t bound);
mpz_class certificate_lower_bound(const mpz_class& prime);

}  // namespace oneshotsea
