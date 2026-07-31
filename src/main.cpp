#include "oneshotsea/curve.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/search_pipeline.hpp"
#include "oneshotsea/schoof.hpp"
#include "oneshotsea/sea.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
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

std::string optional_string(const std::map<std::string, std::string>& options,
                            const std::string& name, std::string fallback) {
    const auto found = options.find(name);
    return found == options.end() ? std::move(fallback) : found->second;
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

void usage() {
    std::cerr
        << "usage:\n"
        << "  oneshotsea curve --p P --seed S --index I\n"
        << "  oneshotsea montgomery-curve --p P --seed S --index I\n"
        << "  oneshotsea point-count --p P --a A --b B\n"
        << "  oneshotsea schoof-residue --p P --a A --b B --ell L\n"
        << "  oneshotsea schoof-count --p P --a A --b B --max-ell L\n"
        << "  oneshotsea elkies-residue --p P --a A --b B --ell L --file PATH\n"
        << "  oneshotsea elkies-bmss-residue --p P --a A --b B --ell L --file PATH\n"
        << "  oneshotsea elkies-weber-residue --p P --a A --b B --ell L --file PATH\n"
        << "  oneshotsea elkies-division-residue --p P --a A --b B --ell L\n"
        << "  oneshotsea sea-weber-count --p P --a A --b B --max-level L --table-dir PATH --trace-cap N\n"
        << "  oneshotsea search --p P --seed S --range-start I --range-end J --worker-id W --worker-count N --max-level L --table-dir PATH --smooth-cache PATH --checkpoint PATH [--max-curves N]\n"
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
        if (command == "search") {
            oneshotsea::SearchPipelineConfig config;
            config.prime = oneshotsea::parse_integer(required(options, "p"));
            config.seed = required_u64(options, "seed");
            config.table_directory = required(options, "table-dir");
            config.max_level = required_u64(options, "max-level");
            const std::uint64_t trace_cap = optional_u64(
                options, "trace-cap", 4096U);
            const std::uint64_t assembly_attempts = optional_u64(
                options, "assembly-attempts", 400U);
            const std::uint64_t max_certificate_candidates = optional_u64(
                options, "max-certificate-candidates", 100000U);
            const std::uint64_t max_candidate_search_nodes = optional_u64(
                options, "max-candidate-search-nodes", 1000000U);
            if (trace_cap > std::numeric_limits<std::size_t>::max() ||
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
                options, "smooth-max-batch", 4096U);
            if (smooth_batch == 0U ||
                smooth_batch > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("--smooth-max-batch is out of range");
            }
            smooth_options.max_orders_per_batch =
                static_cast<std::size_t>(smooth_batch);
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
            run_options.checkpoint_every = optional_u64(
                options, "checkpoint-every", 1U);
            run_options.checkpoint_path = checkpoint;
            run_options.progress_path = progress;
            run_options.certificate_path = certificate_output;
            std::cout << "{\"schema\":\"oneshotsea.search-start.v1\""
                      << ",\"prime\":\"" << config.prime
                      << "\",\"seed\":\"" << config.seed
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
                      << "\",\"heuristic_rejection\":false"
                      << ",\"resources\":{\"smooth_threads\":\""
                      << smooth_threads << "\",\"smooth_max_batch\":\""
                      << smooth_batch
                      << "\",\"smooth_build_segment_span\":\""
                      << smooth_build_segment_span
                      << "\",\"assembly_attempts\":\""
                      << config.assembly_attempts << "\",\"trace_cap\":\""
                      << config.early_trace_cap
                      << "\",\"max_certificate_candidates\":\""
                      << config.max_certificate_candidates
                      << "\",\"max_candidate_search_nodes\":\""
                      << config.max_candidate_search_nodes << "\"}}\n"
                      << std::flush;
            const auto callback = [](const oneshotsea::SearchCurveReport& report,
                                     const oneshotsea::SearchState& current) {
                std::cout << oneshotsea::search_curve_report_json(report, current)
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
                      << (result.verified.has_value() ? "true" : "false")
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
            if (trace_cap_u64 > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("--trace-cap is out of range");
            }
            const auto progress = [](const oneshotsea::WeberSeaLevelRecord& record) {
                std::cout << "{\"type\":\"level\",\"ell\":" << record.ell
                          << ",\"exact\":" << (record.exact ? "true" : "false");
                if (record.trace_residue.has_value()) {
                    std::cout << ",\"trace_residue\":" << *record.trace_residue;
                }
                std::cout << ",\"exact_modulus\":\"" << record.exact_modulus
                          << "\",\"trace_candidates\":\""
                          << record.trace_candidate_count
                          << "\",\"compatible_source_lifts\":"
                          << record.compatible_source_lifts
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
                          << "}}\n" << std::flush;
            };
            const auto result = oneshotsea::run_weber_sea_reference(
                curve, required(options, "table-dir"), max_level,
                static_cast<std::size_t>(trace_cap_u64), progress);
            std::cout << "{\"type\":\"summary\",\"exact_modulus\":\""
                      << result.constraints.modulus()
                      << "\",\"trace_candidates\":\""
                      << result.constraints.candidate_count()
                      << "\",\"compatible_source_lifts\":"
                      << result.compatible_source_lifts.size()
                      << ",\"levels_processed\":" << result.levels.size()
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
                std::cout << ",\"timings_us\":{\"source_lifts\":"
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
                          << weber_timings.eigenvalue_attempts << '}';
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
