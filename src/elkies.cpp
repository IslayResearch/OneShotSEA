#include "oneshotsea/elkies.hpp"

#include <algorithm>
#include <stdexcept>

namespace oneshotsea {
namespace {

Poly division_polynomial_3(const Curve& curve) {
    const Field& field = curve.field();
    const mpz_class a = curve.a();
    const mpz_class b = curve.b();
    return Poly(field, {-a * a, 12 * b, 6 * a, 0, 3}).monic();
}

Curve velu_codomain_3(const Curve& curve, const mpz_class& kernel_x) {
    const Field& field = curve.field();
    const mpz_class x2 = field.square(kernel_x);
    const mpz_class x3 = field.mul(x2, kernel_x);
    const mpz_class t = field.add(field.mul(6, x2), field.mul(2, curve.a()));
    const mpz_class w = field.add(
        field.add(field.mul(10, x3), field.mul(6, field.mul(curve.a(), kernel_x))),
        field.mul(4, curve.b()));
    return Curve(field, field.sub(curve.a(), field.mul(5, t)),
                 field.sub(curve.b(), field.mul(7, w)));
}

std::uint64_t inverse_mod_small(std::uint64_t value, std::uint64_t modulus) {
    for (std::uint64_t candidate = 1; candidate < modulus; ++candidate) {
        if ((value * candidate) % modulus == 1) {
            return candidate;
        }
    }
    throw std::logic_error("Frobenius eigenvalue is not invertible modulo ell");
}

}  // namespace

std::vector<ElkiesKernelResult> elkies_kernels_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial) {
    constexpr std::uint64_t ell = 3;
    if (modular_polynomial.level() != ell) {
        throw std::invalid_argument("reference Elkies kernel path currently requires level 3");
    }
    if (curve.is_singular()) {
        throw std::invalid_argument("Elkies residue requires a nonsingular curve");
    }
    const Field& field = curve.field();
    if (mpz_cmp_ui(field.modulus().get_mpz_t(), ell) == 0) {
        throw std::invalid_argument("ell must differ from p");
    }
    if (mpz_probab_prime_p(field.modulus().get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("Elkies residue requires probable-prime p");
    }

    const Poly psi3 = division_polynomial_3(curve);
    const std::vector<mpz_class> kernel_x_values = linear_roots(psi3);
    const Poly specialized = modular_polynomial.evaluate_x(field, curve.j_invariant());
    const std::vector<mpz_class> modular_neighbors = linear_roots(specialized);

    std::vector<ElkiesKernelResult> results;
    results.reserve(kernel_x_values.size());
    const Poly x = Poly::x(field);
    for (const mpz_class& kernel_x : kernel_x_values) {
        const Poly kernel = sub(x, Poly::constant(field, kernel_x)).monic();
        if (kernel.degree() != 1) {
            throw std::logic_error("ell=3 kernel has the wrong degree");
        }
        const auto [quotient, remainder] = divmod(psi3, kernel);
        static_cast<void>(quotient);
        if (!remainder.is_zero()) {
            throw std::logic_error("Elkies kernel does not divide psi_3");
        }

        Curve codomain = velu_codomain_3(curve, kernel_x);
        if (codomain.is_singular()) {
            throw std::runtime_error("Velu codomain is singular");
        }
        const mpz_class neighbor_j = codomain.j_invariant();
        if (!std::binary_search(modular_neighbors.begin(), modular_neighbors.end(),
                                neighbor_j) ||
            specialized.evaluate(neighbor_j) != 0) {
            throw std::runtime_error("Velu codomain is not a Phi_3 neighbor");
        }

        const mpz_class rhs = field.add(
            field.add(field.mul(field.square(kernel_x), kernel_x),
                      field.mul(curve.a(), kernel_x)),
            curve.b());
        const int character = field.legendre(rhs);
        if (character == 0) {
            throw std::runtime_error("odd-order kernel unexpectedly contains 2-torsion");
        }
        const std::uint64_t eigenvalue = character > 0 ? 1 : ell - 1;
        const std::uint64_t p_mod_ell = mpz_fdiv_ui(field.modulus().get_mpz_t(), ell);
        const std::uint64_t inverse = inverse_mod_small(eigenvalue, ell);
        const std::uint64_t trace_residue =
            (eigenvalue + (p_mod_ell * inverse) % ell) % ell;
        results.push_back({ell, kernel, std::move(codomain), neighbor_j,
                           eigenvalue, trace_residue});
    }
    return results;
}

std::optional<std::uint64_t> elkies_trace_residue_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial) {
    const std::vector<ElkiesKernelResult> kernels =
        elkies_kernels_reference(curve, modular_polynomial);
    if (kernels.empty()) {
        return std::nullopt;
    }
    const std::uint64_t residue = kernels.front().trace_residue;
    for (const ElkiesKernelResult& kernel : kernels) {
        if (kernel.trace_residue != residue) {
            throw std::runtime_error("Elkies kernels imply inconsistent trace residues");
        }
    }
    return residue;
}

}  // namespace oneshotsea
