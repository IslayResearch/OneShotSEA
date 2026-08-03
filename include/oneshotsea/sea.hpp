#pragma once

#include "oneshotsea/atkin.hpp"
#include "oneshotsea/cm_surface.hpp"
#include "oneshotsea/curve.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/trace.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace oneshotsea {

// The production search may opt into this fixed, schedule-bound tail only
// after exhausting its authenticated Weber levels.  Keeping the list fixed
// makes checkpoint identities deterministic and caps the slow reference work.
inline constexpr char kRareSchoofFallbackPolicy[] =
    "retained-state-exact-schoof-3,5,7,11,13,17,19,23,29,31,37-v2";
inline constexpr char kClassicalDirectSeaPolicy[] =
    "retained-state-three-power-classical-j-crt-bmss-atkin-v2";

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

struct SchoofFallbackLevelRecord {
    std::uint64_t ell;
    std::uint64_t trace_residue;
    mpz_class exact_modulus;
    mpz_class constraint_modulus;
    mpz_class exact_trace_candidate_count;
    mpz_class trace_candidate_count;
    std::uint64_t elapsed_us;
};

struct ClassicalDirectSeaLevelRecord {
    std::uint64_t ell;
    bool exact;
    std::optional<std::uint64_t> trace_residue;
    mpz_class order_discriminant;
    std::uint64_t class_number;
    std::size_t auxiliary_prime_count;
    std::size_t elkies_kernel_count;
    mpz_class exact_modulus;
    mpz_class constraint_modulus;
    mpz_class exact_trace_candidate_count;
    mpz_class trace_candidate_count;
    std::optional<std::uint64_t> atkin_projective_order;
    std::size_t atkin_residue_count;
    std::uint64_t elapsed_us;
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
    std::vector<SchoofFallbackLevelRecord> schoof_fallback_levels;
    std::vector<mpz_class> compatible_source_lifts;
    std::optional<std::vector<mpz_class>> traces;
    std::vector<ClassicalDirectSeaLevelRecord> classical_direct_levels;
};

using WeberSeaProgress = std::function<void(const WeberSeaLevelRecord&)>;
using SchoofFallbackProgress =
    std::function<void(const SchoofFallbackLevelRecord&)>;
using ClassicalDirectSeaProgress =
    std::function<void(const ClassicalDirectSeaLevelRecord&)>;

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

// Extend an already-computed Weber state with a fixed set of independent,
// exact Schoof residues. Existing exact prior/table moduli are skipped. If an
// exact residue upgrades an existing Atkin modulus, effective constraints are
// rebuilt from the exact state plus only the remaining nonredundant Atkin
// constraints. The existing Weber work is never repeated.
void extend_weber_sea_with_schoof_fallback(
    const Curve& curve, WeberSeaResult& result, std::size_t trace_cap,
    const SchoofFallbackProgress& progress = {});

// Extend retained SEA constraints with callback-free classical direct
// evaluations at the requested strictly increasing prime levels.  Each level
// derives its D=-7*3^(2n) order and HCP state internally.  Positive roots are
// admitted only through BMSS/Frobenius exact residues; square-free no-root
// specializations contribute only certified Atkin sets.  Completion for
// trace_cap>1 uses the effective exact+Atkin state, while trace_cap=1 still
// requires exact residues.  State updates and progress callbacks are
// transactional per level.
void extend_sea_with_classical_direct(
    const Curve& curve, WeberSeaResult& result,
    const std::vector<std::uint64_t>& levels, std::size_t trace_cap,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface,
    const ClassicalDirectSeaProgress& progress = {});

// Immutable, curve-independent preparation for one direct-SEA schedule.  The
// target characteristic, ordered levels, and both bounded execution caps are
// retained with the prepared level contexts so a search cannot substitute
// work prepared under different schedule semantics.
class ClassicalDirectSeaContext {
public:
    ~ClassicalDirectSeaContext();
    ClassicalDirectSeaContext(const ClassicalDirectSeaContext&) = delete;
    ClassicalDirectSeaContext& operator=(const ClassicalDirectSeaContext&) =
        delete;
    ClassicalDirectSeaContext(ClassicalDirectSeaContext&&) noexcept;
    ClassicalDirectSeaContext& operator=(
        ClassicalDirectSeaContext&&) noexcept;

    const mpz_class& target_modulus() const { return target_modulus_; }
    const std::vector<std::uint64_t>& levels() const { return levels_; }
    std::uint64_t maximum_prime_candidates() const {
        return maximum_prime_candidates_;
    }
    std::uint64_t maximum_x_candidates_per_surface() const {
        return maximum_x_candidates_per_surface_;
    }
    std::size_t prepared_context_count() const;
    std::uint64_t preparation_us() const;

private:
    ClassicalDirectSeaContext(
        mpz_class target_modulus, std::vector<std::uint64_t> levels,
        std::uint64_t maximum_prime_candidates,
        std::uint64_t maximum_x_candidates_per_surface);

    friend ClassicalDirectSeaContext make_classical_direct_sea_context(
        const Field&, const std::vector<std::uint64_t>&, std::uint64_t,
        std::uint64_t);
    friend void extend_sea_with_prepared_classical_direct(
        const Curve&, WeberSeaResult&, const ClassicalDirectSeaContext&,
        std::size_t, const ClassicalDirectSeaProgress&);

    struct LevelSlot;
    const ClassicalDirectLevelContext& level_context(
        std::size_t index) const;

    mpz_class target_modulus_;
    std::vector<std::uint64_t> levels_;
    std::uint64_t maximum_prime_candidates_;
    std::uint64_t maximum_x_candidates_per_surface_;
    std::vector<std::unique_ptr<LevelSlot>> level_slots_;
};

ClassicalDirectSeaContext make_classical_direct_sea_context(
    const Field& target_field, const std::vector<std::uint64_t>& levels,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface);

// The same retained-state extension using a schedule-bound context prepared
// once for this target characteristic. Search workers may share it read-only;
// only target-j interpolation, CRT combination, and SEA consumption remain
// per curve.
void extend_sea_with_prepared_classical_direct(
    const Curve& curve, WeberSeaResult& result,
    const ClassicalDirectSeaContext& context,
    std::size_t trace_cap,
    const ClassicalDirectSeaProgress& progress = {});

}  // namespace oneshotsea
