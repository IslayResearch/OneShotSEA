#include "oneshotsea/sea.hpp"

#include "oneshotsea/atkin.hpp"
#include "oneshotsea/cm_surface.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/poly.hpp"
#include "oneshotsea/schoof.hpp"
#include "oneshotsea/weber.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace oneshotsea {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint64_t, 11> kRareSchoofFallbackLevels = {
    3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U, 31U, 37U,
};

bool is_prime(std::uint64_t value) {
    if (value < 2U) {
        return false;
    }
    for (std::uint64_t divisor = 2U; divisor <= value / divisor; ++divisor) {
        if (value % divisor == 0U) {
            return false;
        }
    }
    return true;
}

void validate_classical_direct_levels(
    const std::vector<std::uint64_t>& levels) {
    if (!std::is_sorted(levels.begin(), levels.end()) ||
        std::adjacent_find(levels.begin(), levels.end()) != levels.end()) {
        throw std::invalid_argument(
            "classical direct levels must be strictly increasing");
    }
    for (const std::uint64_t ell : levels) {
        if (ell <= 3U || ell > std::numeric_limits<unsigned>::max() ||
            !is_prime(ell)) {
            throw std::invalid_argument(
                "classical direct schedule contains an invalid level");
        }
    }
}

bool count_fits_cap(const mpz_class& count, std::size_t cap) {
    const mpz_class cap_integer(std::to_string(cap));
    return count <= cap_integer;
}

bool completion_fits_cap(const WeberSeaResult& result, std::size_t cap) {
    return cap == 1U
        ? count_fits_cap(result.constraints.candidate_count(), cap)
        : count_fits_cap(result.effective_constraints.candidate_count(), cap);
}

std::optional<std::vector<mpz_class>> enumerate_completed(
    const WeberSeaResult& result, std::size_t cap);

std::uint64_t elapsed_us(Clock::time_point start);

struct ClassicalDirectEvaluation {
    std::uint64_t ell;
    mpz_class order_discriminant;
    std::uint64_t class_number;
    CrtSpecializationResult specialization;
};

using ClassicalDirectEvaluator =
    std::function<ClassicalDirectEvaluation(std::size_t)>;
using ClassicalDirectPreparation = std::function<void(std::size_t)>;

