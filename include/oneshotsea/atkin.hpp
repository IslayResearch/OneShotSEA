#pragma once

#include "oneshotsea/curve.hpp"
#include "oneshotsea/modpoly.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace oneshotsea {

struct AtkinConstraint {
    std::uint64_t ell;
    std::uint64_t projective_order;
    std::vector<std::uint64_t> trace_residues;
};

// Load one of the independently generated and pinned classical-j tables that
// is trusted for production Atkin evidence. Missing tables disable this small
// optional path; a present table with the wrong digest is a hard error.
std::optional<SparseModularPolynomial> load_trusted_classical_atkin_table(
    const std::filesystem::path& classical_table_directory,
    std::uint64_t ell);

// Return a constraint only when a square-free classical specialization has no
// rational root and every irreducible factor has one common degree r|ell+1.
// For a nonexceptional Atkin level this degree is the Frobenius order in PGL.
// Rational roots, repeated roots, mixed degrees, and exceptional invariants
// fail closed as unavailable. The caller is responsible for authenticating
// the modular polynomial; production uses the loader above.
std::optional<AtkinConstraint> classical_atkin_constraint_reference(
    const Curve& curve,
    const SparseModularPolynomial& classical_modular_polynomial);

// The same fail-closed factor-degree certification using an authenticated
// directly reconstructed classical-j specialization.
std::optional<AtkinConstraint> classical_atkin_constraint_reference(
    const Curve& curve,
    const ModularPolynomialSpecialization& classical_specialization);

// The same direct classifier using complete root evidence already produced by
// the Elkies attempt. On a no-root level, its retained X^p image seeds the
// factor-degree certificate and avoids a duplicate exponentiation.
std::optional<AtkinConstraint> classical_atkin_constraint_reference(
    const Curve& curve,
    const ModularPolynomialSpecialization& classical_specialization,
    const CertifiedLinearRoots& roots);

}  // namespace oneshotsea
