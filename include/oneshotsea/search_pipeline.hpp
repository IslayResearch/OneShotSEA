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

// Semantic configuration of the deterministic production search.  The
// production path deliberately has no heuristic-rejection switch: every
// early rejection is justified by a complete trace set and exact n^4-smooth
// parts, or is an explicit SEA availability/level-limit outcome.
struct SearchPipelineConfig {
    mpz_class prime;
    std::uint64_t seed = 0;
    std::filesystem::path table_directory;
    std::uint64_t max_level = 0;
    // One default bounded-smoothness batch: every trace produces a curve and
    // twist order, so 64 traces fill the search CLI's 128-order default.  A
    // larger cap can make an early exact screen slower than finishing SEA.
    std::size_t early_trace_cap = 64;
    // Maximum concurrent modular-root jobs inside SEA. Zero selects the
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
    std::string expected_table_manifest_sha256;
    std::string expected_verifier_sha256;
    std::string expected_python_sha256;
};

enum class SearchCurveStatus : std::uint8_t {
    no_rational_weber_lift,
    sea_level_limit,
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

struct SearchCurveReport {
    std::uint64_t global_index = 0;
    SearchCurveStatus status = SearchCurveStatus::sea_level_limit;
    CurveSearchOutcome outcome{CurveTerminalStage::rejected_sea, false, false};
    std::uint64_t rejected_generator_samples = 0;
    std::size_t sea_passes = 0;
    std::size_t sea_levels = 0;
    std::size_t exact_sea_levels = 0;
    std::size_t atkin_sea_levels = 0;
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
    const SearchSeaLevelCallback& sea_level_callback = {});

struct SearchPipelineRunOptions {
    // Zero means no curves; callers choose an explicit cap or the remaining
    // assigned range.  This makes accidental unbounded test runs impossible.
    std::uint64_t max_curves = 0;
    // Maximum curves evaluated concurrently against the same immutable
    // ExactSmoothEngine/cache. Reports and all durable artifacts are still
    // committed strictly in increasing index order. This resource setting is
    // deliberately absent from the resumable search identity. Per-curve SEA
    // and smooth worker counts/caps are not silently divided, so callers must
    // budget their product with this value explicitly.
    std::size_t curve_threads = 1;
    std::uint64_t checkpoint_every = 1;
    std::filesystem::path checkpoint_path;
    std::filesystem::path progress_path;
    // Required for a nonempty run.  A canonical certificate is durably
    // published here before the checkpoint cursor advances.
    std::filesystem::path certificate_path;
    // With curve_threads > 1 this callback can observe interleaved indices,
    // but invocations are serialized so each live telemetry record is atomic.
    // Level telemetry is diagnostic and never advances the checkpoint.
    SearchSeaLevelCallback sea_level_callback;
};

struct SearchPipelineRunResult {
    std::uint64_t curves_processed = 0;
    bool exhausted_assigned_range = false;
    std::optional<SearchCurveReport> verified;
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
                                     const SearchState& state);
std::string search_sea_level_json(std::uint64_t global_index,
                                  const SearchSeaLevelTiming& level);

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
