#include "oneshotsea/field.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/weber_curve_generator.hpp"
#include "oneshotsea/weber_table_trust.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using OptionsMap = std::map<std::string, std::string>;

struct AuditOptions {
    mpz_class prime;
    std::uint64_t seed = 0;
    std::uint64_t range_start = 0;
    std::uint64_t count = 0;
    std::uint64_t max_level = 0;
    std::size_t trace_cap = 0;
    std::size_t sea_threads = 0;
    std::filesystem::path table_directory;
    bool schoof_fallback = false;
};

void usage() {
    std::cerr
        << "usage: oracle_weber_audit --p P --seed S --range-start I "
           "--count N --max-level L --trace-cap C --sea-threads N "
           "--table-dir PATH --schoof-fallback 0|1\n";
}

OptionsMap parse_options(int argc, char** argv) {
    OptionsMap options;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for final option");
        }
        const std::string option(argv[index]);
        if (option.size() <= 2U || option.substr(0, 2) != "--") {
            throw std::invalid_argument("expected --name value option pair");
        }
        const std::string name = option.substr(2);
        if (!options.emplace(name, argv[index + 1]).second) {
            throw std::invalid_argument("duplicate option: --" + name);
        }
    }
    return options;
}

const std::string& required(const OptionsMap& options,
                            const std::string& name) {
    const auto found = options.find(name);
    if (found == options.end()) {
        throw std::invalid_argument("missing required option: --" + name);
    }
    return found->second;
}

std::uint64_t parse_u64(std::string_view text, const std::string& name) {
    std::uint64_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value, 10);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end) {
        throw std::invalid_argument("--" + name +
                                    " must be a canonical unsigned integer");
    }
    return value;
}

std::size_t parse_size(std::string_view text, const std::string& name,
                       bool allow_zero) {
    const std::uint64_t value = parse_u64(text, name);
    if (!allow_zero && value == 0U) {
        throw std::invalid_argument("--" + name + " must be positive");
    }
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (value > static_cast<std::uint64_t>(
                        std::numeric_limits<std::size_t>::max())) {
            throw std::invalid_argument("--" + name + " is out of range");
        }
    }
    return static_cast<std::size_t>(value);
}

AuditOptions validated_options(int argc, char** argv) {
    const OptionsMap parsed = parse_options(argc, argv);
    static const std::vector<std::string> allowed = {
        "p",          "seed",        "range-start",
        "count",      "max-level",   "trace-cap",
        "sea-threads", "table-dir",  "schoof-fallback",
    };
    for (const auto& [name, value] : parsed) {
        static_cast<void>(value);
        bool known = false;
        for (const std::string& candidate : allowed) {
            if (name == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            throw std::invalid_argument("unknown option: --" + name);
        }
    }

    AuditOptions options;
    options.prime = oneshotsea::parse_integer(required(parsed, "p"));
    options.seed = parse_u64(required(parsed, "seed"), "seed");
    options.range_start =
        parse_u64(required(parsed, "range-start"), "range-start");
    options.count = parse_u64(required(parsed, "count"), "count");
    options.max_level =
        parse_u64(required(parsed, "max-level"), "max-level");
    options.trace_cap =
        parse_size(required(parsed, "trace-cap"), "trace-cap", false);
    options.sea_threads =
        parse_size(required(parsed, "sea-threads"), "sea-threads", true);
    options.table_directory = required(parsed, "table-dir");
    const std::uint64_t fallback = parse_u64(
        required(parsed, "schoof-fallback"), "schoof-fallback");
    if (fallback > 1U) {
        throw std::invalid_argument("--schoof-fallback must be 0 or 1");
    }
    options.schoof_fallback = fallback == 1U;

    if (options.count == 0U) {
        throw std::invalid_argument("--count must be positive");
    }
    if (options.count - 1U >
        std::numeric_limits<std::uint64_t>::max() - options.range_start) {
        throw std::invalid_argument("requested range exceeds UINT64_MAX");
    }
    if (options.max_level < 5U) {
        throw std::invalid_argument("--max-level must be at least 5");
    }
    return options;
}

void write_optional_u64(std::ostream& output,
                        const std::optional<std::uint64_t>& value) {
    if (value.has_value()) {
        output << *value;
    } else {
        output << "null";
    }
}

void write_mpz_vector(std::ostream& output,
                      const std::vector<mpz_class>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        output << '"' << values[index] << '"';
    }
    output << ']';
}

