#include "oneshotsea/direct_context_cache.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/x1_27_probe.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

const mpz_class kPrime(
    "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000237");
constexpr std::uint64_t kSeed = UINT64_C(202607300000);
constexpr std::size_t kEarlyLevelCount = 20U;

const std::vector<std::uint64_t> kLevels = {
    7U, 5U, 11U, 13U, 19U, 17U, 23U, 29U, 31U, 37U, 41U,
    43U, 47U, 53U, 67U, 71U, 79U, 61U, 73U, 59U, 89U, 97U,
};

struct Fixture {
    std::uint64_t index;
    std::size_t early_candidates;
    std::uint64_t baseline_last_weber_level;
    std::uint64_t tail_last_weber_level;
    const char* trace;
};

constexpr std::array<Fixture, 3> kFixtures = {{
    {UINT64_C(2000002), 13U, 263U, 257U,
     "312744557074493258005540218670034986285435355679693042023392238"},
    {UINT64_C(2000003), 4U, 277U, 269U,
     "-252845884365417830567895303231394093497235790636298489485509474"},
    {UINT64_C(2000004), 5U, 379U, 373U,
     "432966650303160993124127306120296021107647349914430129038843294"},
}};

std::uint64_t elapsed_us(Clock::time_point start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start).count();
    if (elapsed < 0) {
        throw std::logic_error("steady clock moved backwards");
    }
    return static_cast<std::uint64_t>(elapsed);
}

oneshotsea::ExactTracePrior trace_prior(
    const oneshotsea::X127ProbeSample& sample) {
    const std::uint64_t positive =
        (mpz_fdiv_ui(kPrime.get_mpz_t(), sample.group_divisor) + 1U) %
        sample.group_divisor;
    const std::uint64_t residue =
        sample.selected_side == oneshotsea::X127CanonicalSide::curve
            ? positive
            : (sample.group_divisor - positive) % sample.group_divisor;
    return oneshotsea::ExactTracePrior(
        kPrime, sample.group_divisor, residue);
}

