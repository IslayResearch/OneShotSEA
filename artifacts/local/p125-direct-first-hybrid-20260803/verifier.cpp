#include "oneshotsea/sea.hpp"
#include "oneshotsea/x1_27_probe.hpp"

#include <chrono>
#include <iostream>

int main() {
    const mpz_class prime(
        "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237");
    constexpr std::uint64_t seed = UINT64_C(202607300000);
    constexpr std::uint64_t index = UINT64_C(2000000);
    auto generated = oneshotsea::deterministic_x1_27_search_curve(
        prime, seed, index, true);
    const auto& sample = *generated.sample;
    const std::uint64_t divisor = sample.group_divisor;
    const std::uint64_t p_plus_one =
        (mpz_fdiv_ui(prime.get_mpz_t(), divisor) + 1U) % divisor;
    const std::uint64_t residue =
        sample.selected_side == oneshotsea::X127CanonicalSide::curve
            ? p_plus_one
            : (divisor - p_plus_one) % divisor;
    const oneshotsea::ExactTracePrior prior(prime, divisor, residue);
    const auto start = std::chrono::steady_clock::now();
    const auto result = oneshotsea::run_weber_sea_reference(
        sample.pair.curve, "data/modpoly/weber_f", 401U, 1U, {}, 1U,
        true, true, {}, prior, sample.pair.weber_f);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "trace=";
    if (result.traces && result.traces->size() == 1U) {
        std::cout << result.traces->front();
    } else {
        std::cout << "incomplete";
    }
    std::cout << " exact_candidates=" << result.constraints.candidate_count()
              << " effective_candidates="
              << result.effective_constraints.candidate_count()
              << " levels=" << result.levels.size()
              << " elapsed_us=" << elapsed << '\n';
}