void extend_classical_direct_impl(
    const Curve& curve, WeberSeaResult& result,
    const std::vector<std::uint64_t>& levels, std::size_t trace_cap,
    const ClassicalDirectPreparation& prepare,
    const ClassicalDirectEvaluator& evaluate,
    const ClassicalDirectSeaProgress& progress) {
    if (curve.is_singular()) {
        throw std::invalid_argument(
            "classical direct SEA requires a nonsingular curve");
    }
    if (curve.field().modulus() != result.constraints.prime() ||
        curve.field().modulus() != result.effective_constraints.prime()) {
        throw std::invalid_argument(
            "classical direct SEA state belongs to a different field");
    }
    if (trace_cap == 0U) {
        throw std::invalid_argument(
            "classical direct SEA received a zero trace cap");
    }
    if (result.constraints.candidate_count() == 0 ||
        result.effective_constraints.candidate_count() == 0) {
        throw std::invalid_argument(
            "classical direct SEA requires nonempty retained constraints");
    }
    validate_classical_direct_levels(levels);
    if (completion_fits_cap(result, trace_cap)) {
        result.traces = enumerate_completed(result, trace_cap);
        return;
    }

    bool committed_level = false;
    for (std::size_t level_index = 0U; level_index < levels.size();
         ++level_index) {
        const std::uint64_t ell = levels[level_index];
        const mpz_class ell_integer(std::to_string(ell));
        const bool already_attempted = std::any_of(
            result.classical_direct_levels.begin(),
            result.classical_direct_levels.end(),
            [ell](const ClassicalDirectSeaLevelRecord& level) {
                return level.ell == ell;
            });
        if (result.effective_constraints.modulus() % ell_integer == 0 ||
            already_attempted) {
            continue;
        }
        if (prepare) {
            prepare(level_index);
        }
        const Clock::time_point start = Clock::now();
        ClassicalDirectEvaluation evaluation = evaluate(level_index);
        if (evaluation.ell != ell ||
            evaluation.specialization.prime_count == 0U) {
            throw std::logic_error(
                "classical direct evaluator returned inconsistent metadata");
        }
        const std::vector<ElkiesKernelResult> kernels =
            elkies_kernels_bmss_specialized_reference(
                curve, evaluation.specialization.specialization);
        std::optional<std::uint64_t> residue;
        std::optional<AtkinConstraint> atkin;
        if (!kernels.empty()) {
            residue = kernels.front().trace_residue;
            if (std::any_of(
                    kernels.begin(), kernels.end(),
                    [&residue](const ElkiesKernelResult& kernel) {
                        return kernel.trace_residue != *residue;
                    })) {
                throw std::logic_error(
                    "classical direct level produced inconsistent exact residues");
            }
        } else {
            atkin = classical_atkin_constraint_reference(
                curve, evaluation.specialization.specialization);
        }

        TraceConstraints next_exact = result.constraints;
        TraceConstraints next_effective = result.effective_constraints;
        std::vector<AtkinConstraint> next_atkin = result.atkin_constraints;
        if (residue.has_value()) {
            next_exact.refine_exact(ell, *residue);
            next_effective.refine_exact(ell, *residue);
        } else if (atkin.has_value()) {
            next_effective.refine(ell, atkin->trace_residues);
            if (next_effective.candidate_count() == 0) {
                throw std::runtime_error(
                    "classical direct Atkin evidence eliminated the Hasse interval");
            }
            next_atkin.push_back(*atkin);
        }
        ClassicalDirectSeaLevelRecord level{
            ell,
            residue.has_value(),
            residue,
            std::move(evaluation.order_discriminant),
            evaluation.class_number,
            evaluation.specialization.prime_count,
            kernels.size(),
            next_exact.modulus(),
            next_effective.modulus(),
            next_exact.candidate_count(),
            next_effective.candidate_count(),
            atkin.has_value()
                ? std::optional<std::uint64_t>(atkin->projective_order)
                : std::nullopt,
            atkin.has_value() ? atkin->trace_residues.size() : 0U,
            elapsed_us(start),
        };
        std::vector<ClassicalDirectSeaLevelRecord> next_levels =
            result.classical_direct_levels;
        next_levels.push_back(level);
        if (progress) {
            progress(level);
        }
        result.constraints = std::move(next_exact);
        result.effective_constraints = std::move(next_effective);
        result.atkin_constraints = std::move(next_atkin);
        result.classical_direct_levels = std::move(next_levels);
        result.traces.reset();
        committed_level = true;
        if (completion_fits_cap(result, trace_cap)) {
            result.traces = enumerate_completed(result, trace_cap);
            break;
        }
    }
    if (!committed_level) {
        // A prior cap-N enumeration is not a cap-1 completion.  When every
        // requested direct level is already retained, do not leak that stale
        // multi-trace result through a normal no-op extension.
        result.traces.reset();
    }
    if (!result.traces.has_value() &&
        completion_fits_cap(result, trace_cap)) {
        result.traces = enumerate_completed(result, trace_cap);
    }
}

std::optional<std::vector<mpz_class>> enumerate_completed(
    const WeberSeaResult& result, std::size_t cap) {
    return cap == 1U ? result.constraints.enumerate(cap)
                     : result.effective_constraints.enumerate(cap);
}

std::uint64_t elapsed_us(Clock::time_point start) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - start).count();
    if (elapsed < 0) {
        throw std::logic_error("steady clock moved backwards");
    }
    return static_cast<std::uint64_t>(elapsed);
}

TraceConstraints rebuilt_effective_constraints(
    const TraceConstraints& exact_constraints,
    const std::vector<AtkinConstraint>& atkin_constraints) {
    TraceConstraints effective = exact_constraints;
    for (const AtkinConstraint& atkin : atkin_constraints) {
        const mpz_class ell(std::to_string(atkin.ell));
        if (exact_constraints.modulus() % ell == 0) {
            // The exact state now owns this modulus. Check that the prior
            // certified Atkin evidence agrees before dropping it as redundant.
            for (const mpz_class& exact : exact_constraints.residues()) {
                const std::uint64_t residue =
                    mpz_fdiv_ui(exact.get_mpz_t(), atkin.ell);
                if (std::find(atkin.trace_residues.begin(),
                              atkin.trace_residues.end(), residue) ==
                    atkin.trace_residues.end()) {
                    throw std::runtime_error(
                        "exact Schoof residue contradicts certified Atkin evidence");
                }
            }
            continue;
        }
        effective.refine(atkin.ell, atkin.trace_residues);
    }
    if (effective.candidate_count() == 0) {
        throw std::runtime_error(
            "Schoof-refined effective constraints eliminated the Hasse interval");
    }
    return effective;
}

