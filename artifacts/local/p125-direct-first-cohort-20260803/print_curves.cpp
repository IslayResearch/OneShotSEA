#include "oneshotsea/x1_27_probe.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>

int main() {
    const mpz_class prime(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237");
    constexpr std::uint64_t seed = UINT64_C(202607300000);

    for (std::uint64_t index = UINT64_C(2000001);
         index < UINT64_C(2000005); ++index) {
        const auto generated = oneshotsea::deterministic_x1_27_search_curve(
            prime, seed, index, true);
        if (!generated.sample.has_value()) {
            throw std::runtime_error("deterministic curve generation failed");
        }
        const auto& sample = *generated.sample;
        const auto& curve = sample.pair.curve;
        const std::uint64_t positive =
            (mpz_fdiv_ui(prime.get_mpz_t(), sample.group_divisor) + 1U) %
            sample.group_divisor;
        const std::uint64_t residue =
            sample.selected_side == oneshotsea::X127CanonicalSide::curve
                ? positive
                : (sample.group_divisor - positive) % sample.group_divisor;
        std::cout << "index=" << index
                  << " a=" << curve.a()
                  << " b=" << curve.b()
                  << " j=" << curve.j_invariant()
                  << " prior_modulus=" << sample.group_divisor
                  << " prior_residue=" << residue
                  << " selected_side="
                  << oneshotsea::x1_27_canonical_side_name(
                         sample.selected_side)
                  << '\n';
    }
}
