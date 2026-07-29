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

}  // namespace oneshotsea
