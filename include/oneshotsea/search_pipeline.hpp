#pragma once

#include "oneshotsea/certificate.hpp"
#include "oneshotsea/exact_smooth.hpp"
#include "oneshotsea/integrity.hpp"
#include "oneshotsea/search_checkpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace oneshotsea {

enum class SearchCurveFamily : std::uint8_t {
    weber_f,
    x1_11,
    x1_27,
};

const char* search_curve_family_name(SearchCurveFamily family);

// Semantic configuration of the deterministic production search.  The
// Sound early rejection always requires a complete trace set and exact
// n^4-smooth parts. A separately labeled opt-in policy may retire a curve
// when the available SEA levels cannot complete its trace set; this can lose
// search opportunities but can never create a false certificate.
struct SearchPipelineConfig {
    mpz_class prime;
    std::uint64_t seed = 0;
    SearchCurveFamily curve_family = SearchCurveFamily::weber_f;
    // Semantic only for X1 families. Requiring point four additionally
    // guarantees the validated order-divisor metadata recorded by the
    // selected generator.
    bool x1_require_point_four = false;
    std::filesystem::path table_directory;
    std::uint64_t max_level = 0;
    // One default bounded-smoothness batch: every trace produces a curve and
    // twist order, so 64 traces fill the search CLI's 128-order default.  A
    // larger cap can make an early exact screen slower than finishing SEA.
    std::size_t early_trace_cap = 64;
    // Opt into the fixed retained-state exact-Schoof tail after all configured
    // Weber levels fail to fit the requested trace cap. This is semantic and
    // is bound into the resumable schedule identity.
    bool enable_schoof_fallback = false;
    // Optional callback-free classical-j direct SEA tail, evaluated after the
    // authenticated Weber schedule and before exact Schoof fallback.  The
    // strictly increasing prime list and both failure caps are semantic and
    // are bound into the resumable schedule identity.  An empty list retains
    // the published table-backed behavior.
    std::vector<std::uint64_t> classical_direct_levels;
    std::uint64_t classical_direct_maximum_prime_candidates = 1000000U;
    std::uint64_t classical_direct_maximum_x_candidates_per_surface =
        1000000U;
    bool skip_incomplete_curves = false;
    // Maximum concurrent modular-root jobs inside table SEA and independent
    // auxiliary-prime jobs during direct-level preparation. Zero selects the
    // available CPU concurrency. This is a resource setting and deliberately
    // does not alter the deterministic schedule/checkpoint identity.
    std::size_t sea_threads = 0;
    std::size_t assembly_attempts = 400;
    std::size_t max_certificate_candidates = 100000;
    std::size_t max_candidate_search_nodes = 1000000;
    std::uint64_t certificate_seed = 1;
    std::filesystem::path canonical_verifier;
    std::string python_executable = "python3";
    // Optional run-time assertions used by the CLI after it has computed the
    // content identities.  Library users may leave these empty while building
    // an identity, then set them before executing the run.
    std::string expected_schedule_sha256;
    std::string expected_smooth_cache_sha256;
    std::string expected_table_manifest_sha256;
    std::string expected_verifier_sha256;
    std::string expected_python_sha256;
};

enum class SearchCurveStatus : std::uint8_t {
    no_rational_weber_lift,
    sea_level_limit,
    heuristic_no_lift_skip,
    heuristic_level_limit_skip,
    sound_smoothness_reject,
    no_certificate_candidate,
    certificate_assembly_failed,
    canonical_verifier_rejected,
    verified_certificate,
};

const char* search_curve_status_name(SearchCurveStatus status);

struct SearchCurveTimings {
    std::uint64_t generation_us = 0;
    std::uint64_t sea_us = 0;
    std::uint64_t smoothness_us = 0;
    std::uint64_t candidate_us = 0;
    std::uint64_t assembly_us = 0;
    std::uint64_t verifier_us = 0;
    std::uint64_t total_us = 0;
};

