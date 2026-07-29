#pragma once

#include "oneshotsea/curve.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/poly.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace oneshotsea {

// One Frobenius-stable ell-isogeny kernel and its normalized Velu codomain.
// The first implementation supports ell=3; the API is intentionally general
// so larger-level kernel reconstruction can reuse the checked eigenvalue tail.
struct ElkiesKernelResult {
    std::uint64_t ell;
    Poly kernel;
    Curve codomain;
    mpz_class neighbor_j;
    std::uint64_t eigenvalue;
    std::uint64_t trace_residue;
};

// Construct every rational ell=3 kernel, validate its degree/divisibility and
// normalized codomain against Phi_3(j,Y), then compute its Frobenius
// eigenvalue. An empty vector means the level is Atkin (no rational isogeny).
std::vector<ElkiesKernelResult> elkies_kernels_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial);

// Return the exact trace residue shared by the validated Elkies kernels, or
// nullopt for an Atkin level. Inconsistent kernel residues are a hard error.
std::optional<std::uint64_t> elkies_trace_residue_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial);

}  // namespace oneshotsea
