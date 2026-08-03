#include "oneshotsea/direct_context_cache.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/x1_27_probe.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kSeed = UINT64_C(202607300000);
constexpr std::uint64_t kGlobalIndex = UINT64_C(1000030);
constexpr std::uint64_t kPrimeCandidateCap = UINT64_C(10000000);
constexpr std::uint64_t kXCandidateCap = UINT64_C(1000000);
constexpr std::string_view kFreshTimingScope =
    "fresh_preparation_plus_evaluation";
constexpr std::string_view kCachedTimingScope =
    "cached_level_materialization_plus_evaluation_index_excluded";

mpz_class target_prime() {
    return mpz_class(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237");
}

mpz_class oracle_trace() {
    // Obtained separately from the authenticated table-backed SEA path.
    // The paths share downstream BMSS/Frobenius and trace-state code.
    // It is used only after a direct level has committed its result.
    return mpz_class(
        "-534284869337319737295513917655253909609824180266230842767530862");
}

std::vector<std::uint64_t> completing_exact_schedule() {
    return {
        5U,   23U,  29U,  31U,  37U,  41U,  43U,  53U,  67U,  71U,
        73U,  101U, 127U, 137U, 139U, 151U, 157U, 179U, 197U, 199U,
        211U, 223U, 229U, 233U, 239U, 241U, 251U, 263U, 269U, 271U,
    };
}

std::uint64_t parse_u64(std::string_view text, const char* label) {
    if (text.empty() || text.front() == '-' || text.front() == '+') {
        throw std::invalid_argument(std::string(label) +
                                    " must be an unsigned integer");
    }
    std::size_t consumed = 0U;
    const unsigned long long value =
        std::stoull(std::string(text), &consumed, 10);
    if (consumed != text.size() ||
        value > std::numeric_limits<std::uint64_t>::max()) {
        throw std::invalid_argument(std::string(label) +
                                    " is outside uint64");
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t elapsed_us(Clock::time_point started) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started).count();
    if (elapsed < 0) {
        throw std::logic_error("steady clock moved backwards");
    }
    return static_cast<std::uint64_t>(elapsed);
}

std::uint64_t checked_add_us(std::uint64_t left, std::uint64_t right,
                             const char* label) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error(label);
    }
    return left + right;
}

oneshotsea::ExactTracePrior trace_prior(
    const mpz_class& prime, const oneshotsea::X127ProbeSample& sample) {
    if (sample.group_divisor == 0U) {
        throw std::logic_error("fixed X1(27) sample has no group divisor");
    }
    const std::uint64_t p_plus_one =
        (mpz_fdiv_ui(prime.get_mpz_t(), sample.group_divisor) + 1U) %
        sample.group_divisor;
    const std::uint64_t residue =
        sample.selected_side == oneshotsea::X127CanonicalSide::curve
        ? p_plus_one
        : (sample.group_divisor - p_plus_one) % sample.group_divisor;
    return oneshotsea::ExactTracePrior(
        prime, sample.group_divisor, residue);
}

void validate_schedule(const std::vector<std::uint64_t>& levels) {
    if (levels.empty()) {
        throw std::invalid_argument("direct validation schedule is empty");
    }
    for (std::size_t index = 0U; index < levels.size(); ++index) {
        const std::uint64_t ell = levels[index];
        if (ell <= 3U || !oneshotsea::is_prime_u64(ell) ||
            (index != 0U && levels[index - 1U] >= ell)) {
            throw std::invalid_argument(
                "direct validation levels must be increasing distinct primes greater than three");
        }
    }
}

void print_level(
    std::size_t index,
    const oneshotsea::ClassicalDirectSeaLevelRecord& record,
    std::uint64_t expected_residue, bool oracle_accepted,
    std::size_t matrix_payload_bytes, std::uint64_t preparation_us,
    std::uint64_t total_us, std::string_view timing_scope) {
    std::cout
        << "{\"schema\":\"oneshotsea.p125-direct-trace-level.v1\""
        << ",\"index\":\"" << index << "\""
        << ",\"ell\":\"" << record.ell << "\""
        << ",\"exact\":" << (record.exact ? "true" : "false")
        << ",\"trace_residue\":";
    if (record.trace_residue.has_value()) {
        std::cout << '"' << *record.trace_residue << '"';
    } else {
        std::cout << "null";
    }
    std::cout
        << ",\"oracle_residue\":\"" << expected_residue << "\""
        << ",\"oracle_accepted\":"
        << (oracle_accepted ? "true" : "false")
        << ",\"atkin_projective_order\":";
    if (record.atkin_projective_order.has_value()) {
        std::cout << '"' << *record.atkin_projective_order << '"';
    } else {
        std::cout << "null";
    }
    std::cout
        << ",\"atkin_residue_count\":\""
        << record.atkin_residue_count << "\""
        << ",\"exact_modulus\":\"" << record.exact_modulus << "\""
        << ",\"exact_trace_candidate_count\":\""
        << record.exact_trace_candidate_count << "\""
        << ",\"auxiliary_prime_count\":\""
        << record.auxiliary_prime_count << "\""
        << ",\"class_number\":\"" << record.class_number << "\""
        << ",\"matrix_payload_bytes\":\""
        << matrix_payload_bytes << "\""
        << ",\"preparation_us\":\"" << preparation_us
        << "\",\"evaluation_us\":\"" << record.elapsed_us
        << "\",\"total_us\":\"" << total_us
        << "\",\"timing_scope\":\"" << timing_scope << "\"}\n";
    std::cout.flush();
}

