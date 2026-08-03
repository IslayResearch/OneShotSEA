#include "oneshotsea/direct_context_cache.hpp"
#include "oneshotsea/schoof.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/x1_27_probe.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sys/resource.h>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    mpz_class prime;
    std::uint64_t seed = UINT64_C(202607300000);
    std::uint64_t range_start = 0U;
    std::uint64_t count = 0U;
    std::size_t threads = 4U;
    bool require_point_four = true;
    std::string cache_path;
    std::string cache_sha256;
    std::size_t cache_resident_bytes = 0U;
    std::uint64_t schoof_through = 0U;
    std::uint64_t maximum_prime_candidates = UINT64_C(1000000);
    std::uint64_t maximum_x_candidates = UINT64_C(1000000);
    std::vector<std::uint64_t> levels;
};

struct Aggregate {
    std::uint64_t samples = 0U;
    std::uint64_t exact = 0U;
    std::uint64_t atkin = 0U;
    std::uint64_t unconstrained = 0U;
    std::uint64_t information_microbits = 0U;
    std::uint64_t evaluation_us = 0U;
    std::uint64_t materialization_us = 0U;
    std::uint64_t materializations = 0U;
    std::uint64_t schoof_attempts = 0U;
    std::uint64_t schoof_validations = 0U;
    std::uint64_t schoof_us = 0U;
};

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

std::uint64_t elapsed_us(Clock::time_point start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start).count();
    if (elapsed < 0) {
        throw std::logic_error("steady clock moved backwards");
    }
    return static_cast<std::uint64_t>(elapsed);
}

void checked_add(std::uint64_t& target, std::uint64_t increment,
                 const char* label) {
    if (increment > std::numeric_limits<std::uint64_t>::max() - target) {
        throw std::overflow_error(label);
    }
    target += increment;
}

std::uint64_t peak_rss_bytes() {
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) {
        throw std::runtime_error("getrusage failed");
    }
    const auto encoded = static_cast<unsigned long long>(usage.ru_maxrss);
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(encoded);
#else
    if (encoded > std::numeric_limits<std::uint64_t>::max() / 1024ULL) {
        throw std::overflow_error("peak RSS byte count overflows uint64");
    }
    return static_cast<std::uint64_t>(encoded * 1024ULL);
#endif
}

oneshotsea::ExactTracePrior trace_prior(
    const mpz_class& prime, const oneshotsea::X127ProbeSample& sample) {
    if (sample.group_divisor == 0U) {
        throw std::logic_error("X1(27) sample has no group divisor");
    }
    const std::uint64_t positive =
        (mpz_fdiv_ui(prime.get_mpz_t(), sample.group_divisor) + 1U) %
        sample.group_divisor;
    const std::uint64_t residue =
        sample.selected_side == oneshotsea::X127CanonicalSide::curve
            ? positive
            : (sample.group_divisor - positive) % sample.group_divisor;
    return oneshotsea::ExactTracePrior(
        prime, sample.group_divisor, residue);
}

std::uint64_t information_microbits(
    const oneshotsea::ClassicalDirectSeaLevelRecord& record) {
    std::size_t residues = 0U;
    if (record.exact) {
        residues = 1U;
    } else if (record.atkin_projective_order.has_value()) {
        residues = record.atkin_residue_count;
    } else {
        return 0U;
    }
    if (residues == 0U || residues > record.ell) {
        throw std::logic_error(
            "direct level retained an invalid information residue count");
    }
    const long double bits = std::log2(
        static_cast<long double>(record.ell) /
        static_cast<long double>(residues));
    const long double scaled = bits * 1000000.0L;
    if (!(scaled >= 0.0L) ||
        scaled > static_cast<long double>(
                     std::numeric_limits<std::uint64_t>::max())) {
        throw std::overflow_error("direct information score is out of range");
    }
    return static_cast<std::uint64_t>(std::llround(scaled));
}

bool validate_schoof_control(
    const mpz_class& prime,
    const oneshotsea::ClassicalDirectSeaLevelRecord& record,
    std::uint64_t residue) {
    if (record.exact) {
        return record.trace_residue.has_value() &&
               *record.trace_residue == residue;
    }
    if (!record.atkin_projective_order.has_value()) {
        // An unconstrained direct result makes no mathematical claim for an
        // independent residue to corroborate.
        return true;
    }
    const std::uint64_t control_order =
        oneshotsea::projective_frobenius_order(
            record.ell, prime, residue);
    const std::vector<std::uint64_t> allowed =
        oneshotsea::atkin_trace_residues_from_projective_order(
            record.ell, prime, *record.atkin_projective_order);
    return control_order == *record.atkin_projective_order &&
           allowed.size() == record.atkin_residue_count &&
           std::find(allowed.begin(), allowed.end(), residue) !=
               allowed.end();
}