void write_optional_traces(
    std::ostream& output,
    const std::optional<std::vector<mpz_class>>& traces) {
    if (!traces.has_value()) {
        output << "null";
        return;
    }
    write_mpz_vector(output, *traces);
}

void write_level(std::ostream& output,
                 const oneshotsea::WeberSeaLevelRecord& level) {
    const oneshotsea::ElkiesStageTimings& timings = level.timings;
    const char* const classification =
        level.exact
            ? "exact_elkies"
            : (level.atkin_projective_order.has_value()
                   ? "certified_atkin"
                   : "unconstrained");
    output << "{\"ell\":" << level.ell
           << ",\"classification\":\"" << classification << '"'
           << ",\"exact\":" << (level.exact ? "true" : "false")
           << ",\"trace_residue\":";
    write_optional_u64(output, level.trace_residue);
    output << ",\"exact_modulus\":\"" << level.exact_modulus
           << "\",\"constraint_modulus\":\""
           << level.constraint_modulus
           << "\",\"exact_trace_candidate_count\":\""
           << level.exact_trace_candidate_count
           << "\",\"trace_candidate_count\":\""
           << level.trace_candidate_count
           << "\",\"atkin_projective_order\":";
    write_optional_u64(output, level.atkin_projective_order);
    output << ",\"atkin_residue_count\":\"" << level.atkin_residue_count
           << "\",\"compatible_source_lifts\":\""
           << level.compatible_source_lifts
           << "\",\"timings\":{\"modular_root_workers\":\""
           << timings.modular_root_workers
           << "\",\"modular_root_orbits\":\""
           << timings.modular_root_orbits
           << "\",\"modular_root_reused_lifts\":\""
           << timings.modular_root_reused_lifts
           << "\",\"modular_root_orbit_reuse\":"
           << (timings.modular_root_orbit_reuse ? "true" : "false")
           << ",\"source_lifts_us\":\"" << timings.source_lifts_us
           << "\",\"modular_roots_us\":\"" << timings.modular_roots_us
           << "\",\"normalized_codomain_us\":\""
           << timings.normalized_codomain_us << "\",\"bmss_us\":\""
           << timings.bmss_us << "\",\"eigenvalue_us\":\""
           << timings.eigenvalue_us << "\",\"lift_pairs\":\""
           << timings.lift_pairs << "\",\"distinct_codomains\":\""
           << timings.distinct_codomains
           << "\",\"codomain_cache_hits\":\""
           << timings.codomain_cache_hits
           << "\",\"conjugate_eigenvalue_reuse\":"
           << (timings.conjugate_eigenvalue_reuse ? "true" : "false")
           << ",\"eigenvalue_attempts\":\""
           << timings.eigenvalue_attempts
           << "\",\"independent_eigenvalue_recoveries\":\""
           << timings.independent_eigenvalue_recoveries
           << "\",\"conjugate_eigenvalues_derived\":\""
           << timings.conjugate_eigenvalues_derived << "\"}}";
}

void write_levels(std::ostream& output,
                  const std::vector<oneshotsea::WeberSeaLevelRecord>& levels) {
    output << '[';
    for (std::size_t index = 0; index < levels.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        write_level(output, levels[index]);
    }
    output << ']';
}

void write_fallback_level(
    std::ostream& output,
    const oneshotsea::SchoofFallbackLevelRecord& level) {
    output << "{\"ell\":" << level.ell
           << ",\"classification\":\"exact_schoof\""
           << ",\"trace_residue\":" << level.trace_residue
           << ",\"exact_modulus\":\"" << level.exact_modulus
           << "\",\"constraint_modulus\":\""
           << level.constraint_modulus
           << "\",\"exact_trace_candidate_count\":\""
           << level.exact_trace_candidate_count
           << "\",\"trace_candidate_count\":\""
           << level.trace_candidate_count << "\",\"elapsed_us\":\""
           << level.elapsed_us << "\"}";
}

void write_fallback_levels(
    std::ostream& output,
    const std::vector<oneshotsea::SchoofFallbackLevelRecord>& levels) {
    output << '[';
    for (std::size_t index = 0; index < levels.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        write_fallback_level(output, levels[index]);
    }
    output << ']';
}