std::vector<std::uint64_t> available_weber_levels(
    const std::filesystem::path& directory, std::uint64_t max_level) {
    std::vector<std::uint64_t> levels;
    for (std::uint64_t ell = 5U; ell <= max_level; ell += 2U) {
        if (is_prime(ell) && ell % 3U != 0U &&
            std::filesystem::is_regular_file(
                directory / ("phi_" + std::to_string(ell) + ".txt"))) {
            levels.push_back(ell);
        }
        if (ell > max_level - 2U) {
            break;
        }
    }
    return levels;
}

}  // namespace

ExactTracePrior::ExactTracePrior(mpz_class prime, std::uint64_t modulus,
                                 std::uint64_t residue)
    : prime_(std::move(prime)), modulus_(modulus), residue_(residue) {
    if (modulus_ < 2U) {
        throw std::invalid_argument(
            "exact trace-prior modulus must be at least two");
    }
    if (residue_ >= modulus_) {
        throw std::invalid_argument(
            "exact trace-prior residue must be canonical");
    }
    const mpz_class modulus_integer(std::to_string(modulus_));
    mpz_class common_divisor;
    mpz_gcd(common_divisor.get_mpz_t(), prime_.get_mpz_t(),
            modulus_integer.get_mpz_t());
    if (common_divisor != 1) {
        throw std::invalid_argument(
            "exact trace-prior modulus must be coprime to the characteristic");
    }
    TraceConstraints validation(prime_);
    validation.refine_exact(modulus_, residue_);
}

std::optional<ExactTracePrior>
exact_trace_prior_from_full_rational_two_torsion(const Curve& curve) {
    if (curve.is_singular()) {
        throw std::invalid_argument(
            "2-torsion trace prior requires a nonsingular curve");
    }
    const std::vector<mpz_class> roots = linear_roots(Poly(
        curve.field(), {curve.b(), curve.a(), 0, 1}));
    if (roots.size() != 3U) {
        return std::nullopt;
    }
    constexpr std::uint64_t divisor = 4U;
    const mpz_class& prime = curve.field().modulus();
    const std::uint64_t residue =
        (mpz_fdiv_ui(prime.get_mpz_t(), divisor) + 1U) % divisor;
    return ExactTracePrior(prime, divisor, residue);
}

std::vector<std::uint64_t> expected_information_per_cost_order(
    const std::vector<std::uint64_t>& increasing_levels,
    const std::vector<WeberSeaLevelEstimate>& estimates) {
    if (!std::is_sorted(increasing_levels.begin(), increasing_levels.end()) ||
        std::adjacent_find(increasing_levels.begin(), increasing_levels.end()) !=
            increasing_levels.end()) {
        throw std::invalid_argument(
            "available Weber levels must be strictly increasing");
    }
    std::map<std::uint64_t, WeberSeaLevelEstimate> by_level;
    for (const WeberSeaLevelEstimate& estimate : estimates) {
        if (estimate.expected_cost_us == 0U) {
            throw std::invalid_argument(
                "Weber scheduling estimate has zero measured cost");
        }
        if (!std::binary_search(increasing_levels.begin(),
                                increasing_levels.end(), estimate.ell)) {
            throw std::invalid_argument(
                "Weber scheduling estimate names an unavailable level");
        }
        if (!by_level.emplace(estimate.ell, estimate).second) {
            throw std::invalid_argument(
                "Weber scheduling profile contains a duplicate level");
        }
    }
    if (by_level.size() != increasing_levels.size()) {
        throw std::invalid_argument(
            "Weber scheduling profile must cover every available level");
    }

    std::vector<std::uint64_t> ordered = increasing_levels;
    std::sort(ordered.begin(), ordered.end(), [&](std::uint64_t left,
                                                   std::uint64_t right) {
        const WeberSeaLevelEstimate& left_estimate = by_level.at(left);
        const WeberSeaLevelEstimate& right_estimate = by_level.at(right);
        const mpz_class left_product =
            mpz_class(std::to_string(left_estimate.information_units)) *
            mpz_class(std::to_string(right_estimate.expected_cost_us));
        const mpz_class right_product =
            mpz_class(std::to_string(right_estimate.information_units)) *
            mpz_class(std::to_string(left_estimate.expected_cost_us));
        return left_product == right_product ? left < right
                                             : left_product > right_product;
    });
    return ordered;
}

