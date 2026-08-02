#include "oneshotsea/sea.hpp"
#include "oneshotsea/weber.hpp"
#include "oneshotsea/x1_11_probe.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

std::uint64_t trace_residue(const mpz_class& prime,
                            const oneshotsea::X111ProbeSample& sample) {
    const std::uint64_t divisor = sample.group_divisor;
    const std::uint64_t p_plus_one =
        (mpz_fdiv_ui(prime.get_mpz_t(), divisor) + 1U) % divisor;
    return sample.selected_side == oneshotsea::X111CanonicalSide::curve
        ? p_plus_one
        : (divisor - p_plus_one) % divisor;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: benchmark_known_lift INDEX generic|known\n";
        return 2;
    }
    const std::uint64_t index = std::stoull(argv[1]);
    const std::string mode(argv[2]);
    if (mode != "generic" && mode != "known") {
        throw std::invalid_argument("mode must be generic or known");
    }
    const mpz_class prime(
        "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237");
    constexpr std::uint64_t seed = UINT64_C(202607300000);
    const auto generated = oneshotsea::deterministic_x1_11_search_curve(
        prime, seed, index, true);
    const auto& sample = *generated.sample;
    const oneshotsea::ExactTracePrior prior(
        prime, sample.group_divisor, trace_residue(prime, sample));
    const std::size_t discovered_lifts = oneshotsea::weber_f_lifts(
        sample.pair.curve.field(), sample.pair.curve.j_invariant()).size();
    std::uint64_t modular_roots_us = 0;
    std::uint64_t source_lifts_us = 0;
    std::uint64_t bmss_us = 0;
    std::uint64_t eigenvalue_us = 0;
    const auto started = std::chrono::steady_clock::now();
    const auto result = oneshotsea::run_weber_sea_reference(
        sample.pair.curve, "data/modpoly/weber_f", 401, 16,
        [&](const oneshotsea::WeberSeaLevelRecord& level) {
            modular_roots_us += level.timings.modular_roots_us;
            source_lifts_us += level.timings.source_lifts_us;
            bmss_us += level.timings.bmss_us;
            eigenvalue_us += level.timings.eigenvalue_us;
        },
        1, true, true, {}, prior,
        mode == "known" ? std::optional<mpz_class>(sample.pair.weber_f)
                        : std::nullopt);
    const auto elapsed_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    std::cout << "index=" << index << " mode=" << mode
              << " discovered_lifts=" << discovered_lifts
              << " retained_lifts=" << result.compatible_source_lifts.size()
              << " levels=" << result.levels.size()
              << " traces=" << (result.traces ? result.traces->size() : 0U)
              << " elapsed_us=" << elapsed_us
              << " source_lifts_us=" << source_lifts_us
              << " modular_roots_us=" << modular_roots_us
              << " bmss_us=" << bmss_us
              << " eigenvalue_us=" << eigenvalue_us << '\n';
    std::cout << "projection=";
    for (const auto& level : result.levels) {
        std::cout << level.ell << ':';
        if (level.trace_residue) {
            std::cout << *level.trace_residue;
        } else {
            std::cout << '-';
        }
        std::cout << ':' << level.exact_modulus << ':'
                  << level.exact_trace_candidate_count << ',';
    }
    std::cout << " traces=";
    if (result.traces) {
        for (const mpz_class& trace : *result.traces) {
            std::cout << trace << ',';
        }
    }
    std::cout << '\n';
}