void write_atkin_constraints(
    std::ostream& output,
    const std::vector<oneshotsea::AtkinConstraint>& constraints) {
    output << '[';
    for (std::size_t index = 0; index < constraints.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        const oneshotsea::AtkinConstraint& constraint = constraints[index];
        output << "{\"ell\":" << constraint.ell
               << ",\"projective_order\":"
               << constraint.projective_order << ",\"trace_residues\":[";
        for (std::size_t residue_index = 0;
             residue_index < constraint.trace_residues.size();
             ++residue_index) {
            if (residue_index != 0U) {
                output << ',';
            }
            output << constraint.trace_residues[residue_index];
        }
        output << "]}";
    }
    output << ']';
}

void write_state(std::ostream& output, const char* status,
                 const oneshotsea::WeberSeaResult& state) {
    output << "{\"status\":\"" << status
           << "\",\"exact_modulus\":\"" << state.constraints.modulus()
           << "\",\"constraint_modulus\":\""
           << state.effective_constraints.modulus()
           << "\",\"exact_trace_candidate_count\":\""
           << state.constraints.candidate_count()
           << "\",\"trace_candidate_count\":\""
           << state.effective_constraints.candidate_count()
           << "\",\"exact_residue_classes\":";
    write_mpz_vector(output, state.constraints.residues());
    output << ",\"effective_residue_classes\":";
    write_mpz_vector(output, state.effective_constraints.residues());
    output << ",\"compatible_source_lifts\":";
    write_mpz_vector(output, state.compatible_source_lifts);
    output << ",\"atkin_constraints\":";
    write_atkin_constraints(output, state.atkin_constraints);
    output << ",\"levels\":";
    write_levels(output, state.levels);
    output << ",\"fallback_levels\":";
    write_fallback_levels(output, state.schoof_fallback_levels);
    output << ",\"trace_count\":";
    if (state.traces.has_value()) {
        output << '"' << state.traces->size() << '"';
    } else {
        output << "null";
    }
    output << ",\"traces\":";
    write_optional_traces(output, state.traces);
    output << '}';
}

const char* trace_state_status(const oneshotsea::WeberSeaResult& state) {
    if (state.compatible_source_lifts.empty() && state.levels.empty()) {
        return "no_rational_weber_lift";
    }
    if (!state.traces.has_value()) {
        return "level_limit";
    }
    if (state.traces->empty()) {
        throw std::logic_error("SEA enumerated an empty complete trace set");
    }
    return "trace_set_enumerated";
}

std::optional<std::vector<mpz_class>> exact_singleton_traces(
    const oneshotsea::WeberSeaResult& state) {
    std::optional<std::vector<mpz_class>> traces =
        state.constraints.enumerate(1U);
    if (!traces.has_value() || traces->size() != 1U) {
        return std::nullopt;
    }
    return traces;
}

std::optional<std::vector<mpz_class>> certified_singleton_traces(
    const oneshotsea::WeberSeaResult& state) {
    std::optional<std::vector<mpz_class>> traces =
        state.effective_constraints.enumerate(1U);
    if (!traces.has_value() || traces->size() != 1U) {
        return std::nullopt;
    }
    return traces;
}

oneshotsea::WeberSeaResult run_sea(
    const AuditOptions& options, const oneshotsea::WeberCurvePair& pair,
    const std::optional<oneshotsea::ExactTracePrior>& trace_prior,
    std::size_t trace_cap) {
    return oneshotsea::run_weber_sea_reference(
        pair.curve, options.table_directory.string(), options.max_level,
        trace_cap, {}, options.sea_threads, true, true, {}, trace_prior,
        pair.weber_f);
}