struct SearchSeaLevelTiming {
    std::size_t pass = 0;
    std::uint64_t ell = 0;
    bool exact = false;
    std::optional<std::uint64_t> trace_residue;
    mpz_class exact_modulus = 1;
    mpz_class constraint_modulus = 1;
    mpz_class exact_trace_candidate_count = 0;
    mpz_class trace_candidate_count = 0;
    std::optional<std::uint64_t> atkin_projective_order;
    std::size_t atkin_residue_count = 0;
    std::size_t compatible_source_lifts = 0;
    std::size_t modular_root_workers = 0;
    std::size_t modular_root_orbits = 0;
    std::size_t modular_root_reused_lifts = 0;
    bool modular_root_orbit_reuse = false;
    std::uint64_t source_lifts_us = 0;
    std::uint64_t modular_roots_us = 0;
    std::uint64_t normalized_codomain_us = 0;
    std::uint64_t bmss_us = 0;
    std::uint64_t eigenvalue_us = 0;
    bool conjugate_eigenvalue_reuse = false;
    std::uint64_t eigenvalue_attempts = 0;
    std::uint64_t independent_eigenvalue_recoveries = 0;
    std::uint64_t conjugate_eigenvalues_derived = 0;
};

struct SearchSchoofFallbackTiming {
    std::size_t pass = 0;
    std::uint64_t ell = 0;
    std::uint64_t trace_residue = 0;
    mpz_class exact_modulus = 1;
    mpz_class constraint_modulus = 1;
    mpz_class exact_trace_candidate_count = 0;
    mpz_class trace_candidate_count = 0;
    std::uint64_t elapsed_us = 0;
};

struct SearchClassicalDirectLevelTiming {
    std::size_t pass = 0;
    std::uint64_t ell = 0;
    bool exact = false;
    std::optional<std::uint64_t> trace_residue;
    mpz_class order_discriminant = 0;
    std::uint64_t class_number = 0;
    std::size_t auxiliary_prime_count = 0;
    std::size_t elkies_kernel_count = 0;
    mpz_class exact_modulus = 1;
    mpz_class constraint_modulus = 1;
    mpz_class exact_trace_candidate_count = 0;
    mpz_class trace_candidate_count = 0;
    std::optional<std::uint64_t> atkin_projective_order;
    std::size_t atkin_residue_count = 0;
    std::uint64_t elapsed_us = 0;
};

struct SearchCurveReport {
    std::uint64_t global_index = 0;
    SearchCurveStatus status = SearchCurveStatus::sea_level_limit;
    CurveSearchOutcome outcome{CurveTerminalStage::rejected_sea, false, false};
    std::uint64_t rejected_generator_samples = 0;
    std::optional<std::uint64_t> trace_prior_modulus;
    std::optional<std::uint64_t> trace_prior_residue;
    std::size_t sea_passes = 0;
    std::size_t sea_levels = 0;
    std::size_t exact_sea_levels = 0;
    std::size_t atkin_sea_levels = 0;
    std::size_t classical_direct_passes = 0;
    std::vector<SearchClassicalDirectLevelTiming> classical_direct_levels;
    std::vector<SearchSchoofFallbackTiming> schoof_fallback_levels;
    std::optional<mpz_class> final_exact_trace_candidate_count;
    std::optional<mpz_class> final_trace_candidate_count;
    std::vector<SearchSeaLevelTiming> sea_level_timings;
    std::size_t initial_trace_count = 0;
    std::optional<mpz_class> exact_trace;
    bool certificate_uses_twist_order = false;
    bool certificate_uses_odd_only = false;
    MontgomerySide certificate_montgomery_side = MontgomerySide::either;
    std::size_t candidate_attempts = 0;
    std::size_t candidate_search_nodes = 0;
    std::size_t assembly_attempts = 0;
    std::size_t canonical_rejections = 0;
    std::optional<MontgomeryCertificate> certificate;
    SearchCurveTimings timings;
};