WeberSeaResult run_weber_sea_reference(
    const Curve& curve, const std::string& table_directory,
    std::uint64_t max_level, std::size_t trace_cap,
    const WeberSeaProgress& progress,
    std::size_t modular_root_threads, bool enable_root_orbit_reuse,
    bool enable_conjugate_eigenvalue_reuse,
    const std::vector<WeberSeaLevelEstimate>& level_estimates,
    const std::optional<ExactTracePrior>& trace_prior,
    const std::optional<mpz_class>& known_source_lift) {
    if (curve.is_singular()) {
        throw std::invalid_argument("SEA requires a nonsingular curve");
    }
    if (max_level < 5U) {
        throw std::invalid_argument("SEA max level must be at least 5");
    }
    if (max_level > std::numeric_limits<unsigned>::max()) {
        throw std::invalid_argument("SEA max level does not fit table format");
    }
    if (trace_cap == 0U) {
        throw std::invalid_argument("SEA trace cap must be positive");
    }
    const std::filesystem::path directory(table_directory);
    if (!std::filesystem::is_directory(directory)) {
        throw std::invalid_argument("Weber table directory does not exist");
    }

    TraceConstraints initial_constraints(curve.field().modulus());
    if (trace_prior.has_value()) {
        if (trace_prior->prime() != curve.field().modulus()) {
            throw std::invalid_argument(
                "exact trace prior belongs to a different field");
        }
        initial_constraints.refine_exact(
            trace_prior->modulus(), trace_prior->residue());
    }
    std::vector<mpz_class> source_lifts;
    if (known_source_lift.has_value()) {
        const mpz_class normalized =
            curve.field().normalize(*known_source_lift);
        const mpz_class curve_j = curve.j_invariant();
        if (*known_source_lift != normalized || normalized == 0 ||
            curve_j == 0 ||
            curve_j == curve.field().normalize(1728) ||
            j_from_weber_f(curve.field(), normalized) != curve_j ||
            j_derivative_from_weber_f(curve.field(), normalized) == 0) {
            throw std::invalid_argument(
                "known Weber source lift is not a normalized nonzero unramified lift of the curve j-invariant");
        }
        source_lifts.push_back(normalized);
    } else {
        source_lifts =
            weber_f_lifts(curve.field(), curve.j_invariant());
    }
    WeberSeaResult result{
        initial_constraints, initial_constraints, {}, {}, {},
        std::move(source_lifts), std::nullopt, {}};
    if (result.compatible_source_lifts.empty()) {
        return result;
    }
    if (completion_fits_cap(result, trace_cap)) {
        result.traces = enumerate_completed(result, trace_cap);
        return result;
    }

    std::vector<std::uint64_t> levels =
        available_weber_levels(directory, max_level);
    if (!level_estimates.empty()) {
        levels = expected_information_per_cost_order(levels, level_estimates);
    }

    for (const std::uint64_t ell : levels) {
        if (trace_prior.has_value() &&
            trace_prior->modulus() % ell == 0U) {
            continue;
        }
        const std::filesystem::path table =
            directory / ("phi_" + std::to_string(ell) + ".txt");
        const SparseModularPolynomial modular_polynomial =
            SparseModularPolynomial::load(
                static_cast<unsigned>(ell), table.string());
        WeberElkiesLevelResult level = compute_weber_elkies_level_reference(
            curve, modular_polynomial, &result.compatible_source_lifts,
            modular_root_threads, enable_root_orbit_reuse,
            enable_conjugate_eigenvalue_reuse);
        std::optional<std::uint64_t> residue;
        std::optional<AtkinConstraint> atkin;
        const std::filesystem::path classical_directory =
            directory.parent_path() / "j";
        if (const auto classical = load_trusted_classical_atkin_table(
                classical_directory, ell); classical.has_value()) {
            atkin = classical_atkin_constraint_reference(curve, *classical);
        }
        if (!level.kernels.empty()) {
            residue = level.kernels.front().trace_residue;
            for (const ElkiesKernelResult& kernel : level.kernels) {
                if (kernel.trace_residue != *residue) {
                    throw std::runtime_error(
                        "Weber level produced inconsistent exact residues");
                }
            }
            if (level.compatible_source_lifts.empty()) {
                throw std::logic_error(
                    "exact Weber level did not retain its source lifts");
            }
            result.compatible_source_lifts =
                std::move(level.compatible_source_lifts);
            result.constraints.refine_exact(ell, *residue);
            result.effective_constraints.refine_exact(ell, *residue);
            if (atkin.has_value()) {
                throw std::runtime_error(
                    "classical Atkin evidence contradicts an exact Weber residue");
            }
        } else if (atkin.has_value()) {
            result.effective_constraints.refine(
                ell, atkin->trace_residues);
            if (result.effective_constraints.candidate_count() == 0) {
                throw std::runtime_error(
                    "certified Atkin constraint eliminated the Hasse interval");
            }
            result.atkin_constraints.push_back(*atkin);
        }
        WeberSeaLevelRecord record{
            ell,
            residue.has_value(),
            residue,
            result.constraints.modulus(),
            result.effective_constraints.modulus(),
            result.constraints.candidate_count(),
            result.effective_constraints.candidate_count(),
            atkin.has_value()
                ? std::optional<std::uint64_t>(atkin->projective_order)
                : std::nullopt,
            atkin.has_value() ? atkin->trace_residues.size() : 0U,
            result.compatible_source_lifts.size(),
            level.timings,
        };
        result.levels.push_back(record);
        if (progress) {
            progress(result.levels.back());
        }
        if (completion_fits_cap(result, trace_cap)) {
            result.traces = enumerate_completed(result, trace_cap);
            break;
        }
    }
    if (!result.traces.has_value() && completion_fits_cap(result, trace_cap)) {
        result.traces = enumerate_completed(result, trace_cap);
    }
    return result;
}

