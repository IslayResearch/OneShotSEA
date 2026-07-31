#include "oneshotsea/early_abort.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace oneshotsea {

namespace {

const mpz_class& evidence_value(const SmoothPartEvidence& evidence) {
    return std::visit(
        [](const auto& typed_evidence) -> const mpz_class& {
            return typed_evidence.value;
        },
        evidence);
}

}  // namespace

std::optional<SmoothScreen> screen_order_candidates(
    const TraceConstraints& constraints, std::size_t trace_cap,
    const mpz_class& certificate_lower_bound_value,
    const SmoothPartExtractor& extractor) {
    const auto traces = constraints.enumerate(trace_cap);
    if (!traces.has_value()) {
        return std::nullopt;
    }
    SmoothScreen result{traces->size(), {}};
    result.survivors.reserve(2U * traces->size());
    for (const mpz_class& trace : *traces) {
        for (const bool twist : {false, true}) {
            const mpz_class signed_trace = twist ? -trace : trace;
            const mpz_class order = constraints.prime() + 1 - signed_trace;
            if (order <= 0) {
                throw std::logic_error("Hasse candidate produced a nonpositive order");
            }
            SmoothPartEvidence evidence = extractor(order);
            const mpz_class& smooth_part = evidence_value(evidence);
            if (smooth_part <= 0 || order % smooth_part != 0) {
                throw std::runtime_error("smooth-part extractor violated divisibility invariant");
            }

            // If a certificate factor m > L exists for the actual order, all
            // primes of m are at most n^4, so m divides the exact full-n^4
            // smooth part S and necessarily S > L.  Thus exact S <= L rules
            // the candidate out.  A partial divisor is only a lower bound on
            // S: it proves success when already > L, but can never prove
            // rejection while unprocessed prime rungs remain.
            const bool exact = std::holds_alternative<ExactN4SmoothPart>(evidence);
            if (!exact || smooth_part > certificate_lower_bound_value) {
                result.survivors.push_back(
                    {trace, twist, order, std::move(evidence)});
            }
        }
    }
    return result;
}

mpz_class trial_smooth_part(mpz_class value, std::uint64_t bound) {
    if (value <= 0) {
        throw std::invalid_argument("smooth-part input must be positive");
    }
    if (bound > 10000000U) {
        throw std::invalid_argument("trial smooth-part helper is limited to 10^7");
    }
    mpz_class smooth = 1;
    for (std::uint64_t prime = 2; prime <= bound; ++prime) {
        bool is_prime = true;
        for (std::uint64_t divisor = 2; divisor * divisor <= prime; ++divisor) {
            if (prime % divisor == 0) {
                is_prime = false;
                break;
            }
        }
        if (!is_prime) {
            continue;
        }
        while (mpz_divisible_ui_p(value.get_mpz_t(), static_cast<unsigned long>(prime)) != 0) {
            value /= static_cast<unsigned long>(prime);
            smooth *= static_cast<unsigned long>(prime);
        }
    }
    return smooth;
}

mpz_class certificate_lower_bound(const mpz_class& prime) {
    if (prime <= 0) {
        throw std::invalid_argument("certificate bound requires positive p");
    }
    mpz_class q;
    mpz_sqrt(q.get_mpz_t(), prime.get_mpz_t());
    mpz_class four_q = 4 * q;
    mpz_class root;
    mpz_sqrt(root.get_mpz_t(), four_q.get_mpz_t());
    return q + 1 + root;
}

}  // namespace oneshotsea