// Emitted synchronously after one SEA level has completed.  This is
// diagnostic observability only: partial level progress never advances or
// alters the durable search checkpoint.
using SearchSeaLevelCallback =
    std::function<void(std::uint64_t, const SearchSeaLevelTiming&)>;
using SearchClassicalDirectLevelCallback = std::function<void(
    std::uint64_t, const SearchClassicalDirectLevelTiming&)>;

// Injectable so a focused integration test can assert verifier invocation.
// Production callers should leave this empty to invoke the unmodified script.
using CanonicalCertificateVerifier =
    std::function<bool(const MontgomeryCertificate&)>;

// Resolve through PATH once, canonicalize symlinks, and require a regular
// executable.  Search schedules bind both this absolute path and its digest.
std::string resolve_executable_path(const std::string& executable);
bool authenticate_python3_interpreter(
    const std::string& absolute_python_executable);

bool verify_with_canonical_voneshot(
    const MontgomeryCertificate& certificate,
    const std::filesystem::path& verifier,
    const std::string& python_executable = "python3");

SearchCurveReport process_search_curve(
    const SearchPipelineConfig& config, const ExactSmoothEngine& smooth_engine,
    std::uint64_t global_index,
    const CanonicalCertificateVerifier& verifier = {},
    const SearchSeaLevelCallback& sea_level_callback = {},
    const ExactSmoothBatchCoordinator* smooth_coordinator = nullptr,
    const SearchClassicalDirectLevelCallback&
        classical_direct_level_callback = {});

struct SearchPipelineRunOptions {
    // Zero means no curves; callers choose an explicit cap or the remaining
    // assigned range.  This makes accidental unbounded test runs impossible.
    std::uint64_t max_curves = 0;
    // Maximum curves evaluated concurrently against the same immutable
    // ExactSmoothEngine/cache. Reports and all durable artifacts are still
    // committed strictly in increasing index order. This resource setting is
    // deliberately absent from the resumable search identity. Per-curve SEA
    // worker counts are not silently divided. With no smooth coordinators,
    // smooth work also runs per curve; with a coordinator pool it runs through
    // at most smooth_coordinator_count simultaneous smooth calls. Callers must
    // budget those worker products explicitly.
    std::size_t curve_threads = 1;
    // Zero keeps independent exact-smooth calls. A positive value routes each
    // curve deterministically by global_index modulo this many no-delay FIFO
    // coordinators, retaining that many scans in parallel while batching
    // requests queued within each cohort. Must not exceed curve_threads. This
    // is a resource setting and is absent from the resumable search identity.
    std::size_t smooth_coordinator_count = 0;
    std::uint64_t checkpoint_every = 1;
    std::filesystem::path checkpoint_path;
    std::filesystem::path progress_path;
    // Required for a nonempty run.  A canonical certificate is durably
    // published here before the checkpoint cursor advances.
    std::filesystem::path certificate_path;
    // Per-level SEA timings are valuable for benchmarks but can dominate the
    // retained log of a long production search. Disabling them preserves the
    // per-curve outcome, aggregate SEA counts, and major-kernel timings.
    bool include_sea_level_timings = true;
    // With curve_threads > 1 this callback can observe interleaved indices,
    // but invocations are serialized so each live telemetry record is atomic.
    // Level telemetry is diagnostic and never advances the checkpoint.
    SearchSeaLevelCallback sea_level_callback;
    SearchClassicalDirectLevelCallback classical_direct_level_callback;
};

