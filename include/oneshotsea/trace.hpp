#pragma once

#include <gmpxx.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace oneshotsea {

// Residue classes for the Frobenius trace.  Each residue is canonical in
// [0, modulus), and represents every trace in the Hasse interval congruent to it.
class TraceConstraints {
public:
    explicit TraceConstraints(mpz_class prime);

    const mpz_class& prime() const { return prime_; }
    const mpz_class& modulus() const { return modulus_; }
    const mpz_class& hasse_radius() const { return hasse_radius_; }
    const std::vector<mpz_class>& residues() const { return residues_; }

    // Intersect the current classes with t mod ell in allowed. ell must be
    // coprime to the current modulus. Empty allowed sets are valid and make the
    // constraint inconsistent.
    void refine(std::uint64_t ell, const std::vector<std::uint64_t>& allowed);

    // Add a residue claimed to be exact.  Unlike the classification-oriented
    // refine() operation, an exact residue is required to leave at least one
    // trace in the Hasse interval.  The update is transactional so a rejected
    // residue cannot corrupt the preceding constraint state.
    void refine_exact(std::uint64_t ell, std::uint64_t residue);

    mpz_class candidate_count() const;
    std::optional<std::vector<mpz_class>> enumerate(std::size_t cap) const;

private:
    mpz_class prime_;
    mpz_class modulus_;
    mpz_class hasse_radius_;
    std::vector<mpz_class> residues_;
};

// The exact trace residues compatible with whether Phi_ell(j(E),Y) has a root
// over F_p, using the discriminant t^2-4p modulo ell. This is classification
// information only; the Elkies eigenvalue stage will normally refine it to one
// exact residue.
std::vector<std::uint64_t> trace_residues_from_classification(
    std::uint64_t ell, const mpz_class& prime, bool has_rational_isogeny);

// Return the order in PGL(2,F_ell) of Frobenius with characteristic
// polynomial X^2-trace_residue*X+prime.  This is the permutation-cycle
// length on E[ell]'s cyclic subgroups in the ordinary, nonexceptional case.
std::uint64_t projective_frobenius_order(
    std::uint64_t ell, const mpz_class& prime,
    std::uint64_t trace_residue);

// Exact Atkin residue set implied by a certified projective Frobenius order.
// Only nonsquare-discriminant residues are returned.  In particular, this is
// stronger than the coarse no-rational-isogeny classification whenever the
// order is a proper source of information within ell+1.
std::vector<std::uint64_t> atkin_trace_residues_from_projective_order(
    std::uint64_t ell, const mpz_class& prime,
    std::uint64_t projective_order);

}  // namespace oneshotsea