void write_record(
    std::ostream& output, const AuditOptions& options, std::uint64_t index,
    const oneshotsea::WeberCurvePair& pair,
    const std::optional<oneshotsea::ExactTracePrior>& trace_prior,
    const oneshotsea::WeberSeaResult& early,
    const oneshotsea::WeberSeaResult& final_state, const char* unique_mode) {
    const std::optional<std::vector<mpz_class>> certified_traces =
        certified_singleton_traces(final_state);
    const std::optional<std::vector<mpz_class>> exact_traces =
        exact_singleton_traces(final_state);
    const bool complete = certified_traces.has_value() &&
                          final_state.traces.has_value() &&
                          final_state.traces->size() == 1U &&
                          final_state.traces->front() ==
                              certified_traces->front();
    const bool final_exact_only =
        complete && exact_traces.has_value() &&
        exact_traces->front() == certified_traces->front();
    if (final_state.traces.has_value() && final_state.traces->empty()) {
        throw std::logic_error("unique SEA state contains an empty trace set");
    }

    output << "{\"schema\":\"oneshotsea.weber-audit.v1\",\"p\":\""
           << options.prime << "\",\"seed\":\"" << options.seed
           << "\",\"index\":\"" << index
           << "\",\"max_level\":\"" << options.max_level
           << "\",\"trace_cap\":\"" << options.trace_cap
           << "\",\"sea_threads\":\"" << options.sea_threads
           << "\",\"schoof_fallback\":"
           << (options.schoof_fallback ? "true" : "false")
           << ",\"smoothness_audited\":false"
           << ",\"rejected_samples\":\"" << pair.rejected_samples
           << "\",\"weber_f\":\"" << pair.weber_f << "\",\"j\":\""
           << pair.j_invariant << "\",\"twist_parameter\":\""
           << pair.twist_parameter << "\",\"curve\":{\"a\":\""
           << pair.curve.a() << "\",\"b\":\"" << pair.curve.b()
           << "\"},\"twist\":{\"a\":\"" << pair.twist.a()
           << "\",\"b\":\"" << pair.twist.b()
           << "\"},\"trace_prior\":";
    if (trace_prior.has_value()) {
        output << "{\"modulus\":\"" << trace_prior->modulus()
               << "\",\"residue\":\"" << trace_prior->residue()
               << "\"}";
    } else {
        output << "null";
    }
    output << ",\"early\":";
    write_state(output, trace_state_status(early), early);
    output << ",\"unique_mode\":\"" << unique_mode
           << "\",\"final\":";
    write_state(output, complete ? "complete" : trace_state_status(final_state),
                final_state);
    output << ",\"final_exact_trace\":";
    if (complete) {
        output << '"' << certified_traces->front() << '"';
    } else {
        output << "null";
    }
    output << ",\"final_exact_only\":"
           << (final_exact_only ? "true" : "false")
           << ",\"complete\":" << (complete ? "true" : "false")
           << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const AuditOptions options = validated_options(argc, argv);
        oneshotsea::authenticate_trusted_weber_table_set(
            options.table_directory);

        for (std::uint64_t offset = 0; offset < options.count; ++offset) {
            const std::uint64_t index = options.range_start + offset;
            const oneshotsea::WeberCurvePair pair =
                oneshotsea::deterministic_weber_curve_pair(
                    options.prime, options.seed, index);
            const std::optional<oneshotsea::ExactTracePrior> trace_prior =
                oneshotsea::exact_trace_prior_from_full_rational_two_torsion(
                    pair.curve);

            oneshotsea::WeberSeaResult early =
                run_sea(options, pair, trace_prior, options.trace_cap);
            if (!early.traces.has_value() && options.schoof_fallback) {
                oneshotsea::extend_weber_sea_with_schoof_fallback(
                    pair.curve, early, options.trace_cap);
            }
            static_cast<void>(trace_state_status(early));

            oneshotsea::WeberSeaResult final_state = early;
            const char* unique_mode = "already_exact_singleton";
            const std::optional<std::vector<mpz_class>> early_exact_traces =
                exact_singleton_traces(early);
            const std::optional<std::vector<mpz_class>>
                early_certified_traces = certified_singleton_traces(early);
            if (early_exact_traces.has_value()) {
                final_state.traces = early_exact_traces;
            } else if (early_certified_traces.has_value()) {
                unique_mode = "already_certified_singleton";
                final_state.traces = early_certified_traces;
            } else {
                if (options.schoof_fallback) {
                    unique_mode = "retained_schoof_fallback";
                    oneshotsea::extend_weber_sea_with_schoof_fallback(
                        pair.curve, final_state, 1U);
                } else {
                    unique_mode = "fresh_cap_one";
                    final_state = run_sea(options, pair, trace_prior, 1U);
                }
            }
            const std::optional<std::vector<mpz_class>>
                final_certified_traces =
                    certified_singleton_traces(final_state);
            if (final_certified_traces.has_value()) {
                final_state.traces = final_certified_traces;
            } else {
                final_state.traces.reset();
            }

            write_record(std::cout, options, index, pair, trace_prior, early,
                         final_state, unique_mode);
            std::cout.flush();
            if (!std::cout) {
                throw std::runtime_error("failed to write audit record");
            }
        }
        return 0;
    } catch (const std::exception& error) {
        usage();
        std::cerr << "oracle Weber audit: " << error.what() << '\n';
        return 2;
    }
}