struct ExactSmoothBatchPoolTelemetry {
    std::uint64_t submitted_requests = 0U;
    std::uint64_t completed_requests = 0U;
    std::uint64_t failed_requests = 0U;
    std::uint64_t cancelled_requests = 0U;
    std::uint64_t coordinator_batches = 0U;
    std::uint64_t successful_cache_scan_chunks = 0U;
    std::uint64_t submitted_orders = 0U;
    // These are maxima of the corresponding per-coordinator maxima, never a
    // claim about the sum of simultaneous queue depth across the whole pool.
    std::size_t max_queued_requests_in_any_cohort = 0U;
    std::size_t max_requests_per_batch_in_any_cohort = 0U;
    std::size_t max_orders_per_successful_scan_chunk_in_any_cohort = 0U;
    std::vector<ExactSmoothScanChunkSizeCount>
        successful_scan_chunks_by_order_count;
};

// Transactionally merge one coordinator snapshot. Throws overflow_error and
// leaves aggregate unchanged if any uint64 counter would wrap.
void merge_exact_smooth_batch_pool_telemetry(
    ExactSmoothBatchPoolTelemetry& aggregate,
    const ExactSmoothBatchTelemetry& cohort);

struct SearchPipelineRunResult {
    std::uint64_t curves_processed = 0;
    bool exhausted_assigned_range = false;
    std::optional<SearchCurveReport> verified;
    // Curve-independent direct-SEA levels constructed lazily at most once by
    // this invocation and then shared read-only across curve workers.  The
    // aggregate preparation time is reported separately for amortization;
    // the first worker's wall-clock SEA time necessarily includes its wait.
    std::size_t classical_direct_context_count = 0U;
    std::uint64_t classical_direct_preparation_us = 0U;
    // Exact payload of the two compact uint64 interpolation matrices. This
    // excludes witness metadata, vector headers, and allocator overhead.
    std::size_t classical_direct_interpolation_coefficient_count = 0U;
    std::size_t classical_direct_interpolation_storage_bytes = 0U;
    // True only when this invocation actually constructed the resource-only
    // cross-curve exact-smooth coordinator pool.
    bool smooth_batch_coordinator_enabled = false;
    std::size_t smooth_batch_coordinator_count = 0U;
    ExactSmoothBatchPoolTelemetry smooth_batch_telemetry;
    // Indexed by global_index modulo smooth_batch_coordinator_count.
    std::vector<ExactSmoothBatchTelemetry> smooth_batch_cohort_telemetry;
};

using SearchReportCallback =
    std::function<void(const SearchCurveReport&, const SearchState&)>;

// Advances SearchState only after a complete terminal report.  Checkpoints
// therefore never contain partial SEA residues.  Processing stops immediately
// after canonical verification succeeds.
SearchPipelineRunResult run_search_pipeline(
    const SearchPipelineConfig& config, const ExactSmoothEngine& smooth_engine,
    SearchState& state, const SearchPipelineRunOptions& options,
    const SearchReportCallback& report_callback = {},
    const CanonicalCertificateVerifier& verifier = {});

std::string search_curve_report_json(const SearchCurveReport& report,
                                     const SearchState& state,
                                     bool include_sea_level_timings = true);
std::string search_sea_level_json(std::uint64_t global_index,
                                  const SearchSeaLevelTiming& level);
std::string search_classical_direct_level_json(
    std::uint64_t global_index,
    const SearchClassicalDirectLevelTiming& level);

// Content identities used to bind resumable checkpoints.  The schedule hash
// includes the exact smooth-cache and canonical-verifier digests.
std::string weber_table_manifest_sha256(
    const std::filesystem::path& table_directory, std::uint64_t max_level);
std::string search_schedule_sha256(
    const SearchPipelineConfig& config,
    const std::string& smooth_cache_sha256,
    const std::string& canonical_verifier_sha256);

SearchIdentity make_search_identity(
    const SearchPipelineConfig& config, SearchRange global_range,
    std::uint64_t worker_id, std::uint64_t worker_count,
    const std::string& smooth_cache_sha256,
    const std::string& canonical_verifier_sha256,
    const std::string& build_id);

}  // namespace oneshotsea
