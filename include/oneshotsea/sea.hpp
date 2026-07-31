#pragma once

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
    mpz_class trace_candidate_count;
    std::size_t compatible_source_lifts;
    ElkiesStageTimings timings;
};

struct WeberSeaResult {
    TraceConstraints constraints;
    std::vector<WeberSeaLevelRecord> levels;
    std::vector<mpz_class> compatible_source_lifts;
    std::optional<std::vector<mpz_class>> traces;
};

using WeberSeaProgress = std::function<void(const WeberSeaLevelRecord&)>;

// Increasing-level correctness runner for the checked-in Weber table set.
// Positive kernel evidence narrows the possible source lifts; empty/Atkin
// levels never discard a lift. Stops once the complete exact-residue Hasse set
// fits `trace_cap`, or once all tables through `max_level` are exhausted.
WeberSeaResult run_weber_sea_reference(
    const Curve& curve, const std::string& table_directory,
    std::uint64_t max_level, std::size_t trace_cap,
    const WeberSeaProgress& progress = {},
    std::size_t modular_root_threads = 0);

}  // namespace oneshotsea
