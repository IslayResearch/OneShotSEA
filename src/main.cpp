#include "oneshotsea/curve.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/search_pipeline.hpp"
#include "oneshotsea/schoof.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/x1_11_probe.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::map<std::string, std::string> parse_options(int argc, char** argv, int begin) {
    std::map<std::string, std::string> options;
    for (int i = begin; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument.rfind("--", 0) != 0 || i + 1 >= argc) {
            throw std::invalid_argument("expected --name value option, got: " + argument);
        }
        options[argument.substr(2)] = argv[++i];
    }
    return options;
}

const std::string& required(const std::map<std::string, std::string>& options,
                            const std::string& name) {
    const auto found = options.find(name);
    if (found == options.end()) {
        throw std::invalid_argument("missing --" + name);
    }
    return found->second;
}

std::uint64_t required_u64(const std::map<std::string, std::string>& options,
                           const std::string& name) {
    const std::string& text = required(options, name);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("--" + name + " must be an unsigned 64-bit integer");
    }
    return value;
}

unsigned required_unsigned(const std::map<std::string, std::string>& options,
                           const std::string& name) {
    const std::uint64_t value = required_u64(options, name);
    if (value > std::numeric_limits<unsigned>::max()) {
        throw std::invalid_argument("--" + name + " is out of range");
    }
    return static_cast<unsigned>(value);
}

std::uint64_t optional_u64(const std::map<std::string, std::string>& options,
                           const std::string& name, std::uint64_t fallback) {
    if (!options.contains(name)) {
        return fallback;
    }
    return required_u64(options, name);
}

std::uint64_t parse_profile_u64(const std::string& text,
                                const std::filesystem::path& path,
                                std::size_t line_number) {
    std::uint64_t value = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument(
            "invalid unsigned integer in SEA level profile " + path.string() +
            ":" + std::to_string(line_number));
    }
    return value;
}

std::vector<oneshotsea::WeberSeaLevelEstimate> load_level_profile(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::invalid_argument("cannot open SEA level profile: " +
                                    path.string());
    }
    std::vector<oneshotsea::WeberSeaLevelEstimate> estimates;
    std::string line;
    for (std::size_t line_number = 1U; std::getline(input, line);
         ++line_number) {
        std::istringstream parser(line);
        std::string ell;
        if (!(parser >> ell) || ell.starts_with('#')) {
            continue;
        }
        std::string information;
        std::string cost;
        std::string trailing;
        if (!(parser >> information >> cost) || parser >> trailing) {
            throw std::invalid_argument(
                "SEA level profile rows must be: ell information_units cost_us at " +
                path.string() + ":" + std::to_string(line_number));
        }
        estimates.push_back({
            parse_profile_u64(ell, path, line_number),
            parse_profile_u64(information, path, line_number),
            parse_profile_u64(cost, path, line_number),
        });
    }
    if (!input.eof()) {
        throw std::invalid_argument("cannot read SEA level profile: " +
                                    path.string());
    }
    if (estimates.empty()) {
        throw std::invalid_argument("SEA level profile is empty: " +
                                    path.string());
    }
    return estimates;
}

std::string optional_string(const std::map<std::string, std::string>& options,
                            const std::string& name, std::string fallback) {
    const auto found = options.find(name);
    return found == options.end() ? std::move(fallback) : found->second;
}

std::vector<std::uint64_t> optional_u64_list(
    const std::map<std::string, std::string>& options,
    const std::string& name) {
    const auto found = options.find(name);
    if (found == options.end()) {
        return {};
    }
    const std::string& text = found->second;
    if (text.empty()) {
        throw std::invalid_argument("--" + name +
                                    " must be a comma-separated integer list");
    }
    std::vector<std::uint64_t> values;
    std::size_t begin = 0U;
    while (begin < text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end =
            comma == std::string::npos ? text.size() : comma;
        const std::string_view item(text.data() + begin, end - begin);
        std::uint64_t value = 0U;
        const auto parsed = std::from_chars(
            item.data(), item.data() + item.size(), value, 10);
        if (item.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != item.data() + item.size()) {
            throw std::invalid_argument("--" + name +
                                        " must be a comma-separated integer list");
        }
        values.push_back(value);
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1U;
        if (begin == text.size()) {
            throw std::invalid_argument("--" + name +
                                        " must not end with a comma");
        }
    }
    return values;
}

void ensure_parent_directory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void require_distinct_output_paths(
    const std::vector<std::filesystem::path>& paths) {
    for (std::size_t left = 0; left < paths.size(); ++left) {
        for (std::size_t right = 0; right < left; ++right) {
            if (oneshotsea::paths_alias(paths[left], paths[right])) {
                throw std::invalid_argument(
                    "smooth cache, checkpoint, progress, certificate, and metadata paths must be distinct");
            }
        }
    }
}

void write_x1_11_counters(
    std::ostream& output,
    const oneshotsea::X111ProbeRejections& counters) {
    output << "{\"x_samples\":\"" << counters.x_samples
           << "\",\"x_polynomials_without_roots\":\""
           << counters.x_polynomials_without_roots
           << "\",\"x1_points\":\"" << counters.x1_points
           << "\",\"singular_curves\":\"" << counters.singular_curves
           << "\",\"exceptional_j\":\"" << counters.exceptional_j
           << "\",\"exact_order_11_failures\":\""
           << counters.exact_order_11_failures
           << "\",\"points_without_weber_lifts\":\""
           << counters.points_without_weber_lifts
           << "\",\"weber_lifts\":\"" << counters.weber_lifts
           << "\",\"nonsquare_explicit_montgomery_u\":\""
           << counters.nonsquare_explicit_montgomery_u
           << "\",\"points_without_explicit_montgomery_model\":\""
           << counters.points_without_explicit_montgomery_model
           << "\",\"full_two_torsion_failures\":\""
           << counters.full_two_torsion_failures
           << "\",\"point_four_rejections\":\""
           << counters.point_four_rejections << "\",\"accepted\":\""
           << counters.accepted << "\"}";
}

