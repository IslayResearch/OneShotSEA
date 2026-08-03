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
    if (!generated.sample.has_value()) {
        std::cerr << "the pinned X1(27) target was not regenerated\n";
        return 2;
    }
    const auto& sample = *generated.sample;
    const mpz_class expected_a(
        "71767066679186603923921770935567842539817966722958413189905128110444348984023454005578926582733725886104879149565268081093352");
    const mpz_class expected_b(
        "14511377786124402615947847290378561693211977815305608793270085406962899322682302670385951055155817257403252766376845387395489");
    if (sample.pair.curve.a() != expected_a ||
        sample.pair.curve.b() != expected_b) {
        std::cerr << "the regenerated curve does not match the pinned model\n";
        return 3;
    }
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
    if (!result.traces || result.traces->size() != 1U) {
        std::cerr << "the table-backed point count did not complete\n";
        return 4;
    }
    std::cout << "schema=oneshotsea.native-table-backed-cap-one.v1\n"
              << "prime=" << prime << '\n'
              << "seed=" << seed << '\n'
              << "index=" << index << '\n'
              << "a=" << sample.pair.curve.a() << '\n'
              << "b=" << sample.pair.curve.b() << '\n'
              << "trace=" << result.traces->front() << '\n'
              << "exact_candidates="
              << result.constraints.candidate_count() << '\n'
              << "effective_candidates="
              << result.effective_constraints.candidate_count() << '\n'
              << "levels=" << result.levels.size() << '\n'
              << "elapsed_us=" << elapsed << '\n';
}
