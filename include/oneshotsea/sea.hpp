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

// A caller-supplied exact congruence for the Frobenius trace. The modulus may
// be composite, but must fit uint64, be coprime to the field characteristic,
// and admit at least one trace in the Hasse interval. The residue is required
// to already be canonical in [0, modulus).
class ExactTracePrior {
public:
    ExactTracePrior(mpz_class prime, std::uint64_t modulus,
                    std::uint64_t residue);

    const mpz_class& prime() const { return prime_; }
    std::uint64_t modulus() const { return modulus_; }
    std::uint64_t residue() const { return residue_; }

private:
    mpz_class prime_;
    std::uint64_t modulus_;
    std::uint64_t residue_;
};

// Return t = p+1 (mod 4) only after directly validating that the short
// Weierstrass cubic has all three rational 2-torsion roots. Curves without
// full rational E[2] return no prior.
std::optional<ExactTracePrior>
exact_trace_prior_from_full_rational_two_torsion(const Curve& curve);

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
    // The exact caller prior, when present, followed by exact Elkies residues.
    // These remain the final unique-trace gate.
    TraceConstraints constraints;
    // The same exact prior and Elkies residues, plus independently certified
    // Atkin constraints. This state may supply a complete bounded trace set
    // for sound smoothness screening.
    TraceConstraints effective_constraints;
    std::vector<AtkinConstraint> atkin_constraints;
    std::vector<WeberSeaLevelRecord> levels;
    std::vector<mpz_class> compatible_source_lifts;
    std::optional<std::vector<mpz_class>> traces;
};

using WeberSeaProgress = std::function<void(const WeberSeaLevelRecord&)>;

// Benchmark-only scheduling input.  information_units may use any fixed
// scale (for example measured microbits of trace-candidate reduction), while
// expected_cost_us is the correspondingly measured level cost.  Production
// search leaves this empty and therefore retains increasing-prime order.
struct WeberSeaLevelEstimate {
    std::uint64_t ell;
    std::uint64_t information_units;
    std::uint64_t expected_cost_us;
};

// Return a strict permutation of increasing_levels, sorted by decreasing
// information_units / expected_cost_us.  Products are compared exactly and
// equal scores retain increasing-prime order.  The estimates must cover every
// available level exactly once and every measured cost must be nonzero.
std::vector<std::uint64_t> expected_information_per_cost_order(
    const std::vector<std::uint64_t>& increasing_levels,
    const std::vector<WeberSeaLevelEstimate>& estimates);

// Increasing-level correctness runner for the checked-in Weber table set.
// Positive kernel evidence narrows the possible source lifts; empty/Atkin
// levels never discard a lift. For trace_cap>1, independently certified
// classical-j Atkin constraints at checked-in levels 5 and 7 may produce the
// complete bounded trace set used for sound early screening. trace_cap=1
// always requires uniqueness from the exact prior and Elkies residues alone.
// A known_source_lift is an optional generator witness: it must already be
// normalized, nonzero, unramified/nonexceptional, and map to the curve's
// exact j-invariant. Invalid witnesses are rejected rather than silently
// falling back to lift discovery.
WeberSeaResult run_weber_sea_reference(
    const Curve& curve, const std::string& table_directory,
    std::uint64_t max_level, std::size_t trace_cap,
    const WeberSeaProgress& progress = {},
    std::size_t modular_root_threads = 0,
    bool enable_root_orbit_reuse = true,
    bool enable_conjugate_eigenvalue_reuse = true,
    const std::vector<WeberSeaLevelEstimate>& level_estimates = {},
    const std::optional<ExactTracePrior>& trace_prior = std::nullopt,
    const std::optional<mpz_class>& known_source_lift = std::nullopt);

}  // namespace oneshotsea
