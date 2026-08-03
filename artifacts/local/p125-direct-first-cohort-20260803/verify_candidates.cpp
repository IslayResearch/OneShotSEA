#include "oneshotsea/direct_context_cache.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/x1_27_probe.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const mpz_class kPrime(
    "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000237");
constexpr std::uint64_t kSeed = UINT64_C(202607300000);
constexpr char kCacheSha256[] =
    "d9848275c04d77c5a40f96eb06f113100ebc7a1b3ac0bc6c15d207677be41a53";
const std::vector<std::uint64_t> kLevels = {
    7U, 5U, 11U, 13U, 19U, 17U, 23U, 29U, 31U, 37U,
    41U, 43U, 47U, 53U, 67U, 71U, 79U, 61U, 73U, 59U,
};

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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: verify_candidates WEBER_TABLE_DIR DIRECT_CACHE\n";
        return 2;
    }

    const oneshotsea::Field field(kPrime);
    auto context = oneshotsea::load_classical_direct_context_cache(
        field, kLevels, UINT64_C(10000000), UINT64_C(1000000), 1U,
        argv[2], kCacheSha256,
        oneshotsea::ClassicalDirectContextCacheLimits{
            UINT64_C(1000000000), 4096U, UINT64_C(1000000)});
    context.set_cached_context_residency_budget_bytes(
        std::size_t{1000000000});

    for (std::uint64_t index = UINT64_C(2000001);
         index < UINT64_C(2000005); ++index) {
        const auto generated = oneshotsea::deterministic_x1_27_search_curve(
            kPrime, kSeed, index, true);
        if (!generated.sample.has_value()) {
            throw std::runtime_error("deterministic curve generation failed");
        }
        const auto& sample = *generated.sample;
        const oneshotsea::ExactTracePrior prior = trace_prior(sample);
        oneshotsea::TraceConstraints initial(kPrime);
        initial.refine_exact(prior.modulus(), prior.residue());
        oneshotsea::WeberSeaResult retained{
            initial, initial, {}, {}, {}, {}, std::nullopt, {},
            oneshotsea::SeaCurveModelBinding{
                sample.pair.curve.a(), sample.pair.curve.b()}};
        oneshotsea::extend_sea_with_prepared_classical_direct(
            sample.pair.curve, retained, context, 16U);
        oneshotsea::WeberSeaResult result =
            oneshotsea::run_weber_sea_reference(
                sample.pair.curve, argv[1], 401U, 16U, {}, 1U,
                true, true, {}, prior, sample.pair.weber_f, &retained);
        if (!result.traces.has_value() || result.traces->empty()) {
            throw std::runtime_error("retained candidate set did not complete");
        }
        std::sort(result.traces->begin(), result.traces->end());
        std::cout << "index=" << index
                  << " count=" << result.traces->size() << " traces=";
        for (std::size_t i = 0; i < result.traces->size(); ++i) {
            if (i != 0U) {
                std::cout << ',';
            }
            std::cout << (*result.traces)[i];
        }
        std::cout << '\n';
    }
}