void validate_options(const Options& options) {
    if (options.prime <= 3 ||
        mpz_probab_prime_p(options.prime.get_mpz_t(), 25) == 0 ||
        mpz_fdiv_ui(options.prime.get_mpz_t(), 4U) != 1U) {
        throw std::invalid_argument(
            "cohort target must be a probable prime congruent to one modulo four");
    }
    if (options.count == 0U ||
        options.count - 1U >
            std::numeric_limits<std::uint64_t>::max() -
                options.range_start) {
        throw std::invalid_argument("cohort range is empty or overflows");
    }
    if (options.threads == 0U) {
        throw std::invalid_argument("cohort thread limit must be positive");
    }
    if (options.cache_path.empty() || options.cache_sha256.empty() ||
        options.levels.empty()) {
        throw std::invalid_argument(
            "cohort profiling requires a cache, digest, and levels");
    }
    if (options.maximum_prime_candidates == 0U ||
        options.maximum_x_candidates == 0U) {
        throw std::invalid_argument(
            "cohort direct-construction caps must be positive");
    }
    if (!std::is_sorted(options.levels.begin(), options.levels.end()) ||
        std::adjacent_find(options.levels.begin(), options.levels.end()) !=
            options.levels.end()) {
        throw std::invalid_argument(
            "cohort levels must be strictly increasing");
    }
    for (const std::uint64_t ell : options.levels) {
        if (ell <= 3U || ell > std::numeric_limits<unsigned>::max() ||
            !oneshotsea::is_prime_u64(ell)) {
            throw std::invalid_argument(
                "cohort levels must be odd primes greater than three");
        }
    }
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        const auto value = [&](const char* label) -> std::string_view {
            if (++index >= argc) {
                throw std::invalid_argument(std::string(label) +
                                            " requires a value");
            }
            return argv[index];
        };
        if (argument == "--p") {
            options.prime = oneshotsea::parse_integer(
                std::string(value("--p")));
        } else if (argument == "--seed") {
            options.seed = parse_u64(value("--seed"), "seed");
        } else if (argument == "--range-start") {
            options.range_start = parse_u64(
                value("--range-start"), "range start");
        } else if (argument == "--count") {
            options.count = parse_u64(value("--count"), "count");
        } else if (argument == "--threads") {
            const std::uint64_t parsed = parse_u64(
                value("--threads"), "thread limit");
            if (parsed > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("thread limit exceeds size_t");
            }
            options.threads = static_cast<std::size_t>(parsed);
        } else if (argument == "--require-point4") {
            const std::uint64_t parsed = parse_u64(
                value("--require-point4"), "point-four policy");
            if (parsed > 1U) {
                throw std::invalid_argument(
                    "--require-point4 must be zero or one");
            }
            options.require_point_four = parsed != 0U;
        } else if (argument == "--cache") {
            options.cache_path = value("--cache");
        } else if (argument == "--cache-sha256") {
            options.cache_sha256 = value("--cache-sha256");
        } else if (argument == "--cache-resident-bytes") {
            const std::uint64_t parsed = parse_u64(
                value("--cache-resident-bytes"),
                "cache residency budget");
            if (parsed > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument(
                    "cache residency budget exceeds size_t");
            }
            options.cache_resident_bytes =
                static_cast<std::size_t>(parsed);
        } else if (argument == "--schoof-through") {
            options.schoof_through = parse_u64(
                value("--schoof-through"), "Schoof control level");
        } else if (argument == "--maximum-prime-candidates") {
            options.maximum_prime_candidates = parse_u64(
                value("--maximum-prime-candidates"),
                "maximum prime candidates");
        } else if (argument == "--maximum-x-candidates") {
            options.maximum_x_candidates = parse_u64(
                value("--maximum-x-candidates"),
                "maximum x candidates");
        } else if (argument == "--help") {
            std::cout
                << "usage: profile_classical_direct_cohort --p P --range-start I --count N --cache PATH --cache-sha256 DIGEST [--seed S] [--threads N] [--require-point4 0|1] [--cache-resident-bytes N] [--schoof-through ELL] [--maximum-prime-candidates N] [--maximum-x-candidates N] ELL...\n";
            std::exit(EXIT_SUCCESS);
        } else {
            options.levels.push_back(parse_u64(argument, "SEA level"));
        }
    }
    validate_options(options);
    return options;
}

