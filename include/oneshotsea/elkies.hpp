#pragma once

#include "oneshotsea/curve.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/poly.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace oneshotsea {

// One Frobenius-stable ell-isogeny kernel and its normalized Velu codomain.
struct ElkiesKernelResult {
    std::uint64_t ell;
    Poly kernel;
    Curve codomain;
    mpz_class neighbor_j;
    std::uint64_t eigenvalue;
    std::uint64_t trace_residue;
};

struct ElkiesStageTimings {
    std::uint64_t source_lifts_us = 0;
    std::uint64_t modular_roots_us = 0;
    std::uint64_t normalized_codomain_us = 0;
    std::uint64_t bmss_us = 0;
    std::uint64_t eigenvalue_us = 0;
    std::uint64_t lift_pairs = 0;
    std::uint64_t distinct_codomains = 0;
    std::uint64_t codomain_cache_hits = 0;
    std::uint64_t eigenvalue_attempts = 0;
};

struct WeberElkiesLevelResult {
    std::vector<ElkiesKernelResult> kernels;
    // Source lifts that produced at least one fully validated kernel. Positive
    // evidence may safely restrict later levels; an empty level must not.
    std::vector<mpz_class> compatible_source_lifts;
    ElkiesStageTimings timings;
};

// Compute the normalized short-Weierstrass Vélu codomain from the monic kernel
// polynomial using its first three power sums. The kernel is independently
// checked for degree, square-freeness, and divisibility by psi_ell.
Curve velu_codomain_reference(const Curve& curve, const Poly& kernel,
                              std::uint64_t ell);

std::uint64_t trace_residue_from_eigenvalue(const mpz_class& prime,
                                            std::uint64_t ell,
                                            std::uint64_t eigenvalue);

// Slow kernel-first path: factor psi_ell, assemble degree-(ell-1)/2 divisors,
// retain cyclic Frobenius eigenlines, and compute normalized codomains and
// exact residues. This is an independent reference, not the production BMSS
// reconstruction algorithm. An empty vector means the level is Atkin.
std::vector<ElkiesKernelResult> elkies_kernels_division_reference(
    const Curve& curve, std::uint64_t ell);

std::optional<std::uint64_t> elkies_trace_residue_division_reference(
    const Curve& curve, std::uint64_t ell);

// Additionally validate every recovered codomain against Phi_ell(j,Y).
std::vector<ElkiesKernelResult> elkies_kernels_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial);

// Return the exact trace residue shared by the validated Elkies kernels, or
// nullopt for an Atkin level. Inconsistent kernel residues are a hard error.
std::optional<std::uint64_t> elkies_trace_residue_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial);

// Reconstruct kernels from simple modular neighbors with the BMSS power-series
// path, then recover and validate exact Frobenius eigenvalues. Exceptional or
// repeated roots are skipped; unexpected reconstruction failures are fatal.
std::vector<ElkiesKernelResult> elkies_kernels_bmss_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial);

std::optional<std::uint64_t> elkies_trace_residue_bmss_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial);

// Exhaust every rational Weber-f lift, as required for p=1 mod 12, and retain
// only candidates that pass normalized BMSS and Frobenius validation.
std::vector<ElkiesKernelResult> elkies_kernels_weber_bmss_reference(
    const Curve& curve,
    const SparseModularPolynomial& weber_modular_polynomial,
    ElkiesStageTimings* timings = nullptr);

WeberElkiesLevelResult compute_weber_elkies_level_reference(
    const Curve& curve,
    const SparseModularPolynomial& weber_modular_polynomial,
    const std::vector<mpz_class>* restricted_source_lifts = nullptr);

std::optional<std::uint64_t> elkies_trace_residue_weber_bmss_reference(
    const Curve& curve,
    const SparseModularPolynomial& weber_modular_polynomial);

}  // namespace oneshotsea