void usage() {
    std::cerr
        << "usage:\n"
        << "  oneshotsea curve --p P --seed S --index I\n"
        << "  oneshotsea montgomery-curve --p P --seed S --index I\n"
        << "  oneshotsea x1-11-probe --p P --seed S --range-start I [--count N] [--max-x-samples N] [--require-point4 0|1]\n"
        << "  oneshotsea point-count --p P --a A --b B\n"
        << "  oneshotsea schoof-residue --p P --a A --b B --ell L\n"
        << "  oneshotsea schoof-count --p P --a A --b B --max-ell L\n"
        << "  oneshotsea elkies-residue --p P --a A --b B --ell L --file PATH\n"
        << "  oneshotsea elkies-bmss-residue --p P --a A --b B --ell L --file PATH\n"
        << "  oneshotsea elkies-weber-residue --p P --a A --b B --ell L --file PATH\n"
        << "  oneshotsea elkies-division-residue --p P --a A --b B --ell L\n"
        << "  oneshotsea sea-weber-count --p P --a A --b B --max-level L --table-dir PATH --trace-cap N [--sea-threads N] [--root-orbit-reuse 0|1] [--conjugate-eigenvalue-reuse 0|1] [--prime-schedule increasing|expected-information-per-cost --level-profile PATH]\n"
        << "  oneshotsea search --p P --seed S --range-start I --range-end J --worker-id W --worker-count N --max-level L --table-dir PATH --smooth-cache PATH --checkpoint PATH [--curve-family weber-f|x1-11|x1-27] [--x1-require-point4 0|1] [--classical-direct-levels L1,L2,...] [--classical-direct-max-prime-candidates N] [--classical-direct-max-x-candidates N] [--schoof-fallback 0|1] [--skip-incomplete-curves 0|1] [--curve-threads N] [--smooth-coordinators N] [--sea-threads N] [--sea-level-telemetry 0|1] [--max-curves N]\n"
        << "  oneshotsea modpoly --p P --a A --b B --level L --file PATH\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 2;
        }
        const std::string command = argv[1];
        const auto options = parse_options(argc, argv, 2);
        if (command == "x1-11-probe") {
            const mpz_class p =
                oneshotsea::parse_integer(required(options, "p"));
            const std::uint64_t seed = required_u64(options, "seed");
            const std::uint64_t range_start =
                required_u64(options, "range-start");
            const std::uint64_t count = optional_u64(options, "count", 1U);
            const std::uint64_t max_x_samples =
                optional_u64(options, "max-x-samples", 64U);
            const std::uint64_t require_point_four_value =
                optional_u64(options, "require-point4", 0U);
            if (count == 0U || max_x_samples == 0U ||
                require_point_four_value > 1U ||
                count > std::numeric_limits<std::uint64_t>::max() -
                            range_start) {
                throw std::invalid_argument(
                    "invalid bounded X1(11) probe range or option");
            }
            const bool require_point_four = require_point_four_value == 1U;
            oneshotsea::X111ProbeRejections aggregate;
            const auto start = std::chrono::steady_clock::now();
            for (std::uint64_t offset = 0; offset < count; ++offset) {
                const std::uint64_t index = range_start + offset;
                oneshotsea::X111ProbeResult result =
                    oneshotsea::deterministic_x1_11_probe(
                        p, seed, index,
                        {max_x_samples, require_point_four});
                aggregate += result.counters;
                std::cout << "{\"schema\":\"oneshotsea.x1-11-probe-index.v1\""
                          << ",\"index\":\"" << index
                          << "\",\"accepted\":"
                          << (result.sample.has_value() ? "true" : "false")
                          << ",\"counters\":";
                write_x1_11_counters(std::cout, result.counters);
                if (result.sample.has_value()) {
                    const oneshotsea::X111ProbeSample& sample = *result.sample;
                    std::cout << ",\"sample\":{\"x1_x\":\""
                              << sample.x1_x << "\",\"x1_y\":\""
                              << sample.x1_y << "\",\"tate_r\":\""
                              << sample.tate_r << "\",\"tate_s\":\""
                              << sample.tate_s << "\",\"tate_b\":\""
                              << sample.tate_b << "\",\"tate_c\":\""
                              << sample.tate_c << "\",\"tate_short_a\":\""
                              << sample.tate_curve.a()
                              << "\",\"tate_short_b\":\""
                              << sample.tate_curve.b()
                              << "\",\"tate_point_x\":\""
                              << sample.tate_point_x
                              << "\",\"tate_point_y\":\""
                              << sample.tate_point_y << "\",\"j\":\""
                              << sample.pair.j_invariant
                              << "\",\"weber_f\":\""
                              << sample.pair.weber_f
                              << "\",\"explicit_montgomery_a\":\""
                              << sample.explicit_montgomery_coefficient
                              << "\",\"selected_side\":\""
                              << oneshotsea::x1_11_canonical_side_name(
                                     sample.selected_side)
                              << "\",\"full_rational_two_torsion\":"
                              << (sample.has_full_rational_two_torsion
                                      ? "true"
                                      : "false")
                              << ",\"point_order_four\":"
                              << (sample.has_point_order_four ? "true"
                                                              : "false")
                              << ",\"cyclic_divisor\":\""
                              << sample.cyclic_divisor
                              << "\",\"group_divisor\":\""
                              << sample.group_divisor
                              << "\",\"opposite_order_residue\":\""
                              << sample.opposite_order_residue << "\"}";
                }
                std::cout << "}\n" << std::flush;
            }
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start);
            std::cout << "{\"schema\":\"" << oneshotsea::kX111ProbeSchema
                      << "\",\"generator_version\":\""
                      << oneshotsea::kX111ProbeGeneratorVersion
                      << "\",\"formula_source_url\":\""
                      << oneshotsea::kX111FormulaSourceUrl
                      << "\",\"formula_source_sha256\":\""
                      << oneshotsea::kX111FormulaSourceSha256
                      << "\",\"prime\":\"" << p << "\",\"seed\":\""
                      << seed << "\",\"range_start\":\"" << range_start
                      << "\",\"count\":\"" << count
                      << "\",\"max_x_samples_per_index\":\""
                      << max_x_samples << "\",\"require_point_four\":"
                      << (require_point_four ? "true" : "false")
                      << ",\"elapsed_us\":\"" << elapsed.count()
                      << "\",\"counters\":";
            write_x1_11_counters(std::cout, aggregate);
            std::cout << "}\n";
            return 0;
        }
        if (command == "search") {
            oneshotsea::SearchPipelineConfig config;
            config.prime = oneshotsea::parse_integer(required(options, "p"));
            config.seed = required_u64(options, "seed");
            const std::string curve_family = optional_string(
                options, "curve-family", "weber-f");
            if (curve_family == "weber-f") {
                config.curve_family = oneshotsea::SearchCurveFamily::weber_f;
            } else if (curve_family == "x1-11") {
                config.curve_family = oneshotsea::SearchCurveFamily::x1_11;
            } else if (curve_family == "x1-27") {
                config.curve_family = oneshotsea::SearchCurveFamily::x1_27;
            } else {
                throw std::invalid_argument(
                    "--curve-family must be weber-f, x1-11, or x1-27");
            }
            const std::uint64_t x1_require_point_four = optional_u64(
                options, "x1-require-point4", 0U);
            if (x1_require_point_four > 1U) {
                throw std::invalid_argument(
                    "--x1-require-point4 must be zero or one");
            }
            config.x1_require_point_four = x1_require_point_four != 0U;
            if (config.curve_family ==
                    oneshotsea::SearchCurveFamily::weber_f &&
                config.x1_require_point_four) {
                throw std::invalid_argument(
                    "--x1-require-point4 requires an X1 curve family");
            }
            config.table_directory = required(options, "table-dir");
            config.max_level = required_u64(options, "max-level");
            const std::uint64_t trace_cap = optional_u64(
                options, "trace-cap", config.early_trace_cap);
            const std::uint64_t sea_threads = optional_u64(
                options, "sea-threads", 0U);
            const std::uint64_t curve_threads = optional_u64(
                options, "curve-threads", 1U);
            const std::uint64_t smooth_coordinators = optional_u64(
                options, "smooth-coordinators", 0U);
            if (smooth_coordinators >
                std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument(
                    "--smooth-coordinators is out of range");
            }
            const std::uint64_t sea_level_telemetry_value = optional_u64(
                options, "sea-level-telemetry", 1U);
            const std::uint64_t skip_incomplete_curves = optional_u64(
                options, "skip-incomplete-curves", 0U);
            const std::uint64_t schoof_fallback = optional_u64(
                options, "schoof-fallback", 0U);
            config.classical_direct_levels = optional_u64_list(
                options, "classical-direct-levels");
            config.classical_direct_maximum_prime_candidates = optional_u64(
                options, "classical-direct-max-prime-candidates",
                config.classical_direct_maximum_prime_candidates);
            config.classical_direct_maximum_x_candidates_per_surface =
                optional_u64(
                    options, "classical-direct-max-x-candidates",
                    config.classical_direct_maximum_x_candidates_per_surface);
            if (config.classical_direct_levels.empty() &&
                (options.contains(
                     "classical-direct-max-prime-candidates") ||
                 options.contains("classical-direct-max-x-candidates"))) {
                throw std::invalid_argument(
                    "classical direct execution caps require --classical-direct-levels");
            }
            const std::uint64_t assembly_attempts = optional_u64(
                options, "assembly-attempts", 400U);
            const std::uint64_t max_certificate_candidates = optional_u64(
                options, "max-certificate-candidates", 100000U);
            const std::uint64_t max_candidate_search_nodes = optional_u64(
                options, "max-candidate-search-nodes", 1000000U);
            if (trace_cap > std::numeric_limits<std::size_t>::max() ||
                curve_threads == 0U || sea_level_telemetry_value > 1U ||
                skip_incomplete_curves > 1U || schoof_fallback > 1U ||
                curve_threads > std::numeric_limits<std::size_t>::max() ||
                smooth_coordinators > curve_threads ||
                sea_threads > std::numeric_limits<std::size_t>::max() ||
                assembly_attempts > std::numeric_limits<std::size_t>::max() ||
                max_certificate_candidates == 0U ||
                max_certificate_candidates >
                    std::numeric_limits<std::size_t>::max() ||
                max_candidate_search_nodes == 0U ||
                max_candidate_search_nodes >
                    std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("search size option is out of range");
            }
            config.early_trace_cap = static_cast<std::size_t>(trace_cap);
            config.enable_schoof_fallback = schoof_fallback != 0U;
            config.skip_incomplete_curves = skip_incomplete_curves != 0U;
            config.sea_threads = static_cast<std::size_t>(sea_threads);
            config.assembly_attempts =
                static_cast<std::size_t>(assembly_attempts);
            config.max_certificate_candidates =
                static_cast<std::size_t>(max_certificate_candidates);
            config.max_candidate_search_nodes =
                static_cast<std::size_t>(max_candidate_search_nodes);
            config.certificate_seed = optional_u64(
                options, "certificate-seed", 1U);
            if (options.contains("verifier")) {
                throw std::invalid_argument(
                    "--verifier overrides are forbidden in production search");
            }
            const std::filesystem::path executable_path =
                std::filesystem::canonical(std::filesystem::absolute(argv[0]));
            const std::filesystem::path repository_root =
                executable_path.parent_path().parent_path();
            config.canonical_verifier = std::filesystem::canonical(
                repository_root / "third_party" /
                "oneshot_primality_proofs" / "voneshot.py");
            config.python_executable = oneshotsea::resolve_executable_path(
                optional_string(options, "python", "python3"));
            if (!oneshotsea::authenticate_python3_interpreter(
                    config.python_executable)) {
                throw std::invalid_argument(
                    "--python did not authenticate as Python 3");
            }
            const std::string python_sha =
                oneshotsea::sha256_file(config.python_executable);

            const std::filesystem::path smooth_cache =
                required(options, "smooth-cache");
            const std::filesystem::path checkpoint =
                required(options, "checkpoint");
            const std::filesystem::path progress = optional_string(
                options, "progress", checkpoint.string() + ".ndjson");
            const std::filesystem::path certificate_output = optional_string(
                options, "certificate-out", checkpoint.string() + ".certificate");
            const std::filesystem::path certificate_metadata =
                certificate_output.string() + ".meta.json";
            require_distinct_output_paths({
                smooth_cache, checkpoint, progress, certificate_output,
                certificate_metadata});
            const bool cache_preexisted =
                std::filesystem::is_regular_file(smooth_cache);
            std::optional<std::string> trusted_existing_cache_sha;
            if (cache_preexisted) {
                const std::string supplied =
                    required(options, "smooth-cache-sha256");
                if (!oneshotsea::is_lower_sha256(supplied)) {
                    throw std::invalid_argument(
                        "--smooth-cache-sha256 must be a trusted lowercase SHA-256 digest");
                }
                trusted_existing_cache_sha = supplied;
            }
            ensure_parent_directory(smooth_cache);
            ensure_parent_directory(checkpoint);
            ensure_parent_directory(progress);

            oneshotsea::ExactSmoothOptions smooth_options;
            const std::uint64_t smooth_threads = optional_u64(
                options, "smooth-threads", 0U);
            if (smooth_threads >
                static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                throw std::invalid_argument("--smooth-threads is out of range");
            }
            smooth_options.thread_count = static_cast<int>(smooth_threads);
            const std::uint64_t smooth_batch = optional_u64(
                options, "smooth-max-batch", 128U);
            if (smooth_batch == 0U ||
                smooth_batch > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("--smooth-max-batch is out of range");
            }
            smooth_options.max_orders_per_batch =
                static_cast<std::size_t>(smooth_batch);
            const std::uint64_t smooth_root_auxiliary_bytes = optional_u64(
                options, "smooth-root-auxiliary-bytes",
                smooth_options.max_root_auxiliary_bytes);
            if (smooth_root_auxiliary_bytes == 0U ||
                smooth_root_auxiliary_bytes >
                    std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument(
                    "--smooth-root-auxiliary-bytes is out of range");
            }
            smooth_options.max_root_auxiliary_bytes =
                static_cast<std::size_t>(smooth_root_auxiliary_bytes);
            const std::uint64_t smooth_build_segment_span = optional_u64(
                options, "smooth-build-segment-span",
                smooth_options.build_segment_span);
            if (smooth_build_segment_span == 0U) {
                throw std::invalid_argument(
                    "--smooth-build-segment-span must be positive");
            }
            smooth_options.build_segment_span = smooth_build_segment_span;
            const oneshotsea::ExactSmoothEngine smooth_engine =
                oneshotsea::ExactSmoothEngine::load_or_build(
                    config.prime, smooth_cache, trusted_existing_cache_sha,
                    smooth_options);
            const std::string cache_sha =
                trusted_existing_cache_sha.has_value()
                    ? *trusted_existing_cache_sha
                    : oneshotsea::sha256_file(smooth_cache);
            const std::string verifier_sha =
                oneshotsea::sha256_file(config.canonical_verifier);
            constexpr const char* pinned_verifier_sha =
                "e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666";
            if (verifier_sha != pinned_verifier_sha) {
                throw std::runtime_error(
                    "pinned canonical voneshot.py digest mismatch");
            }
            std::string build_id = optional_string(options, "build-id", "");
            if (build_id.empty()) {
                const std::string executable_sha =
                    oneshotsea::sha256_file(std::filesystem::absolute(argv[0]));
                build_id = "binary-sha256:" + executable_sha.substr(0, 32U);
            }
            const oneshotsea::SearchRange global_range{
                required_u64(options, "range-start"),
                required_u64(options, "range-end")};
            const std::uint64_t worker_id = required_u64(options, "worker-id");
            const std::uint64_t worker_count =
                required_u64(options, "worker-count");
            const oneshotsea::SearchIdentity identity =
                oneshotsea::make_search_identity(
                    config, global_range, worker_id, worker_count, cache_sha,
                    verifier_sha, build_id);
            config.expected_schedule_sha256 = identity.schedule_sha256;
            config.expected_smooth_cache_sha256 = cache_sha;
            config.expected_table_manifest_sha256 =
                identity.table_manifest_sha256;
            config.expected_verifier_sha256 = verifier_sha;
            config.expected_python_sha256 = python_sha;
            oneshotsea::SearchState state =
                std::filesystem::exists(checkpoint)
                    ? oneshotsea::load_search_checkpoint(checkpoint, identity)
                    : oneshotsea::SearchState(identity);

            const std::uint64_t remaining =
                identity.range.end - state.next_index();
            oneshotsea::SearchPipelineRunOptions run_options;
            run_options.max_curves = optional_u64(
                options, "max-curves", remaining);
            run_options.curve_threads =
                static_cast<std::size_t>(curve_threads);
            run_options.smooth_coordinator_count =
                static_cast<std::size_t>(smooth_coordinators);
            run_options.checkpoint_every = optional_u64(
                options, "checkpoint-every", 1U);
            run_options.checkpoint_path = checkpoint;
            run_options.progress_path = progress;
            run_options.certificate_path = certificate_output;
            const bool sea_level_telemetry = sea_level_telemetry_value != 0U;
            run_options.include_sea_level_timings = sea_level_telemetry;
            if (sea_level_telemetry) {
                run_options.sea_level_callback =
                    [](std::uint64_t index,
                       const oneshotsea::SearchSeaLevelTiming& level) {
                        std::cout
                            << oneshotsea::search_sea_level_json(index, level)
                            << '\n' << std::flush;
                    };
                run_options.classical_direct_level_callback =
                    [](std::uint64_t index,
                       const oneshotsea::SearchClassicalDirectLevelTiming&
                           level) {
                        std::cout
                            << oneshotsea::search_classical_direct_level_json(
                                   index, level)
                            << '\n' << std::flush;
                    };
            }
            std::cout << "{\"schema\":\"oneshotsea.search-start.v1\""
                      << ",\"prime\":\"" << config.prime
                      << "\",\"seed\":\"" << config.seed
                      << "\",\"curve_family\":\""
                      << oneshotsea::search_curve_family_name(
                             config.curve_family)
                      << "\",\"worker_id\":\"" << worker_id
                      << "\",\"worker_count\":\"" << worker_count
                      << "\",\"range_start\":\"" << identity.range.first
                      << "\",\"range_end\":\"" << identity.range.end
                      << "\",\"next_index\":\"" << state.next_index()
                      << "\",\"schedule_sha256\":\""
                      << identity.schedule_sha256
                      << "\",\"table_manifest_sha256\":\""
                      << identity.table_manifest_sha256
                      << "\",\"smooth_cache_sha256\":\"" << cache_sha
                      << "\",\"verifier_sha256\":\"" << verifier_sha
                      << "\",\"python_executable\":\""
                      << config.python_executable
                      << "\",\"python_sha256\":\"" << python_sha
                      << "\",\"build_id\":\"" << build_id
                      << "\",\"heuristic_rejection\":"
                      << (config.skip_incomplete_curves ? "true" : "false");
            if (!config.classical_direct_levels.empty()) {
                std::cout << ",\"classical_direct\":{\"policy\":\""
                          << oneshotsea::kClassicalDirectSeaPolicy
                          << "\",\"levels\":[";
                for (std::size_t index = 0U;
                     index < config.classical_direct_levels.size(); ++index) {
                    if (index != 0U) {
                        std::cout << ',';
                    }
                    std::cout << '"' << config.classical_direct_levels[index]
                              << '"';
                }
                std::cout << "],\"maximum_prime_candidates\":\""
                          << config.classical_direct_maximum_prime_candidates
                          << "\",\"maximum_x_candidates_per_surface\":\""
                          << config
                                 .classical_direct_maximum_x_candidates_per_surface
                          << "\"}";
            }
            std::cout << ",\"resources\":{\"smooth_threads\":\""
                      << smooth_threads << "\",\"smooth_max_batch\":\""
                      << smooth_batch
                      << "\",\"smooth_root_auxiliary_bytes\":\""
                      << smooth_root_auxiliary_bytes
                      << "\",\"smooth_build_segment_span\":\""
                      << smooth_build_segment_span
                      << "\",\"curve_threads\":\""
                      << run_options.curve_threads
                      << "\",\"smooth_coordinators\":\""
                      << run_options.smooth_coordinator_count
                      << "\",\"x1_require_point_four\":"
                      << (config.x1_require_point_four ? "true" : "false")
                      << ",\"skip_incomplete_curves\":"
                      << (config.skip_incomplete_curves ? "true" : "false")
                      << ",\"schoof_fallback\":"
                      << (config.enable_schoof_fallback ? "true" : "false")
                      << ",\"sea_level_telemetry\":"
                      << (sea_level_telemetry ? "true" : "false")
                      << ",\"sea_threads\":\"" << config.sea_threads
                      << "\",\"assembly_attempts\":\""
                      << config.assembly_attempts << "\",\"trace_cap\":\""
                      << config.early_trace_cap
                      << "\",\"max_certificate_candidates\":\""
                      << config.max_certificate_candidates
                      << "\",\"max_candidate_search_nodes\":\""
                      << config.max_candidate_search_nodes << "\"}}\n"
                      << std::flush;
            const auto callback = [sea_level_telemetry](
                                      const oneshotsea::SearchCurveReport& report,
                                      const oneshotsea::SearchState& current) {
                std::cout << oneshotsea::search_curve_report_json(
                                 report, current, sea_level_telemetry)
                          << '\n' << std::flush;
            };
            const oneshotsea::SearchPipelineRunResult result =
                oneshotsea::run_search_pipeline(
                    config, smooth_engine, state, run_options, callback);
            std::cout << "{\"schema\":\"oneshotsea.search-summary.v1\""
                      << ",\"processed\":\"" << result.curves_processed
                      << "\",\"range_exhausted\":"
                      << (result.exhausted_assigned_range ? "true" : "false")
                      << ",\"verified\":"
                      << (result.verified.has_value() ? "true" : "false");
            if (!config.classical_direct_levels.empty()) {
                std::cout << ",\"classical_direct_preparation\":{\"context_count\":\""
                          << result.classical_direct_context_count
                          << "\",\"elapsed_us\":\""
                          << result.classical_direct_preparation_us
                          << "\",\"thread_limit\":\""
                          << config.sea_threads
                          << "\",\"matrix_coefficients\":\""
                          << result
                                 .classical_direct_interpolation_coefficient_count
                          << "\",\"matrix_payload_bytes\":\""
                          << result
                                 .classical_direct_interpolation_storage_bytes
                          << "\"}";
            }
            std::cout
                      << ",\"smooth_batch\":{\"enabled\":"
                      << (result.smooth_batch_coordinator_enabled ? "true"
                                                                  : "false")
                      << ",\"coordinator_count\":\""
                      << result.smooth_batch_coordinator_count
                      << "\",\"submitted_requests\":\""
                      << result.smooth_batch_telemetry.submitted_requests
                      << "\",\"completed_requests\":\""
                      << result.smooth_batch_telemetry.completed_requests
                      << "\",\"failed_requests\":\""
                      << result.smooth_batch_telemetry.failed_requests
                      << "\",\"cancelled_requests\":\""
                      << result.smooth_batch_telemetry.cancelled_requests
                      << "\",\"coordinator_batches\":\""
                      << result.smooth_batch_telemetry.coordinator_batches
                      << "\",\"successful_cache_scan_chunks\":\""
                      << result.smooth_batch_telemetry
                             .successful_cache_scan_chunks
                      << "\",\"submitted_orders\":\""
                      << result.smooth_batch_telemetry.submitted_orders
                      << "\",\"max_queued_requests_in_any_cohort\":\""
                      << result.smooth_batch_telemetry
                             .max_queued_requests_in_any_cohort
                      << "\",\"max_requests_per_batch_in_any_cohort\":\""
                      << result.smooth_batch_telemetry
                             .max_requests_per_batch_in_any_cohort
                      << "\",\"max_orders_per_successful_scan_chunk_in_any_cohort\":\""
                      << result.smooth_batch_telemetry
                             .max_orders_per_successful_scan_chunk_in_any_cohort
                      << "\",\"successful_scan_chunk_size_histogram\":[";
            for (std::size_t index = 0U;
                 index < result.smooth_batch_telemetry
                             .successful_scan_chunks_by_order_count.size();
                 ++index) {
                if (index != 0U) {
                    std::cout << ',';
                }
                const auto& entry = result.smooth_batch_telemetry
                                        .successful_scan_chunks_by_order_count
                                            [index];
                std::cout << "{\"orders\":\"" << entry.order_count
                          << "\",\"scan_chunks\":\"" << entry.scan_chunks
                          << "\"}";
            }
            std::cout << "],\"cohorts\":[";
            for (std::size_t index = 0U;
                 index < result.smooth_batch_cohort_telemetry.size();
                 ++index) {
                if (index != 0U) {
                    std::cout << ',';
                }
                const auto& cohort =
                    result.smooth_batch_cohort_telemetry[index];
                std::cout << "{\"index\":\"" << index
                          << "\",\"submitted_requests\":\""
                          << cohort.submitted_requests
                          << "\",\"completed_requests\":\""
                          << cohort.completed_requests
                          << "\",\"failed_requests\":\""
                          << cohort.failed_requests
                          << "\",\"cancelled_requests\":\""
                          << cohort.cancelled_requests
                          << "\",\"coordinator_batches\":\""
                          << cohort.coordinator_batches
                          << "\",\"successful_cache_scan_chunks\":\""
                          << cohort.successful_cache_scan_chunks
                          << "\",\"submitted_orders\":\""
                          << cohort.submitted_orders
                          << "\",\"max_queued_requests\":\""
                          << cohort.max_queued_requests
                          << "\",\"max_requests_per_batch\":\""
                          << cohort.max_requests_per_batch
                          << "\",\"max_orders_per_successful_scan_chunk\":\""
                          << cohort.max_orders_per_successful_scan_chunk
                          << "\",\"successful_scan_chunk_size_histogram\":[";
                for (std::size_t bucket_index = 0U;
                     bucket_index <
                     cohort.successful_scan_chunks_by_order_count.size();
                     ++bucket_index) {
                    if (bucket_index != 0U) {
                        std::cout << ',';
                    }
                    const auto& bucket =
                        cohort.successful_scan_chunks_by_order_count
                            [bucket_index];
                    std::cout << "{\"orders\":\"" << bucket.order_count
                              << "\",\"scan_chunks\":\""
                              << bucket.scan_chunks << "\"}";
                }
                std::cout << "]}";
            }
            std::cout << "]}"
                      << ",\"state\":"
                      << oneshotsea::search_progress_json(state) << "}\n";
            return 0;
        }
        if (command == "curve" || command == "montgomery-curve") {
            const mpz_class p = oneshotsea::parse_integer(required(options, "p"));
            const std::uint64_t seed = required_u64(options, "seed");
            const std::uint64_t index = required_u64(options, "index");
            if (command == "montgomery-curve") {
                const oneshotsea::MontgomeryCurve curve =
                    oneshotsea::deterministic_montgomery_curve(p, seed, index);
                std::cout << "{\"p\":\"" << p << "\",\"seed\":" << seed
                          << ",\"index\":" << index << ",\"A\":\""
                          << curve.coefficient() << "\",\"singular\":"
                          << (curve.is_singular() ? "true" : "false");
                if (!curve.is_singular()) {
                    const oneshotsea::Curve short_curve = curve.short_weierstrass();
                    std::cout << ",\"a\":\"" << short_curve.a() << "\",\"b\":\""
                              << short_curve.b() << "\",\"j\":\""
                              << curve.j_invariant() << "\"";
                }
                std::cout << "}\n";
                return 0;
            }
            const oneshotsea::Curve curve = oneshotsea::deterministic_curve(p, seed, index);
            std::cout << "{\"p\":\"" << p << "\",\"seed\":" << seed
                      << ",\"index\":" << index << ",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"singular\":"
                      << (curve.is_singular() ? "true" : "false");
            if (!curve.is_singular()) {
                std::cout << ",\"j\":\"" << curve.j_invariant() << "\"";
            }
            std::cout << "}\n";
            return 0;
        }
        const mpz_class p = oneshotsea::parse_integer(required(options, "p"));
        oneshotsea::Field field(p);
        oneshotsea::Curve curve(field,
                                oneshotsea::parse_integer(required(options, "a")),
                                oneshotsea::parse_integer(required(options, "b")));
        if (curve.is_singular()) {
            throw std::invalid_argument("input curve is singular");
        }
        if (command == "point-count") {
            const mpz_class count = oneshotsea::count_points_bruteforce(curve);
            const mpz_class trace = p + 1 - count;
            std::cout << "{\"p\":\"" << p << "\",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"order\":\""
                      << count << "\",\"trace\":\"" << trace << "\"}\n";
            return 0;
        }
        if (command == "schoof-residue") {
            const std::uint64_t ell = required_u64(options, "ell");
            const std::uint64_t residue = oneshotsea::schoof_trace_mod_ell(curve, ell);
            std::cout << "{\"p\":\"" << p << "\",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"ell\":" << ell
                      << ",\"trace_residue\":" << residue << "}\n";
            return 0;
        }
        if (command == "schoof-count") {
            const std::uint64_t max_ell = required_u64(options, "max-ell");
            const auto result = oneshotsea::schoof_count_reference(curve, max_ell);
            std::cout << "{\"p\":\"" << p << "\",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"order\":\""
                      << result.order << "\",\"trace\":\"" << result.trace
                      << "\",\"residue_modulus\":\"" << result.residue_modulus
                      << "\",\"levels\":[";
            for (std::size_t index = 0; index < result.levels.size(); ++index) {
                if (index != 0) {
                    std::cout << ',';
                }
                std::cout << result.levels[index];
            }
            std::cout << "]}\n";
            return 0;
        }
        if (command == "sea-weber-count") {
            const std::uint64_t max_level = required_u64(options, "max-level");
            const std::uint64_t trace_cap_u64 = required_u64(options, "trace-cap");
            const std::uint64_t sea_threads_u64 = optional_u64(
                options, "sea-threads", 0U);
            const std::uint64_t root_orbit_reuse_u64 = optional_u64(
                options, "root-orbit-reuse", 1U);
            const std::uint64_t conjugate_eigenvalue_reuse_u64 = optional_u64(
                options, "conjugate-eigenvalue-reuse", 1U);
            if (trace_cap_u64 > std::numeric_limits<std::size_t>::max() ||
                sea_threads_u64 > std::numeric_limits<std::size_t>::max() ||
                root_orbit_reuse_u64 > 1U ||
                conjugate_eigenvalue_reuse_u64 > 1U) {
                throw std::invalid_argument(
                    "SEA resource option is out of range");
            }
            const std::string prime_schedule = optional_string(
                options, "prime-schedule", "increasing");
            std::vector<oneshotsea::WeberSeaLevelEstimate> level_estimates;
            if (prime_schedule == "increasing") {
                if (options.contains("level-profile")) {
                    throw std::invalid_argument(
                        "--level-profile requires the expected-information-per-cost schedule");
                }
            } else if (prime_schedule == "expected-information-per-cost") {
                level_estimates = load_level_profile(
                    required(options, "level-profile"));
            } else {
                throw std::invalid_argument("unknown SEA prime schedule: " +
                                            prime_schedule);
            }
            const auto progress = [](const oneshotsea::WeberSeaLevelRecord& record) {
                std::cout << "{\"type\":\"level\",\"ell\":" << record.ell
                          << ",\"exact\":" << (record.exact ? "true" : "false");
                if (record.trace_residue.has_value()) {
                    std::cout << ",\"trace_residue\":" << *record.trace_residue;
                }
                std::cout << ",\"exact_modulus\":\"" << record.exact_modulus
                          << "\",\"constraint_modulus\":\""
                          << record.constraint_modulus
                          << "\",\"exact_trace_candidates\":\""
                          << record.exact_trace_candidate_count
                          << "\",\"trace_candidates\":\""
                          << record.trace_candidate_count
                          << "\",\"atkin_projective_order\":";
                if (record.atkin_projective_order.has_value()) {
                    std::cout << *record.atkin_projective_order;
                } else {
                    std::cout << "null";
                }
                std::cout << ",\"atkin_residue_count\":"
                          << record.atkin_residue_count
                          << ",\"compatible_source_lifts\":"
                          << record.compatible_source_lifts
                          << ",\"modular_root_workers\":"
                          << record.timings.modular_root_workers
                          << ",\"modular_root_orbits\":"
                          << record.timings.modular_root_orbits
                          << ",\"modular_root_reused_lifts\":"
                          << record.timings.modular_root_reused_lifts
                          << ",\"modular_root_orbit_reuse\":"
                          << (record.timings.modular_root_orbit_reuse
                                  ? "true" : "false")
                          << ",\"timings_us\":{\"source_lifts\":"
                          << record.timings.source_lifts_us
                          << ",\"modular_roots\":"
                          << record.timings.modular_roots_us
                          << ",\"normalized_codomain\":"
                          << record.timings.normalized_codomain_us
                          << ",\"bmss\":" << record.timings.bmss_us
                          << ",\"eigenvalue\":"
                          << record.timings.eigenvalue_us
                          << ",\"lift_pairs\":" << record.timings.lift_pairs
                          << ",\"distinct_codomains\":"
                          << record.timings.distinct_codomains
                          << ",\"codomain_cache_hits\":"
                          << record.timings.codomain_cache_hits
                          << ",\"eigenvalue_attempts\":"
                          << record.timings.eigenvalue_attempts
                          << ",\"conjugate_eigenvalue_reuse\":"
                          << (record.timings.conjugate_eigenvalue_reuse
                                  ? "true" : "false")
                          << ",\"independent_eigenvalue_recoveries\":"
                          << record.timings.independent_eigenvalue_recoveries
                          << ",\"conjugate_eigenvalues_derived\":"
                          << record.timings.conjugate_eigenvalues_derived
                          << "}}\n" << std::flush;
            };
            const auto result = oneshotsea::run_weber_sea_reference(
                curve, required(options, "table-dir"), max_level,
                static_cast<std::size_t>(trace_cap_u64), progress,
                static_cast<std::size_t>(sea_threads_u64),
                root_orbit_reuse_u64 != 0U,
                conjugate_eigenvalue_reuse_u64 != 0U,
                level_estimates);
            std::cout << "{\"type\":\"summary\",\"exact_modulus\":\""
                      << result.constraints.modulus()
                      << "\",\"constraint_modulus\":\""
                      << result.effective_constraints.modulus()
                      << "\",\"exact_trace_candidates\":\""
                      << result.constraints.candidate_count()
                      << "\",\"trace_candidates\":\""
                      << result.effective_constraints.candidate_count()
                      << "\",\"atkin_constraints\":"
                      << result.atkin_constraints.size()
                      << ",\"compatible_source_lifts\":"
                      << result.compatible_source_lifts.size()
                      << ",\"levels_processed\":" << result.levels.size()
                      << ",\"prime_schedule\":\"" << prime_schedule << "\""
                      << ",\"status\":\""
                      << (result.compatible_source_lifts.empty() &&
                                  result.levels.empty()
                              ? "no_rational_weber_lift"
                              : (result.traces.has_value()
                                     ? "trace_set_enumerated"
                                     : "level_limit"))
                      << "\""
                      << ",\"complete\":"
                      << (result.traces.has_value() ? "true" : "false");
            if (result.traces.has_value()) {
                std::cout << ",\"traces\":[";
                for (std::size_t index = 0; index < result.traces->size(); ++index) {
                    if (index != 0U) {
                        std::cout << ',';
                    }
                    std::cout << '\"' << (*result.traces)[index] << '\"';
                }
                std::cout << ']';
            }
            std::cout << "}\n";
            return 0;
        }
        if (command == "elkies-residue" || command == "elkies-bmss-residue" ||
            command == "elkies-weber-residue" ||
            command == "elkies-division-residue") {
            const std::uint64_t level = required_u64(options, "ell");
            std::vector<oneshotsea::ElkiesKernelResult> kernels;
            oneshotsea::ElkiesStageTimings weber_timings;
            if (command == "elkies-residue" ||
                command == "elkies-bmss-residue" ||
                command == "elkies-weber-residue") {
                if (level > std::numeric_limits<unsigned>::max()) {
                    throw std::invalid_argument("--ell is out of range");
                }
                const auto modular_polynomial =
                    oneshotsea::SparseModularPolynomial::load(
                        static_cast<unsigned>(level), required(options, "file"));
                if (command == "elkies-weber-residue") {
                    kernels = oneshotsea::elkies_kernels_weber_bmss_reference(
                        curve, modular_polynomial, &weber_timings);
                } else if (command == "elkies-bmss-residue") {
                    kernels = oneshotsea::elkies_kernels_bmss_reference(
                        curve, modular_polynomial);
                } else {
                    kernels = oneshotsea::elkies_kernels_reference(
                        curve, modular_polynomial);
                }
            } else {
                kernels =
                    oneshotsea::elkies_kernels_division_reference(curve, level);
            }
            const bool weber_unavailable =
                command == "elkies-weber-residue" && kernels.empty();
            std::cout << "{\"p\":\"" << p << "\",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"ell\":"
                      << level << ",\"status\":\""
                      << (kernels.empty()
                              ? (weber_unavailable ? "unavailable" : "atkin")
                              : "exact")
                      << "\",\"elkies\":"
                      << (weber_unavailable ? "null"
                                            : (kernels.empty() ? "false" : "true"))
                      << ",\"kernel_count\":" << kernels.size();
            if (!kernels.empty()) {
                const std::uint64_t residue = kernels.front().trace_residue;
                for (const auto& kernel : kernels) {
                    if (kernel.trace_residue != residue) {
                        throw std::runtime_error(
                            "Elkies kernels imply inconsistent trace residues");
                    }
                }
                std::cout << ",\"trace_residue\":" << residue;
            }
            if (command == "elkies-weber-residue") {
                std::cout << ",\"modular_root_workers\":"
                          << weber_timings.modular_root_workers
                          << ",\"timings_us\":{\"source_lifts\":"
                          << weber_timings.source_lifts_us
                          << ",\"modular_roots\":"
                          << weber_timings.modular_roots_us
                          << ",\"normalized_codomain\":"
                          << weber_timings.normalized_codomain_us
                          << ",\"bmss\":" << weber_timings.bmss_us
                          << ",\"eigenvalue\":"
                          << weber_timings.eigenvalue_us
                          << ",\"lift_pairs\":"
                          << weber_timings.lift_pairs
                          << ",\"distinct_codomains\":"
                          << weber_timings.distinct_codomains
                          << ",\"codomain_cache_hits\":"
                          << weber_timings.codomain_cache_hits
                          << ",\"eigenvalue_attempts\":"
                          << weber_timings.eigenvalue_attempts
                          << ",\"conjugate_eigenvalue_reuse\":"
                          << (weber_timings.conjugate_eigenvalue_reuse
                                  ? "true" : "false")
                          << ",\"independent_eigenvalue_recoveries\":"
                          << weber_timings.independent_eigenvalue_recoveries
                          << ",\"conjugate_eigenvalues_derived\":"
                          << weber_timings.conjugate_eigenvalues_derived << '}';
            }
            std::cout << "}\n";
            return 0;
        }
        if (command == "modpoly") {
            const unsigned level = required_unsigned(options, "level");
            const auto modular_polynomial = oneshotsea::SparseModularPolynomial::load(
                level, required(options, "file"));
            const oneshotsea::Poly specialized = modular_polynomial.evaluate_x(
                field, curve.j_invariant());
            const int root_count = oneshotsea::rational_root_count(specialized);
            std::cout << "{\"p\":\"" << p << "\",\"level\":" << level
                      << ",\"j\":\"" << curve.j_invariant()
                      << "\",\"degree\":" << specialized.degree()
                      << ",\"rational_roots\":" << root_count << "}\n";
            return 0;
        }
        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "oneshotsea: " << error.what() << '\n';
        return 1;
    }
}
