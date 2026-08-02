#include "oneshotsea/atkin.hpp"

#include "oneshotsea/factor.hpp"
#include "oneshotsea/integrity.hpp"
#include "oneshotsea/trace.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>

namespace oneshotsea {
namespace {

const std::map<std::uint64_t, std::string>& trusted_table_digests() {
    static const std::map<std::uint64_t, std::string> digests = {
        {5U, "b18e44299e31a71fb8e565bef48454a79e27cfba1e98bda95d6ec03f4555cca2"},
        {7U, "52e1d4c1d1bd85c91083d5265e349a3623b27ba31f39e35c41c0dcd2a8002c23"},
    };
    return digests;
}

}  // namespace

std::optional<SparseModularPolynomial> load_trusted_classical_atkin_table(
    const std::filesystem::path& classical_table_directory,
    std::uint64_t ell) {
    const auto expected = trusted_table_digests().find(ell);
    if (expected == trusted_table_digests().end()) {
        return std::nullopt;
    }
    const std::filesystem::path path =
        classical_table_directory / ("phi_" + std::to_string(ell) + ".txt");
    if (!std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }
    if (sha256_file(path) != expected->second) {
        throw std::runtime_error(
            "trusted classical Atkin table digest mismatch at level " +
            std::to_string(ell));
    }
    return SparseModularPolynomial::load(
        static_cast<unsigned>(ell), path.string());
}

std::optional<AtkinConstraint> classical_atkin_constraint_reference(
    const Curve& curve,
    const SparseModularPolynomial& classical_modular_polynomial) {
    if (curve.is_singular()) {
        throw std::invalid_argument(
            "classical Atkin classification requires a nonsingular curve");
    }
    const std::uint64_t ell = classical_modular_polynomial.level();
    const mpz_class j = curve.j_invariant();
    if (j == 0 || j == curve.field().normalize(1728)) {
        return std::nullopt;
    }
    const Poly specialized =
        classical_modular_polynomial.evaluate_x(curve.field(), j);
    if (specialized.degree() != static_cast<int>(ell + 1U) ||
        specialized.leading_coefficient() != 1 ||
        !gcd(specialized, specialized.derivative()).is_one()) {
        return std::nullopt;
    }
    const std::vector<IrreducibleFactor> factors =
        factor_polynomial(specialized);
    if (factors.empty()) {
        return std::nullopt;
    }
    const int common_degree = factors.front().polynomial.degree();
    if (common_degree <= 1 ||
        std::any_of(factors.begin(), factors.end(),
                    [common_degree](const IrreducibleFactor& factor) {
                        return factor.multiplicity != 1UL ||
                               factor.polynomial.degree() != common_degree;
                    })) {
        return std::nullopt;
    }
    const std::uint64_t projective_order =
        static_cast<std::uint64_t>(common_degree);
    if ((ell + 1U) % projective_order != 0U) {
        return std::nullopt;
    }
    std::vector<std::uint64_t> residues =
        atkin_trace_residues_from_projective_order(
            ell, curve.field().modulus(), projective_order);
    if (residues.empty()) {
        return std::nullopt;
    }
    return AtkinConstraint{ell, projective_order, std::move(residues)};
}

}  // namespace oneshotsea