std::uint64_t last_weber_level(const oneshotsea::WeberSeaResult& result) {
    return result.levels.empty() ? 0U : result.levels.back().ell;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr
            << "usage: validate_p125_cap_one_tail WEBER_TABLE_DIR CACHE SHA256\n";
        return EXIT_FAILURE;
    }
    try {
        oneshotsea::ClassicalDirectSeaContext context =
            oneshotsea::load_classical_direct_context_cache(
                oneshotsea::Field(kPrime), kLevels, UINT64_C(10000000),
                UINT64_C(1000000), 1U, argv[2], argv[3],
                oneshotsea::ClassicalDirectContextCacheLimits{
                    UINT64_C(1000000000), 4096U, UINT64_C(1000000)});

        for (const Fixture& fixture : kFixtures) {
            const auto generated =
                oneshotsea::deterministic_x1_27_search_curve(
                    kPrime, kSeed, fixture.index, true);
            if (!generated.sample.has_value()) {
                throw std::runtime_error(
                    "deterministic X1(27) fixture generation failed");
            }
            const auto& sample = *generated.sample;
            const oneshotsea::ExactTracePrior prior = trace_prior(sample);
            oneshotsea::TraceConstraints initial(kPrime);
            initial.refine_exact(prior.modulus(), prior.residue());
            oneshotsea::WeberSeaResult early{
                initial, initial, {}, {}, {}, {}, std::nullopt, {},
                oneshotsea::SeaCurveModelBinding{
                    sample.pair.curve.a(), sample.pair.curve.b()}};

            oneshotsea::extend_sea_with_prepared_classical_direct_prefix(
                sample.pair.curve, early, context, kEarlyLevelCount, 16U);
            const Clock::time_point early_weber_start = Clock::now();
            early = oneshotsea::run_weber_sea_reference(
                sample.pair.curve, argv[1], 401U, 16U, {}, 1U, true, true,
                {}, prior, sample.pair.weber_f, &early);
            const std::uint64_t early_weber_us =
                elapsed_us(early_weber_start);
            if (!early.traces.has_value() ||
                early.traces->size() != fixture.early_candidates) {
                throw std::runtime_error(
                    "p125 fixture did not reproduce its complete cap-N set");
            }

            oneshotsea::WeberSeaResult baseline = early;
            const Clock::time_point baseline_start = Clock::now();
            baseline = oneshotsea::run_weber_sea_reference(
                sample.pair.curve, argv[1], 401U, 1U, {}, 1U, true, true,
                {}, prior, sample.pair.weber_f, &baseline);
            const std::uint64_t baseline_us = elapsed_us(baseline_start);

            oneshotsea::WeberSeaResult with_tail = early;
            const std::size_t retained_direct_levels =
                with_tail.classical_direct_levels.size();
            const Clock::time_point tail_start = Clock::now();
            oneshotsea::extend_sea_with_prepared_classical_direct_prefix(
                sample.pair.curve, with_tail, context, kLevels.size(), 1U);
            const std::uint64_t tail_us = elapsed_us(tail_start);
            const std::size_t tail_levels =
                with_tail.classical_direct_levels.size() -
                retained_direct_levels;
            const mpz_class candidates_after_tail =
                with_tail.effective_constraints.candidate_count();
            const Clock::time_point tail_weber_start = Clock::now();
            if (!with_tail.traces.has_value()) {
                with_tail = oneshotsea::run_weber_sea_reference(
                    sample.pair.curve, argv[1], 401U, 1U, {}, 1U, true,
                    true, {}, prior, sample.pair.weber_f, &with_tail);
            }
            const std::uint64_t tail_weber_us =
                elapsed_us(tail_weber_start);

            const mpz_class expected_trace(fixture.trace);
            if (!baseline.traces.has_value() ||
                baseline.traces->size() != 1U ||
                baseline.traces->front() != expected_trace ||
                !with_tail.traces.has_value() ||
                with_tail.traces->size() != 1U ||
                with_tail.traces->front() != expected_trace ||
                last_weber_level(baseline) !=
                    fixture.baseline_last_weber_level ||
                last_weber_level(with_tail) !=
                    fixture.tail_last_weber_level ||
                tail_levels == 0U) {
                throw std::runtime_error(
                    "cap-one direct tail did not preserve the certified p125 trace or expected stopping point");
            }

            std::cout
                << "{\"schema\":\"oneshotsea.p125-cap-one-tail.v1\""
                << ",\"index\":\"" << fixture.index
                << "\",\"early_candidates\":\""
                << early.traces->size()
                << "\",\"early_last_weber_level\":\""
                << last_weber_level(early)
                << "\",\"baseline_last_weber_level\":\""
                << last_weber_level(baseline)
                << "\",\"tail_direct_level_count\":\""
                << tail_levels
                << "\",\"candidates_after_tail\":\""
                << candidates_after_tail
                << "\",\"tail_last_weber_level\":\""
                << last_weber_level(with_tail)
                << "\",\"trace\":\"" << with_tail.traces->front()
                << "\",\"timings_us\":{\"early_weber\":\""
                << early_weber_us << "\",\"baseline_continuation\":\""
                << baseline_us << "\",\"direct_tail\":\"" << tail_us
                << "\",\"tail_weber_continuation\":\""
                << tail_weber_us << "\"}}\n";
        }
        std::cout
            << "{\"schema\":\"oneshotsea.p125-cap-one-tail-summary.v1\""
            << ",\"cache_level_load_count\":\""
            << context.cached_level_load_count()
            << "\",\"cache_level_load_us\":\""
            << context.cached_level_load_us()
            << "\",\"cache_peak_resident_contexts\":\""
            << context.peak_cached_resident_context_count() << "\"}\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "p125 cap-one tail validation: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