void extend_weber_sea_with_schoof_fallback(
    const Curve& curve, WeberSeaResult& result, std::size_t trace_cap,
    const SchoofFallbackProgress& progress) {
    if (curve.is_singular()) {
        throw std::invalid_argument(
            "Schoof fallback requires a nonsingular curve");
    }
    if (curve.field().modulus() != result.constraints.prime() ||
        curve.field().modulus() != result.effective_constraints.prime()) {
        throw std::invalid_argument(
            "Schoof fallback state belongs to a different field");
    }
    if (trace_cap == 0U) {
        throw std::invalid_argument("Schoof fallback trace cap must be positive");
    }
    if (result.constraints.candidate_count() == 0 ||
        result.effective_constraints.candidate_count() == 0) {
        throw std::invalid_argument(
            "Schoof fallback requires nonempty retained constraints");
    }

    if (completion_fits_cap(result, trace_cap)) {
        result.traces = enumerate_completed(result, trace_cap);
        return;
    }
    bool committed_level = false;
    for (const std::uint64_t ell : kRareSchoofFallbackLevels) {
        const mpz_class ell_integer(std::to_string(ell));
        if (result.constraints.modulus() % ell_integer == 0) {
            continue;
        }
        const Clock::time_point start = Clock::now();
        const std::uint64_t residue = schoof_trace_mod_ell(curve, ell);
        TraceConstraints next_exact = result.constraints;
        next_exact.refine_exact(ell, residue);
        TraceConstraints next_effective = rebuilt_effective_constraints(
            next_exact, result.atkin_constraints);
        SchoofFallbackLevelRecord level{
            ell,
            residue,
            next_exact.modulus(),
            next_effective.modulus(),
            next_exact.candidate_count(),
            next_effective.candidate_count(),
            elapsed_us(start),
        };
        std::vector<SchoofFallbackLevelRecord> next_levels =
            result.schoof_fallback_levels;
        next_levels.push_back(level);
        if (progress) {
            progress(level);
        }
        result.constraints = std::move(next_exact);
        result.effective_constraints = std::move(next_effective);
        result.schoof_fallback_levels = std::move(next_levels);
        result.traces.reset();
        committed_level = true;
        if (completion_fits_cap(result, trace_cap)) {
            result.traces = enumerate_completed(result, trace_cap);
            break;
        }
    }
    if (!committed_level) {
        // A cap-1 continuation may revisit a state after every fixed modulus
        // is already exact. Its earlier cap-N enumeration is not a unique
        // trace result and must not survive a normal exhausted return.
        result.traces.reset();
    }
    if (!result.traces.has_value() && completion_fits_cap(result, trace_cap)) {
        result.traces = enumerate_completed(result, trace_cap);
    }
}

