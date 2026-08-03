#include "oneshotsea/schoof.hpp"
#include "oneshotsea/sea.hpp"
#if !defined(ONESHOTSEA_BENCHMARK_LEGACY_CONTEXT)
#include "oneshotsea/direct_context_cache.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/resource.h>

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_us(Clock::time_point start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start).count();
    if (elapsed < 0) {
        throw std::runtime_error("steady clock moved backward");
    }
    return static_cast<std::uint64_t>(elapsed);
}

std::uint64_t peak_rss_bytes() {
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) {
        throw std::runtime_error("getrusage failed");
    }
    const auto encoded =
        static_cast<unsigned long long>(usage.ru_maxrss);
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(encoded);
#else
    if (encoded >
        std::numeric_limits<std::uint64_t>::max() / 1024ULL) {
        throw std::overflow_error("peak RSS byte count overflows uint64");
    }
    return static_cast<std::uint64_t>(encoded * 1024ULL);
#endif
}

oneshotsea::WeberSeaResult evaluate(
    const oneshotsea::Curve& curve,
    const oneshotsea::ClassicalDirectSeaContext& context) {
    oneshotsea::TraceConstraints initial(curve.field().modulus());
    oneshotsea::WeberSeaResult state{
        initial, initial, {}, {}, {}, {}, std::nullopt, {}};
    oneshotsea::extend_sea_with_prepared_classical_direct(
        curve, state, context, 1U);
    if (state.classical_direct_levels.size() != 1U) {
        throw std::runtime_error(
            "single-level benchmark did not retain exactly one record");
    }
    return state;
}

std::uint64_t validate_against_schoof(
    const oneshotsea::Curve& curve,
    const oneshotsea::WeberSeaResult& state, std::uint64_t ell) {
    const std::uint64_t schoof =
        oneshotsea::schoof_trace_mod_ell(curve, ell);
    const oneshotsea::ClassicalDirectSeaLevelRecord& record =
        state.classical_direct_levels.front();
    if (record.ell != ell) {
        throw std::runtime_error("direct level record has the wrong prime");
    }
    if (record.exact) {
        if (!record.trace_residue.has_value() ||
            *record.trace_residue != schoof) {
            throw std::runtime_error(
                "exact direct residue disagrees with Schoof");
        }
        return schoof;
    }
    const auto found = std::find_if(
        state.atkin_constraints.begin(), state.atkin_constraints.end(),
        [ell](const oneshotsea::AtkinConstraint& constraint) {
            return constraint.ell == ell;
        });
    if (found == state.atkin_constraints.end() ||
        std::find(found->trace_residues.begin(),
                  found->trace_residues.end(), schoof) ==
            found->trace_residues.end()) {
        throw std::runtime_error(
            "direct Atkin set excludes the Schoof residue");
    }
    return schoof;
}