bool oracle_accepts(
    const oneshotsea::ClassicalDirectSeaLevelRecord& record,
    const oneshotsea::WeberSeaResult& state,
    std::uint64_t expected_residue) {
    if (record.exact) {
        return record.trace_residue.has_value() &&
               *record.trace_residue == expected_residue;
    }
    if (!record.atkin_projective_order.has_value()) {
        // An unconstrained level makes no assertion about the trace.
        return true;
    }
    const auto constraint = std::find_if(
        state.atkin_constraints.begin(), state.atkin_constraints.end(),
        [&record](const oneshotsea::AtkinConstraint& candidate) {
            return candidate.ell == record.ell;
        });
    return constraint != state.atkin_constraints.end() &&
           std::find(constraint->trace_residues.begin(),
                     constraint->trace_residues.end(),
                     expected_residue) != constraint->trace_residues.end();
}

int run(std::size_t threads, const std::vector<std::uint64_t>& levels,
        bool require_completion, const std::string& cache_path,
        const std::string& cache_sha256) {
    validate_schedule(levels);
    const mpz_class prime = target_prime();
    const oneshotsea::Field field(prime);
    const oneshotsea::X127ProbeResult generated =
        oneshotsea::deterministic_x1_27_search_curve(
            prime, kSeed, kGlobalIndex, true);
    if (!generated.sample.has_value()) {
        throw std::runtime_error(
            "fixed X1(27) input did not reproduce its curve");
    }
    const oneshotsea::X127ProbeSample& sample = *generated.sample;
    const oneshotsea::ExactTracePrior prior = trace_prior(prime, sample);
    const mpz_class expected_trace = oracle_trace();
    if (mpz_fdiv_ui(expected_trace.get_mpz_t(), prior.modulus()) !=
            prior.residue() ||
        expected_trace * expected_trace > 4 * prime) {
        throw std::logic_error(
            "authenticated reference trace contradicts the X1(27) prior or Hasse");
    }

    oneshotsea::TraceConstraints initial(prime);
    initial.refine_exact(prior.modulus(), prior.residue());
    oneshotsea::WeberSeaResult state{
        initial, initial, {}, {}, {}, {}, std::nullopt, {},
        oneshotsea::SeaCurveModelBinding{
            sample.pair.curve.a(), sample.pair.curve.b()}};

    std::unique_ptr<oneshotsea::ClassicalDirectSeaContext> cached_context;
    std::uint64_t cached_total_us = 0U;
    if (!cache_path.empty()) {
        const Clock::time_point started = Clock::now();
        cached_context =
            std::make_unique<oneshotsea::ClassicalDirectSeaContext>(
                oneshotsea::load_classical_direct_context_cache(
                    field, levels, kPrimeCandidateCap, kXCandidateCap,
                    threads, cache_path, cache_sha256));
        std::vector<std::uint64_t> cached_level_total_us;
        cached_level_total_us.reserve(levels.size());
        std::uint64_t previous_level_load_us = 0U;
        oneshotsea::extend_sea_with_prepared_classical_direct(
            sample.pair.curve, state, *cached_context, 1U,
            [&](const oneshotsea::ClassicalDirectSeaLevelRecord& record) {
                const std::uint64_t current_level_load_us =
                    cached_context->cached_level_load_us();
                if (current_level_load_us < previous_level_load_us) {
                    throw std::logic_error(
                        "cached-level load timing moved backwards");
                }
                const std::uint64_t materialization_us =
                    current_level_load_us - previous_level_load_us;
                cached_level_total_us.push_back(checked_add_us(
                    materialization_us, record.elapsed_us,
                    "cached direct level total timing overflow"));
                previous_level_load_us = current_level_load_us;
            });
        cached_total_us = elapsed_us(started);
        if (cached_level_total_us.size() !=
                state.classical_direct_levels.size() ||
            previous_level_load_us !=
                cached_context->cached_level_load_us()) {
            throw std::logic_error(
                "cached direct level timing attribution is inconsistent");
        }
        for (std::size_t index = 0U;
             index < state.classical_direct_levels.size(); ++index) {
            const auto& record = state.classical_direct_levels[index];
            const std::uint64_t expected_residue =
                mpz_fdiv_ui(expected_trace.get_mpz_t(), record.ell);
            const bool accepted =
                oracle_accepts(record, state, expected_residue);
            print_level(index, record, expected_residue, accepted,
                        cached_context->interpolation_storage_bytes(index),
                        0U, cached_level_total_us[index],
                        kCachedTimingScope);
            if (!accepted) {
                throw std::runtime_error(
                    "cached direct level contradicts the authenticated p125 reference trace");
            }
        }
    }

    for (std::size_t index = 0U;
         cache_path.empty() && index < levels.size(); ++index) {
        if (state.traces.has_value()) {
            break;
        }
        const std::uint64_t ell = levels[index];
        const std::size_t retained_before =
            state.classical_direct_levels.size();
        const Clock::time_point started = Clock::now();
        oneshotsea::ClassicalDirectSeaContext context =
            oneshotsea::make_classical_direct_sea_context(
                field, {ell}, kPrimeCandidateCap, kXCandidateCap, threads);
        oneshotsea::extend_sea_with_prepared_classical_direct(
            sample.pair.curve, state, context, 1U);
        const std::uint64_t total = elapsed_us(started);
        if (state.classical_direct_levels.size() != retained_before + 1U) {
            throw std::logic_error(
                "direct p125 validation did not retain exactly one level");
        }
        const oneshotsea::ClassicalDirectSeaLevelRecord& record =
            state.classical_direct_levels.back();
        const std::uint64_t expected_residue =
            mpz_fdiv_ui(expected_trace.get_mpz_t(), ell);
        const bool accepted = oracle_accepts(record, state, expected_residue);
        print_level(index, record, expected_residue, accepted,
                    context.interpolation_storage_bytes(),
                    context.preparation_us(), total, kFreshTimingScope);
        if (!accepted) {
            throw std::runtime_error(
                "direct level contradicts the authenticated p125 reference trace");
        }
    }

    const bool complete = state.traces.has_value() &&
        state.traces->size() == 1U;
    const bool exact_match = complete && state.traces->front() ==
        oracle_trace();
    std::cout
        << "{\"schema\":\"oneshotsea.p125-direct-trace-summary.v1\""
        << ",\"complete\":" << (complete ? "true" : "false")
        << ",\"oracle_match\":" << (exact_match ? "true" : "false")
        << ",\"retained_levels\":\""
        << state.classical_direct_levels.size() << "\""
        << ",\"exact_modulus\":\"" << state.constraints.modulus()
        << "\",\"exact_trace_candidate_count\":\""
        << state.constraints.candidate_count() << "\""
        << ",\"cache\":";
    if (cached_context) {
        std::cout
            << "{\"authenticated\":true,\"index_load_us\":\""
            << cached_context->cache_load_us()
            << "\",\"level_load_count\":\""
            << cached_context->cached_level_load_count()
            << "\",\"level_load_us\":\""
            << cached_context->cached_level_load_us()
            << "\",\"peak_resident_contexts\":\""
            << cached_context->peak_cached_resident_context_count()
            << "\",\"final_resident_contexts\":\""
            << cached_context->cached_resident_context_count()
            << "\",\"total_us\":\"" << cached_total_us << "\"}";
    } else {
        std::cout << "null";
    }
    std::cout << ",\"trace\":";
    if (complete) {
        std::cout << '"' << state.traces->front() << '"';
    } else {
        std::cout << "null";
    }
    std::cout << "}\n";
    if (require_completion && (!complete || !exact_match)) {
        throw std::runtime_error(
            "default direct schedule did not reproduce the unique p125 trace");
    }
    return EXIT_SUCCESS;
}