void extend_sea_with_classical_direct(
    const Curve& curve, WeberSeaResult& result,
    const std::vector<std::uint64_t>& levels, std::size_t trace_cap,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface,
    const ClassicalDirectSeaProgress& progress) {
    if (maximum_prime_candidates == 0U ||
        maximum_x_candidates_per_surface == 0U) {
        throw std::invalid_argument(
            "classical direct SEA received a zero execution cap");
    }
    extend_classical_direct_impl(
        curve, result, levels, trace_cap, {},
        [&](std::size_t level_index) {
            const std::uint64_t ell = levels[level_index];
            const SutherlandSuitableOrder order =
                derive_three_power_suitable_order(
                    static_cast<unsigned>(ell));
            CrtSpecializationResult specialization =
                reconstruct_classical_specialization_from_cm(
                    order, curve.field(), curve.j_invariant(),
                    maximum_prime_candidates,
                    maximum_x_candidates_per_surface);
            return ClassicalDirectEvaluation{
                ell, order.discriminant(), order.class_number(),
                std::move(specialization)};
        },
        progress);
}

struct ClassicalDirectSeaContext::LevelSlot {
    std::once_flag once;
    std::unique_ptr<ClassicalDirectLevelContext> context;
    std::exception_ptr failure;
    std::atomic<bool> prepared{false};
    std::atomic<std::uint64_t> preparation_us{0U};
};

ClassicalDirectSeaContext::~ClassicalDirectSeaContext() = default;
ClassicalDirectSeaContext::ClassicalDirectSeaContext(
    ClassicalDirectSeaContext&&) noexcept = default;
ClassicalDirectSeaContext& ClassicalDirectSeaContext::operator=(
    ClassicalDirectSeaContext&&) noexcept = default;

ClassicalDirectSeaContext::ClassicalDirectSeaContext(
    mpz_class target_modulus, std::vector<std::uint64_t> levels,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface,
    std::size_t preparation_threads)
    : target_modulus_(std::move(target_modulus)),
      levels_(std::move(levels)),
      maximum_prime_candidates_(maximum_prime_candidates),
      maximum_x_candidates_per_surface_(
          maximum_x_candidates_per_surface),
      preparation_threads_(preparation_threads) {
    level_slots_.reserve(levels_.size());
    for (std::size_t index = 0U; index < levels_.size(); ++index) {
        level_slots_.push_back(std::make_unique<LevelSlot>());
    }
}

ClassicalDirectSeaContext make_classical_direct_sea_context(
    const Field& target_field, const std::vector<std::uint64_t>& levels,
    std::uint64_t maximum_prime_candidates,
    std::uint64_t maximum_x_candidates_per_surface,
    std::size_t preparation_threads) {
    if (maximum_prime_candidates == 0U ||
        maximum_x_candidates_per_surface == 0U) {
        throw std::invalid_argument(
            "classical direct SEA context received a zero execution cap");
    }
    validate_classical_direct_levels(levels);
    return ClassicalDirectSeaContext(
        target_field.modulus(), levels, maximum_prime_candidates,
        maximum_x_candidates_per_surface, preparation_threads);
}

const ClassicalDirectLevelContext&
ClassicalDirectSeaContext::level_context(std::size_t index) const {
    if (index >= level_slots_.size()) {
        throw std::out_of_range(
            "classical direct SEA level context index is out of range");
    }
    LevelSlot& slot = *level_slots_[index];
    std::call_once(slot.once, [&] {
        if (slot.context) {
            return;
        }
        const Clock::time_point start = Clock::now();
        try {
            const Field target_field(target_modulus_);
            const SutherlandSuitableOrder order =
                derive_three_power_suitable_order(
                    static_cast<unsigned>(levels_[index]));
            std::unique_ptr<ClassicalDirectLevelContext> prepared =
                std::make_unique<ClassicalDirectLevelContext>(
                    prepare_classical_direct_level_context(
                        order, target_field,
                        maximum_prime_candidates_,
                        maximum_x_candidates_per_surface_,
                        preparation_threads_));
            const std::uint64_t duration = elapsed_us(start);
            slot.context = std::move(prepared);
            slot.preparation_us.store(duration,
                                      std::memory_order_release);
            slot.prepared.store(true, std::memory_order_release);
        } catch (...) {
            slot.failure = std::current_exception();
        }
    });
    if (slot.failure) {
        std::rethrow_exception(slot.failure);
    }
    if (!slot.context) {
        throw std::logic_error(
            "classical direct SEA level preparation produced no context");
    }
    return *slot.context;
}