std::uint64_t parse_u64(const std::string& text, const char* label) {
    std::size_t consumed = 0U;
    unsigned long long value = 0U;
    try {
        value = std::stoull(text, &consumed, 10);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid ") + label);
    }
    if (consumed != text.size() ||
        value > std::numeric_limits<std::uint64_t>::max()) {
        throw std::invalid_argument(std::string("invalid ") + label);
    }
    return static_cast<std::uint64_t>(value);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::size_t threads = 4U;
        std::vector<std::uint64_t> levels;
        bool skip_schoof = false;
#if !defined(ONESHOTSEA_BENCHMARK_LEGACY_CONTEXT)
        std::string cache_path;
        std::string cache_sha256;
#endif
        for (int index = 1; index < argc; ++index) {
            const std::string argument(argv[index]);
            if (argument == "--threads") {
                if (++index >= argc) {
                    throw std::invalid_argument(
                        "--threads requires a value");
                }
                const std::uint64_t parsed =
                    parse_u64(argv[index], "thread limit");
                if (parsed >
                    std::numeric_limits<std::size_t>::max()) {
                    throw std::invalid_argument(
                        "thread limit exceeds size_t");
                }
                threads = static_cast<std::size_t>(parsed);
                continue;
            }
            if (argument == "--skip-schoof") {
                skip_schoof = true;
                continue;
            }
#if !defined(ONESHOTSEA_BENCHMARK_LEGACY_CONTEXT)
            if (argument == "--cache") {
                if (++index >= argc) {
                    throw std::invalid_argument(
                        "--cache requires a path");
                }
                cache_path = argv[index];
                continue;
            }
            if (argument == "--cache-sha256") {
                if (++index >= argc) {
                    throw std::invalid_argument(
                        "--cache-sha256 requires a digest");
                }
                cache_sha256 = argv[index];
                continue;
            }
#endif
            const std::uint64_t level = parse_u64(argument, "SEA level");
            if (level <= 3U ||
                level > std::numeric_limits<unsigned>::max() ||
                !oneshotsea::is_prime_u64(level)) {
                throw std::invalid_argument(
                    "SEA levels must be odd primes greater than three");
            }
            levels.push_back(level);
        }
        if (levels.empty()) {
            std::cerr << "usage: benchmark_p125_classical_direct "
                         "[--threads N] [--cache PATH "
                         "--cache-sha256 DIGEST] [--skip-schoof] ELL...\n";
            return 2;
        }
#if !defined(ONESHOTSEA_BENCHMARK_LEGACY_CONTEXT)
        if (cache_path.empty() != cache_sha256.empty() ||
            (!cache_path.empty() && levels.size() != 1U)) {
            throw std::invalid_argument(
                "cache path and digest require exactly one benchmark level");
        }
#endif

        const mpz_class prime(
            "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237");
        const oneshotsea::Field field(prime);
        const oneshotsea::Curve cold_curve(field, 2, 3);
        mpz_class warm_j = field.add(cold_curve.j_invariant(), 1);
        while (warm_j == 0 || warm_j == field.normalize(1728)) {
            warm_j = field.add(warm_j, 1);
        }
        const oneshotsea::Curve warm_curve =
            oneshotsea::short_weierstrass_curve_from_j(field, warm_j);

        for (const std::uint64_t ell : levels) {
#if defined(ONESHOTSEA_BENCHMARK_LEGACY_CONTEXT)
            const auto context =
                oneshotsea::make_classical_direct_sea_context(
                    field, {ell}, 10000000U, 1000000U, threads);
#else
            const auto context = cache_path.empty()
                ? oneshotsea::make_classical_direct_sea_context(
                      field, {ell}, 10000000U, 1000000U, threads)
                : oneshotsea::load_classical_direct_context_cache(
                      field, {ell}, 10000000U, 1000000U, threads,
                      cache_path, cache_sha256);
#endif
            const Clock::time_point cold_start = Clock::now();
            const oneshotsea::WeberSeaResult cold =
                evaluate(cold_curve, context);
            const std::uint64_t cold_us = elapsed_us(cold_start);
            const Clock::time_point warm_start = Clock::now();
            const oneshotsea::WeberSeaResult warm =
                evaluate(warm_curve, context);
            const std::uint64_t warm_us = elapsed_us(warm_start);
            const Clock::time_point validation_start = Clock::now();
            std::optional<std::uint64_t> cold_schoof;
            std::optional<std::uint64_t> warm_schoof;
            if (!skip_schoof) {
                cold_schoof =
                    validate_against_schoof(cold_curve, cold, ell);
                warm_schoof =
                    validate_against_schoof(warm_curve, warm, ell);
            }
            const std::uint64_t validation_us =
                elapsed_us(validation_start);
            const auto& cold_record = cold.classical_direct_levels.front();
            const auto& warm_record = warm.classical_direct_levels.front();

            std::cout
                << "{\"schema\":\"oneshotsea.p125-classical-direct-benchmark.v1\""
                << ",\"ell\":\"" << ell << "\""
                << ",\"thread_limit\":\"" << threads << "\""
                << ",\"preparation_us\":\"" << context.preparation_us()
#if defined(ONESHOTSEA_BENCHMARK_LEGACY_CONTEXT)
                << "\",\"cache_loaded\":false"
                << ",\"cache_load_us\":\"0"
#else
                << "\",\"cache_loaded\":"
                << (context.loaded_from_cache() ? "true" : "false")
                << ",\"cache_load_us\":\"" << context.cache_load_us()
                << "\",\"cached_level_load_count\":\""
                << context.cached_level_load_count()
                << "\",\"cached_level_load_us\":\""
                << context.cached_level_load_us()
                << "\",\"peak_cached_resident_contexts\":\""
                << context.peak_cached_resident_context_count()
                << "\",\"final_cached_resident_contexts\":\""
                << context.cached_resident_context_count()
#endif
                << "\",\"cold_us\":\"" << cold_us
                << "\",\"warm_distinct_j_us\":\"" << warm_us
                << "\",\"validation_us\":\"" << validation_us
                << "\",\"auxiliary_prime_count\":\""
                << cold_record.auxiliary_prime_count
                << "\",\"class_number\":\""
                << cold_record.class_number
#if defined(ONESHOTSEA_BENCHMARK_LEGACY_CONTEXT)
                << "\",\"matrix_coefficients\":null"
                << ",\"matrix_payload_bytes\":null"
#else
                << "\",\"matrix_coefficients\":\""
                << context.interpolation_coefficient_count()
                << "\",\"matrix_payload_bytes\":\""
                << context.interpolation_storage_bytes() << "\""
#endif
                << ",\"cold_exact\":"
                << (cold_record.exact ? "true" : "false")
                << ",\"warm_exact\":"
                << (warm_record.exact ? "true" : "false")
                << ",\"cold_schoof_residue\":";
            if (cold_schoof.has_value()) {
                std::cout << '"' << *cold_schoof << '"';
            } else {
                std::cout << "null";
            }
            std::cout << ",\"warm_schoof_residue\":";
            if (warm_schoof.has_value()) {
                std::cout << '"' << *warm_schoof << '"';
            } else {
                std::cout << "null";
            }
            std::cout << ",\"process_peak_rss_bytes\":\""
                << peak_rss_bytes() << "\"}\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "p125 classical direct benchmark failed: "
                  << error.what() << '\n';
        return 1;
    }
}
