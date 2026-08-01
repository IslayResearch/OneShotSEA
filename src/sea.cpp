#include "oneshotsea/sea.hpp"

#include "oneshotsea/atkin.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/weber.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace oneshotsea {
namespace {

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
    const WeberSeaResult& result, std::size_t cap) {
    return cap == 1U ? result.constraints.enumerate(cap)
                     : result.effective_constraints.enumerate(cap);
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
    const std::vector<WeberSeaLevelEstimate>& level_estimates) {
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

    WeberSeaResult result{
        TraceConstraints(curve.field().modulus()),
        TraceConstraints(curve.field().modulus()), {}, {},
        weber_f_lifts(curve.field(), curve.j_invariant()), std::nullopt};
    if (result.compatible_source_lifts.empty()) {
        return result;
    }

    std::vector<std::uint64_t> levels =
        available_weber_levels(directory, max_level);
    if (!level_estimates.empty()) {
        levels = expected_information_per_cost_order(levels, level_estimates);
    }

    for (const std::uint64_t ell : levels) {
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

}  // namespace oneshotsea
