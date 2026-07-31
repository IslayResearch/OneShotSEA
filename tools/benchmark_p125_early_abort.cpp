#include "oneshotsea/certificate.hpp"
#include "oneshotsea/exact_smooth.hpp"
#include "oneshotsea/trace.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

// Reproduce the only non-singleton early-screen opportunity in the p125
// seed=202607300000,index=0 SEA transcript.  The final trace independently
// authenticates every exact residue used to reconstruct the level-283 state.
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: benchmark_p125_early_abort CACHE SHA256\n";
        return 2;
    }
    try {
        const mpz_class prime(
            "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237");
        const mpz_class trace(
            "-365740341970189488309911289011845029564959533378642173923364552");
        constexpr std::array<std::uint64_t, 31> exact_levels = {
            5,   7,   11,  13,  29,  31,  37,  43,  47,  67,  83,
            89,  103, 113, 127, 137, 139, 163, 167, 193, 199, 229,
            239, 241, 251, 257, 263, 269, 277, 281, 283,
        };
        oneshotsea::TraceConstraints constraints(prime);
        for (const std::uint64_t ell : exact_levels) {
            constraints.refine_exact(
                ell, mpz_fdiv_ui(trace.get_mpz_t(), ell));
        }
        const auto traces = constraints.enumerate(256);
        if (!traces.has_value()) {
            throw std::runtime_error(
                "level-283 trace set exceeds benchmark cap");
        }

        oneshotsea::ExactSmoothOptions options;
        options.thread_count = 8;
        options.max_orders_per_batch = 128;
        options.max_root_auxiliary_bytes = 128U * 1024U * 1024U;
        const auto load_start = std::chrono::steady_clock::now();
        const auto engine = oneshotsea::ExactSmoothEngine::load(
            prime, std::filesystem::path(argv[1]), argv[2], options);
        const auto extract_start = std::chrono::steady_clock::now();
        const auto parts = engine.extract_curve_twist(*traces);
        const auto extract_end = std::chrono::steady_clock::now();
        const auto bounds = oneshotsea::canonical_certificate_bounds(prime);
        std::size_t surviving_orders = 0;
        for (const auto& part : parts) {
            surviving_orders +=
                part.curve_smooth_part.value > bounds.lower_order_bound;
            surviving_orders +=
                part.twist_smooth_part.value > bounds.lower_order_bound;
        }
        std::cout << "candidate_count=" << traces->size() << '\n'
                  << "load_seconds="
                  << std::chrono::duration<double>(extract_start - load_start)
                         .count()
                  << '\n'
                  << "extract_seconds="
                  << std::chrono::duration<double>(extract_end - extract_start)
                         .count()
                  << '\n'
                  << "orders=" << 2U * parts.size() << '\n'
                  << "surviving_orders=" << surviving_orders << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark failure: " << error.what() << '\n';
        return 1;
    }
}
