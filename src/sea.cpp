#include "oneshotsea/sea.hpp"

#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/weber.hpp"

#include <filesystem>
#include <limits>
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

}  // namespace

WeberSeaResult run_weber_sea_reference(
    const Curve& curve, const std::string& table_directory,
    std::uint64_t max_level, std::size_t trace_cap,
    const WeberSeaProgress& progress) {
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
        TraceConstraints(curve.field().modulus()), {},
        weber_f_lifts(curve.field(), curve.j_invariant()), std::nullopt};
    if (result.compatible_source_lifts.empty()) {
        return result;
    }

    for (std::uint64_t ell = 5U; ell <= max_level; ell += 2U) {
        if (!is_prime(ell) || ell % 3U == 0U) {
            continue;
        }
        const std::filesystem::path table =
            directory / ("phi_" + std::to_string(ell) + ".txt");
        if (!std::filesystem::is_regular_file(table)) {
            continue;
        }
        const SparseModularPolynomial modular_polynomial =
            SparseModularPolynomial::load(
                static_cast<unsigned>(ell), table.string());
        WeberElkiesLevelResult level = compute_weber_elkies_level_reference(
            curve, modular_polynomial, &result.compatible_source_lifts);
        std::optional<std::uint64_t> residue;
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
        }
        WeberSeaLevelRecord record{
            ell,
            residue.has_value(),
            residue,
            result.constraints.modulus(),
            result.constraints.candidate_count(),
            result.compatible_source_lifts.size(),
            level.timings,
        };
        result.levels.push_back(record);
        if (progress) {
            progress(result.levels.back());
        }
        if (count_fits_cap(record.trace_candidate_count, trace_cap)) {
            result.traces = result.constraints.enumerate(trace_cap);
            break;
        }
        if (ell > max_level - 2U) {
            break;
        }
    }
    if (!result.traces.has_value() &&
        count_fits_cap(result.constraints.candidate_count(), trace_cap)) {
        result.traces = result.constraints.enumerate(trace_cap);
    }
    return result;
}

}  // namespace oneshotsea