std::vector<std::uint64_t> score_order(
    const std::vector<std::uint64_t>& levels,
    const std::vector<Aggregate>& aggregates, bool include_materialization) {
    if (levels.size() != aggregates.size()) {
        throw std::logic_error(
            "cohort scheduling levels and aggregates disagree");
    }
    std::vector<oneshotsea::WeberSeaLevelEstimate> estimates;
    estimates.reserve(levels.size());
    for (std::size_t index = 0U; index < levels.size(); ++index) {
        const Aggregate& aggregate = aggregates[index];
        if (include_materialization &&
            aggregate.materialization_us >
                std::numeric_limits<std::uint64_t>::max() -
                    aggregate.evaluation_us) {
            throw std::overflow_error("cohort scheduling cost overflow");
        }
        const std::uint64_t cost = aggregate.evaluation_us +
            (include_materialization ? aggregate.materialization_us : 0U);
        if (cost == 0U) {
            throw std::logic_error("cohort scheduling cost is zero");
        }
        estimates.push_back(oneshotsea::WeberSeaLevelEstimate{
            levels[index], aggregate.information_microbits, cost});
    }
    return oneshotsea::expected_information_per_cost_order(
        levels, estimates);
}

void print_order(const std::vector<std::uint64_t>& levels) {
    std::cout << '[';
    for (std::size_t index = 0U; index < levels.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << '"' << levels[index] << '"';
    }
    std::cout << ']';
}