void usage(const char* executable) {
    std::cerr << "usage: " << executable
              << " [--threads N] [--cache PATH --cache-sha256 DIGEST] [ELL ...]\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::size_t threads = 4U;
        std::vector<std::uint64_t> levels;
        std::string cache_path;
        std::string cache_sha256;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--threads") {
                if (++index >= argc) {
                    throw std::invalid_argument(
                        "--threads requires a value");
                }
                const std::uint64_t parsed =
                    parse_u64(argv[index], "thread count");
                if (parsed == 0U ||
                    parsed > std::numeric_limits<std::size_t>::max()) {
                    throw std::invalid_argument(
                        "thread count is outside size_t");
                }
                threads = static_cast<std::size_t>(parsed);
            } else if (argument == "--cache") {
                if (++index >= argc) {
                    throw std::invalid_argument("--cache requires a path");
                }
                cache_path = argv[index];
            } else if (argument == "--cache-sha256") {
                if (++index >= argc) {
                    throw std::invalid_argument(
                        "--cache-sha256 requires a digest");
                }
                cache_sha256 = argv[index];
            } else if (argument == "--help") {
                usage(argv[0]);
                return EXIT_SUCCESS;
            } else {
                levels.push_back(parse_u64(argument, "SEA level"));
            }
        }
        const bool default_schedule = levels.empty();
        if (default_schedule) {
            levels = completing_exact_schedule();
        }
        if (cache_path.empty() != cache_sha256.empty()) {
            throw std::invalid_argument(
                "--cache and --cache-sha256 must be supplied together");
        }
        return run(threads, levels, default_schedule, cache_path,
                   cache_sha256);
    } catch (const std::exception& error) {
        std::cerr << "p125 direct trace validation failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