void ClassicalDirectSeaContext::install_cached_contexts(
    std::vector<std::unique_ptr<ClassicalDirectLevelContext>> contexts,
    std::uint64_t load_us) {
    if (contexts.size() != level_slots_.size() || loaded_from_cache_) {
        throw std::logic_error(
            "classical direct cache does not match an empty schedule context");
    }
    for (std::size_t index = 0U; index < contexts.size(); ++index) {
        if (!contexts[index] || level_slots_[index]->context ||
            level_slots_[index]->prepared.load(std::memory_order_acquire)) {
            throw std::logic_error(
                "classical direct cache contains a missing or duplicate level context");
        }
        level_slots_[index]->context = std::move(contexts[index]);
        level_slots_[index]->prepared.store(true, std::memory_order_release);
    }
    cache_load_us_ = load_us;
    loaded_from_cache_ = true;
}

std::size_t ClassicalDirectSeaContext::prepared_context_count() const {
    return static_cast<std::size_t>(std::count_if(
        level_slots_.begin(), level_slots_.end(),
        [](const std::unique_ptr<LevelSlot>& slot) {
            return slot->prepared.load(std::memory_order_acquire);
        }));
}

std::uint64_t ClassicalDirectSeaContext::preparation_us() const {
    std::uint64_t total = 0U;
    for (const std::unique_ptr<LevelSlot>& slot : level_slots_) {
        const std::uint64_t duration =
            slot->preparation_us.load(std::memory_order_acquire);
        if (duration >
            std::numeric_limits<std::uint64_t>::max() - total) {
            throw std::overflow_error(
                "classical direct SEA preparation timing overflow");
        }
        total += duration;
    }
    return total;
}

std::size_t ClassicalDirectSeaContext::interpolation_coefficient_count()
    const {
    std::size_t total = 0U;
    for (const std::unique_ptr<LevelSlot>& slot : level_slots_) {
        if (!slot->prepared.load(std::memory_order_acquire)) {
            continue;
        }
        if (!slot->context) {
            throw std::logic_error(
                "prepared classical direct level has no compact context");
        }
        const std::size_t count =
            slot->context->interpolation_coefficient_count();
        if (count > std::numeric_limits<std::size_t>::max() - total) {
            throw std::overflow_error(
                "classical direct interpolation count overflows size_t");
        }
        total += count;
    }
    return total;
}

std::size_t ClassicalDirectSeaContext::interpolation_storage_bytes() const {
    const std::size_t count = interpolation_coefficient_count();
    if (count > std::numeric_limits<std::size_t>::max() /
                    sizeof(std::uint64_t)) {
        throw std::overflow_error(
            "classical direct interpolation storage overflows size_t");
    }
    return count * sizeof(std::uint64_t);
}

void extend_sea_with_prepared_classical_direct(
    const Curve& curve, WeberSeaResult& result,
    const ClassicalDirectSeaContext& context,
    std::size_t trace_cap,
    const ClassicalDirectSeaProgress& progress) {
    if (context.target_modulus_ != curve.field().modulus() ||
        context.levels_.size() != context.level_slots_.size()) {
        throw std::invalid_argument(
            "prepared classical direct SEA context belongs to another field or is incomplete");
    }
    extend_classical_direct_impl(
        curve, result, context.levels_, trace_cap,
        [&](std::size_t level_index) {
            static_cast<void>(context.level_context(level_index));
        },
        [&](std::size_t level_index) {
            const ClassicalDirectLevelContext& level_context =
                context.level_context(level_index);
            CrtSpecializationResult specialization =
                reconstruct_classical_specialization_from_prepared_context(
                    level_context, curve.field(), curve.j_invariant());
            if (specialization.prime_count !=
                level_context.auxiliary_prime_count()) {
                throw std::logic_error(
                    "prepared classical direct SEA lost auxiliary-prime coverage");
            }
            return ClassicalDirectEvaluation{
                level_context.level(), level_context.order_discriminant(),
                level_context.class_number(), std::move(specialization)};
        },
        progress);
}

}  // namespace oneshotsea