int run(const Options& options) {
    const oneshotsea::Field field(options.prime);
    auto context = oneshotsea::load_classical_direct_context_cache(
        field, options.levels, options.maximum_prime_candidates,
        options.maximum_x_candidates, options.threads,
        options.cache_path, options.cache_sha256);
    context.set_cached_context_residency_budget_bytes(
        options.cache_resident_bytes);
    std::vector<Aggregate> aggregates(options.levels.size());
    const Clock::time_point cohort_start = Clock::now();
    std::uint64_t total_generation_us = 0U;

    for (std::uint64_t offset = 0U; offset < options.count; ++offset) {
        const std::uint64_t global_index = options.range_start + offset;
        const Clock::time_point generation_start = Clock::now();
        const oneshotsea::X127ProbeResult generated =
            oneshotsea::deterministic_x1_27_search_curve(
                options.prime, options.seed, global_index,
                options.require_point_four);
        if (!generated.sample.has_value()) {
            throw std::runtime_error(
                "deterministic X1(27) search returned no sample");
        }
        const oneshotsea::X127ProbeSample& sample = *generated.sample;
        const std::uint64_t generation_us = elapsed_us(generation_start);
        checked_add(total_generation_us, generation_us,
                    "cohort generation timing overflow");
        const oneshotsea::ExactTracePrior prior =
            trace_prior(options.prime, sample);
        oneshotsea::TraceConstraints initial(options.prime);
        initial.refine_exact(prior.modulus(), prior.residue());
        oneshotsea::WeberSeaResult state{
            initial, initial, {}, {}, {}, {}, std::nullopt, {}};
        std::uint64_t previous_load_count =
            context.cached_level_load_count();
        std::uint64_t previous_load_us = context.cached_level_load_us();
        std::uint64_t curve_evaluation_us = 0U;
        std::uint64_t curve_materialization_us = 0U;
        std::uint64_t curve_schoof_us = 0U;
        std::size_t reached = 0U;
        const Clock::time_point profile_start = Clock::now();
        oneshotsea::extend_sea_with_prepared_classical_direct(
            sample.pair.curve, state, context, 1U,
            [&](const oneshotsea::ClassicalDirectSeaLevelRecord& record) {
                if (reached >= options.levels.size() ||
                    record.ell != options.levels[reached]) {
                    throw std::logic_error(
                        "cohort direct level order changed");
                }
                const std::uint64_t current_load_count =
                    context.cached_level_load_count();
                const std::uint64_t current_load_us =
                    context.cached_level_load_us();
                if (current_load_count < previous_load_count ||
                    current_load_us < previous_load_us) {
                    throw std::logic_error(
                        "cohort cache telemetry moved backwards");
                }
                const std::uint64_t load_count =
                    current_load_count - previous_load_count;
                const std::uint64_t load_us =
                    current_load_us - previous_load_us;
                previous_load_count = current_load_count;
                previous_load_us = current_load_us;
                if (load_count > 1U) {
                    throw std::logic_error(
                        "one cohort level triggered multiple materializations");
                }
                const std::uint64_t information =
                    information_microbits(record);
                std::optional<std::uint64_t> schoof_residue;
                std::uint64_t schoof_us = 0U;
                if (record.ell <= options.schoof_through) {
                    const Clock::time_point schoof_start = Clock::now();
                    schoof_residue = oneshotsea::schoof_trace_mod_ell(
                        sample.pair.curve, record.ell);
                    schoof_us = elapsed_us(schoof_start);
                    if (!validate_schoof_control(
                            options.prime, record, *schoof_residue)) {
                        throw std::runtime_error(
                            "direct cohort result disagrees with independent Schoof control");
                    }
                }
                Aggregate& aggregate = aggregates[reached];
                checked_add(aggregate.samples, 1U,
                            "cohort sample count overflow");
                checked_add(aggregate.exact, record.exact ? 1U : 0U,
                            "cohort exact count overflow");
                checked_add(
                    aggregate.atkin,
                    !record.exact &&
                            record.atkin_projective_order.has_value()
                        ? 1U
                        : 0U,
                    "cohort Atkin count overflow");
                checked_add(
                    aggregate.unconstrained,
                    !record.exact &&
                            !record.atkin_projective_order.has_value()
                        ? 1U
                        : 0U,
                    "cohort unconstrained count overflow");
                checked_add(aggregate.information_microbits, information,
                            "cohort information overflow");
                checked_add(aggregate.evaluation_us, record.elapsed_us,
                            "cohort evaluation timing overflow");
                checked_add(aggregate.materialization_us, load_us,
                            "cohort materialization timing overflow");
                checked_add(aggregate.materializations, load_count,
                            "cohort materialization count overflow");
                checked_add(aggregate.schoof_attempts,
                            schoof_residue.has_value() ? 1U : 0U,
                            "cohort Schoof attempt count overflow");
                const bool schoof_applicable =
                    schoof_residue.has_value() &&
                    (record.exact ||
                     record.atkin_projective_order.has_value());
                checked_add(aggregate.schoof_validations,
                            schoof_applicable ? 1U : 0U,
                            "cohort Schoof validation count overflow");
                checked_add(aggregate.schoof_us, schoof_us,
                            "cohort Schoof timing overflow");
                checked_add(curve_evaluation_us, record.elapsed_us,
                            "curve evaluation timing overflow");
                checked_add(curve_materialization_us, load_us,
                            "curve materialization timing overflow");
                checked_add(curve_schoof_us, schoof_us,
                            "curve Schoof timing overflow");

                std::cout
                    << "{\"schema\":\"oneshotsea.classical-direct-cohort-level.v1\""
                    << ",\"global_index\":\"" << global_index << "\""
                    << ",\"curve_j\":\"" << sample.pair.j_invariant << "\""
                    << ",\"selected_side\":\""
                    << oneshotsea::x1_27_canonical_side_name(
                           sample.selected_side)
                    << "\",\"trace_prior_modulus\":\"" << prior.modulus()
                    << "\",\"trace_prior_residue\":\"" << prior.residue()
                    << "\",\"ell\":\"" << record.ell << "\""
                    << ",\"exact\":" << (record.exact ? "true" : "false")
                    << ",\"trace_residue\":";
                if (record.trace_residue.has_value()) {
                    std::cout << '"' << *record.trace_residue << '"';
                } else {
                    std::cout << "null";
                }
                std::cout << ",\"atkin_projective_order\":";
                if (record.atkin_projective_order.has_value()) {
                    std::cout << '"' << *record.atkin_projective_order << '"';
                } else {
                    std::cout << "null";
                }
                std::cout
                    << ",\"atkin_residue_count\":\""
                    << record.atkin_residue_count
                    << "\",\"information_microbits\":\"" << information
                    << "\",\"evaluation_us\":\"" << record.elapsed_us
                    << "\",\"materialization_count\":\"" << load_count
                    << "\",\"materialization_us\":\"" << load_us
                    << "\",\"schoof_residue\":";
                if (schoof_residue.has_value()) {
                    std::cout << '"' << *schoof_residue << '"';
                } else {
                    std::cout << "null";
                }
                std::cout
                    << ",\"schoof_control_applicable\":"
                    << (schoof_applicable ? "true" : "false")
                    << ",\"schoof_us\":\"" << schoof_us
                    << "\",\"process_peak_rss_bytes\":\""
                    << peak_rss_bytes() << "\"}\n";
                std::cout.flush();
                ++reached;
            });
        if (reached != options.levels.size()) {
            throw std::runtime_error(
                "cohort trace completed before every profiling level; use a shorter schedule or independent level contexts");
        }
        std::cout
            << "{\"schema\":\"oneshotsea.classical-direct-cohort-curve.v1\""
            << ",\"global_index\":\"" << global_index
            << "\",\"levels\":\"" << reached
            << "\",\"generation_us\":\"" << generation_us
            << "\",\"evaluation_us\":\"" << curve_evaluation_us
            << "\",\"materialization_us\":\""
            << curve_materialization_us
            << "\",\"schoof_us\":\"" << curve_schoof_us
            << "\",\"profile_us\":\"" << elapsed_us(profile_start)
            << "\",\"process_peak_rss_bytes\":\"" << peak_rss_bytes()
            << "\"}\n";
        std::cout.flush();
    }

    const std::vector<std::uint64_t> warm_order =
        score_order(options.levels, aggregates, false);
    const std::vector<std::uint64_t> observed_order =
        score_order(options.levels, aggregates, true);
    std::cout
        << "{\"schema\":\"oneshotsea.classical-direct-cohort-summary.v1\""
        << ",\"prime\":\"" << options.prime
        << "\",\"seed\":\"" << options.seed
        << "\",\"range_start\":\"" << options.range_start
        << "\",\"count\":\"" << options.count
        << "\",\"threads\":\"" << options.threads
        << "\",\"require_point_four\":"
        << (options.require_point_four ? "true" : "false")
        << ",\"maximum_prime_candidates\":\""
        << options.maximum_prime_candidates
        << "\",\"maximum_x_candidates_per_surface\":\""
        << options.maximum_x_candidates
        << "\",\"schoof_through\":\"" << options.schoof_through
        << "\",\"cache_sha256\":\"" << options.cache_sha256
        << "\",\"cache_index_us\":\"" << context.cache_load_us()
        << "\",\"cache_residency_budget_bytes\":\""
        << context.cached_context_residency_budget_bytes()
        << "\",\"cached_level_load_count\":\""
        << context.cached_level_load_count()
        << "\",\"cached_level_load_us\":\""
        << context.cached_level_load_us()
        << "\",\"cached_context_evictions\":\""
        << context.cached_context_eviction_count()
        << "\",\"final_cached_retained_contexts\":\""
        << context.cached_retained_context_count()
        << "\",\"final_cached_retained_payload_bytes\":\""
        << context.cached_retained_payload_bytes()
        << "\",\"process_peak_rss_bytes\":\"" << peak_rss_bytes()
        << "\",\"generation_us\":\"" << total_generation_us
        << "\",\"elapsed_us\":\"" << elapsed_us(cohort_start)
        << "\",\"levels\":[";
    for (std::size_t index = 0U; index < options.levels.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        const Aggregate& aggregate = aggregates[index];
        std::cout
            << "{\"ell\":\"" << options.levels[index]
            << "\",\"samples\":\"" << aggregate.samples
            << "\",\"exact\":\"" << aggregate.exact
            << "\",\"atkin\":\"" << aggregate.atkin
            << "\",\"unconstrained\":\"" << aggregate.unconstrained
            << "\",\"information_microbits\":\""
            << aggregate.information_microbits
            << "\",\"evaluation_us\":\"" << aggregate.evaluation_us
            << "\",\"materializations\":\""
            << aggregate.materializations
            << "\",\"materialization_us\":\""
            << aggregate.materialization_us
            << "\",\"schoof_attempts\":\""
            << aggregate.schoof_attempts
            << "\",\"schoof_validations\":\""
            << aggregate.schoof_validations
            << "\",\"schoof_us\":\"" << aggregate.schoof_us << "\"}";
    }
    std::cout << "],\"warm_information_per_cost_order\":";
    print_order(warm_order);
    std::cout << ",\"observed_information_per_cost_order\":";
    print_order(observed_order);
    std::cout
        << ",\"claim_scope\":\"empirical curve-independent scheduling input; not a certificate-yield estimate or asymptotic proof\"}\n";
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "classical direct cohort profile failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
