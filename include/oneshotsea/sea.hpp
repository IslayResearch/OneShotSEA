#pragma once

#include "oneshotsea/atkin.hpp"
#include "oneshotsea/curve.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/trace.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace oneshotsea {

struct WeberSeaLevelRecord {
    std::uint64_t ell;
    bool exact;
    std::optional<std::uint64_t> trace_residue;
    mpz_class exact_modulus;
    mpz_class constraint_modulus;
    mpz_class exact_trace_candidate_count;
    mpz_class trace_candidate_count;
    std::optional<std::uint64_t> atkin_projective_order;
    std::size_t atkin_residue_count;
    std::size_t compatible_source_lifts;
    ElkiesStageTimings timings;
};

struct WeberSeaResult {
    // Exact Elkies residues only. They remain the final unique-trace gate.
    TraceConstraints constraints;
    // Exact Elkies plus independently certified Atkin constraints. This state
    // may supply a complete bounded trace set for sound smoothness screening.
    TraceConstraints effective_constraints;
    std::vector<AtkinConstraint> atkin_constraints;
    std::vector<WeberSeaLevelRecord> levels;
    std::vector<mpz_class> compatible_source_lifts;
    std::optional<std::vector<mpz_class>> traces;
};

using WeberSeaProgress = std::function<void(const WeberSeaLevelRecord&)>;

// Increasing-level correctness runner for the checked-in Weber table set.
// Positive kernel evidence narrows the possible source lifts; empty/Atkin
// levels never discard a lift. For trace_cap>1, independently certified
// classical-j Atkin constraints at checked-in levels 5 and 7 may produce the
// complete bounded trace set used for sound early screening. trace_cap=1
// always requires uniqueness from exact Elkies residues alone.
WeberSeaResult run_weber_sea_reference(
    const Curve& curve, const std::string& table_directory,
    std::uint64_t max_level, std::size_t trace_cap,
    const WeberSeaProgress& progress = {},
    std::size_t modular_root_threads = 0,
    bool enable_root_orbit_reuse = true);

}  // namespace oneshotsea
