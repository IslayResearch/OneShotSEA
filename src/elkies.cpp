#include "oneshotsea/elkies.hpp"
#include "oneshotsea/factor.hpp"
#include "oneshotsea/isogeny.hpp"
#include "oneshotsea/torsion.hpp"
#include "oneshotsea/weber.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <map>
#include <stdexcept>

namespace oneshotsea {
namespace {

constexpr std::size_t kMaxKernelAssemblyNodes = 1000000;

std::uint64_t inverse_mod_small(std::uint64_t value, std::uint64_t modulus) {
    for (std::uint64_t candidate = 1; candidate < modulus; ++candidate) {
        if ((static_cast<unsigned __int128>(value) * candidate) % modulus == 1) {
            return candidate;
        }
    }
    throw std::logic_error("Frobenius eigenvalue is not invertible modulo ell");
}

void assemble_kernel_divisors(const std::vector<Poly>& factors,
                              std::size_t first, int remaining_degree,
                              const Poly& product, std::size_t& visited,
                              std::vector<Poly>& output) {
    if (++visited > kMaxKernelAssemblyNodes) {
        throw std::runtime_error("reference kernel assembly node limit reached");
    }
    if (remaining_degree == 0) {
        output.push_back(product.monic());
        return;
    }
    for (std::size_t index = first; index < factors.size(); ++index) {
        const int degree = factors[index].degree();
        if (degree <= remaining_degree) {
            assemble_kernel_divisors(
                factors, index + 1U, remaining_degree - degree,
                mul(product, factors[index]), visited, output);
        }
    }
}

std::optional<std::uint64_t> common_trace_residue(
    const std::vector<ElkiesKernelResult>& kernels) {
    if (kernels.empty()) {
        return std::nullopt;
    }
    const std::uint64_t residue = kernels.front().trace_residue;
    for (const ElkiesKernelResult& kernel : kernels) {
        if (kernel.trace_residue != residue) {
            throw std::runtime_error(
                "Elkies kernels imply inconsistent trace residues");
        }
    }
    return residue;
}

}  // namespace

Curve velu_codomain_reference(const Curve& curve, const Poly& kernel,
                              std::uint64_t ell) {
    const Poly psi_ell = division_polynomial_reference(curve, ell);
    if (kernel.field().modulus() != curve.field().modulus()) {
        throw std::invalid_argument("kernel and curve use different fields");
    }
    const std::uint64_t expected_degree = (ell - 1U) / 2U;
    if (kernel.degree() != static_cast<int>(expected_degree) ||
        kernel.leading_coefficient() != 1) {
        throw std::invalid_argument("kernel has the wrong degree or normalization");
    }
    if (gcd(kernel, kernel.derivative()).degree() != 0) {
        throw std::invalid_argument("kernel is not square-free");
    }
    if (!divmod(psi_ell, kernel).second.is_zero()) {
        throw std::invalid_argument("kernel does not divide psi_ell");
    }

    const Field& field = curve.field();
    const std::size_t degree = static_cast<std::size_t>(kernel.degree());
    const mpz_class e1 = field.neg(kernel.coefficient(degree - 1U));
    const mpz_class e2 = degree >= 2U ? kernel.coefficient(degree - 2U) : 0;
    const mpz_class e3 =
        degree >= 3U ? field.neg(kernel.coefficient(degree - 3U)) : 0;
    const mpz_class power_sum_1 = e1;
    const mpz_class power_sum_2 =
        field.sub(field.square(e1), field.mul(2, e2));
    const mpz_class power_sum_3 = field.add(
        field.sub(field.mul(e1, power_sum_2), field.mul(e2, power_sum_1)),
        field.mul(3, e3));
    const mpz_class twice_degree(
        static_cast<unsigned long>(2U * expected_degree));
    const mpz_class four_times_degree(
        static_cast<unsigned long>(4U * expected_degree));
    const mpz_class t = field.add(field.mul(6, power_sum_2),
                                  field.mul(twice_degree, curve.a()));
    const mpz_class w = field.add(
        field.add(field.mul(10, power_sum_3),
                  field.mul(6, field.mul(curve.a(), power_sum_1))),
        field.mul(four_times_degree, curve.b()));
    return Curve(field, field.sub(curve.a(), field.mul(5, t)),
                 field.sub(curve.b(), field.mul(7, w)));
}

std::uint64_t trace_residue_from_eigenvalue(const mpz_class& prime,
                                            std::uint64_t ell,
                                            std::uint64_t eigenvalue) {
    const mpz_class ell_integer(std::to_string(ell));
    if (ell < 3 || (ell & 1U) == 0U ||
        mpz_probab_prime_p(ell_integer.get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("ell must be an odd prime");
    }
    if (prime < 2 || mpz_probab_prime_p(prime.get_mpz_t(), 25) == 0) {
        throw std::invalid_argument("characteristic must be prime");
    }
    if (prime == ell_integer) {
        throw std::invalid_argument("ell must differ from the characteristic");
    }
    if (eigenvalue == 0 || eigenvalue >= ell) {
        throw std::invalid_argument("invalid Frobenius eigenvalue modulus");
    }
    const std::uint64_t inverse = inverse_mod_small(eigenvalue, ell);
    const std::uint64_t p_mod_ell = mpz_fdiv_ui(prime.get_mpz_t(), ell);
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(eigenvalue) +
         static_cast<unsigned __int128>(p_mod_ell) * inverse) % ell);
}

std::vector<ElkiesKernelResult> elkies_kernels_division_reference(
    const Curve& curve, std::uint64_t ell) {
    const Poly psi_ell = division_polynomial_reference(curve, ell);
    const std::vector<IrreducibleFactor> factorization =
        factor_polynomial(psi_ell);
    std::vector<Poly> factors;
    factors.reserve(factorization.size());
    for (const IrreducibleFactor& factor : factorization) {
        if (factor.multiplicity != 1) {
            throw std::runtime_error("psi_ell is not square-free");
        }
        factors.push_back(factor.polynomial);
    }

    std::vector<Poly> candidates;
    std::size_t visited = 0;
    assemble_kernel_divisors(
        factors, 0, static_cast<int>((ell - 1U) / 2U),
        Poly::constant(curve.field(), 1), visited, candidates);
    std::sort(candidates.begin(), candidates.end(),
              [](const Poly& lhs, const Poly& rhs) {
                  return std::lexicographical_compare(
                      lhs.coefficients().begin(), lhs.coefficients().end(),
                      rhs.coefficients().begin(), rhs.coefficients().end());
              });

    std::vector<ElkiesKernelResult> results;
    for (const Poly& candidate : candidates) {
        const auto eigenvalue =
            try_frobenius_eigenvalue_reference(curve, candidate, ell);
        if (!eigenvalue.has_value()) {
            continue;
        }
        Curve codomain = velu_codomain_reference(curve, candidate, ell);
        if (codomain.is_singular()) {
            throw std::runtime_error("Velu codomain is singular");
        }
        const mpz_class neighbor_j = codomain.j_invariant();
        const std::uint64_t trace_residue = trace_residue_from_eigenvalue(
            curve.field().modulus(), ell, *eigenvalue);
        results.push_back({ell, candidate, std::move(codomain), neighbor_j,
                           *eigenvalue, trace_residue});
    }
    static_cast<void>(common_trace_residue(results));
    return results;
}

std::optional<std::uint64_t> elkies_trace_residue_division_reference(
    const Curve& curve, std::uint64_t ell) {
    return common_trace_residue(
        elkies_kernels_division_reference(curve, ell));
}

std::vector<ElkiesKernelResult> elkies_kernels_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial) {
    const std::uint64_t ell = modular_polynomial.level();
    std::vector<ElkiesKernelResult> results =
        elkies_kernels_division_reference(curve, ell);
    const Poly specialized = modular_polynomial.evaluate_x(
        curve.field(), curve.j_invariant());
    const std::vector<mpz_class> modular_neighbors = linear_roots(specialized);
    for (const ElkiesKernelResult& result : results) {
        if (!std::binary_search(modular_neighbors.begin(), modular_neighbors.end(),
                                result.neighbor_j) ||
            specialized.evaluate(result.neighbor_j) != 0) {
            throw std::runtime_error("Velu codomain is not a modular neighbor");
        }
        if (ell == 3) {
            const mpz_class kernel_x =
                curve.field().neg(result.kernel.coefficient(0));
            const mpz_class rhs = curve.field().add(
                curve.field().add(
                    curve.field().mul(curve.field().square(kernel_x), kernel_x),
                    curve.field().mul(curve.a(), kernel_x)),
                curve.b());
            const int character = curve.field().legendre(rhs);
            const std::uint64_t sign_eigenvalue = character > 0 ? 1 : ell - 1;
            if (character == 0 || result.eigenvalue != sign_eigenvalue) {
                throw std::logic_error(
                    "quotient and sign Frobenius eigenvalues disagree");
            }
        }
    }
    return results;
}

std::optional<std::uint64_t> elkies_trace_residue_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial) {
    return common_trace_residue(
        elkies_kernels_reference(curve, modular_polynomial));
}

std::vector<ElkiesKernelResult> elkies_kernels_bmss_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial) {
    const std::uint64_t ell = modular_polynomial.level();
    const Poly specialized = modular_polynomial.evaluate_x(
        curve.field(), curve.j_invariant());
    const std::vector<mpz_class> neighbors = linear_roots(specialized);
    std::vector<ElkiesKernelResult> results;
    for (const mpz_class& neighbor : neighbors) {
        Curve codomain = curve;
        try {
            codomain = normalized_codomain_from_classical_modpoly(
                curve, modular_polynomial, neighbor);
        } catch (const std::domain_error&) {
            continue;
        }
        BmssIsogenyResult reconstruction =
            bmss_isogeny_reference(curve, codomain, ell);
        const auto eigenvalue = try_frobenius_eigenvalue_from_isogeny_kernel(
            curve, reconstruction.kernel, ell);
        if (!eigenvalue.has_value()) {
            throw std::runtime_error(
                "BMSS modular neighbor did not yield a Frobenius eigenkernel");
        }
        const std::uint64_t trace_residue = trace_residue_from_eigenvalue(
            curve.field().modulus(), ell, *eigenvalue);
        results.push_back({ell, std::move(reconstruction.kernel),
                           std::move(codomain), neighbor, *eigenvalue,
                           trace_residue});
    }
    static_cast<void>(common_trace_residue(results));
    return results;
}

std::optional<std::uint64_t> elkies_trace_residue_bmss_reference(
    const Curve& curve, const SparseModularPolynomial& modular_polynomial) {
    return common_trace_residue(
        elkies_kernels_bmss_reference(curve, modular_polynomial));
}

WeberElkiesLevelResult compute_weber_elkies_level_reference(
    const Curve& curve,
    const SparseModularPolynomial& weber_modular_polynomial,
    const std::vector<mpz_class>* restricted_source_lifts) {
    using Clock = std::chrono::steady_clock;
    const auto elapsed_us = [](const Clock::time_point& started) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - started)
                .count());
    };
    WeberElkiesLevelResult output;
    ElkiesStageTimings& measured = output.timings;
    const std::uint64_t ell = weber_modular_polynomial.level();
    const Field& field = curve.field();
    std::vector<mpz_class> discovered_source_lifts;
    const std::vector<mpz_class>* source_lifts = restricted_source_lifts;
    auto started = Clock::now();
    if (source_lifts == nullptr) {
        discovered_source_lifts = weber_f_lifts(field, curve.j_invariant());
        source_lifts = &discovered_source_lifts;
    }
    measured.source_lifts_us += elapsed_us(started);
    // Weber symmetries often produce the same normalized codomain many times.
    // For these fixed curves, the normalized isogeny (differential 1) is
    // unique, so deterministic BMSS and Frobenius results can be reused.
    std::map<std::pair<mpz_class, mpz_class>, std::optional<std::size_t>>
        codomain_cache;
    const auto remember_source_lift = [&output](const mpz_class& source_f) {
        if (std::find(output.compatible_source_lifts.begin(),
                      output.compatible_source_lifts.end(), source_f) ==
            output.compatible_source_lifts.end()) {
            output.compatible_source_lifts.push_back(source_f);
        }
    };
    started = Clock::now();
    std::vector<std::future<std::vector<mpz_class>>> root_jobs;
    root_jobs.reserve(source_lifts->size());
    for (const mpz_class& source_f : *source_lifts) {
        root_jobs.push_back(std::async(
            std::launch::async,
            [&field, &weber_modular_polynomial, source_f]() {
                const Poly specialized =
                    weber_modular_polynomial.evaluate_x(field, source_f);
                return linear_roots(specialized);
            }));
    }
    std::vector<std::vector<mpz_class>> neighbor_sets;
    neighbor_sets.reserve(root_jobs.size());
    for (auto& job : root_jobs) {
        neighbor_sets.push_back(job.get());
    }
    measured.modular_roots_us += elapsed_us(started);

    for (std::size_t source_index = 0; source_index < source_lifts->size();
         ++source_index) {
        const mpz_class& source_f = (*source_lifts)[source_index];
        const std::vector<mpz_class>& neighbor_lifts =
            neighbor_sets[source_index];
        for (const mpz_class& neighbor_f : neighbor_lifts) {
            ++measured.lift_pairs;
            Curve codomain = curve;
            started = Clock::now();
            try {
                codomain = normalized_codomain_from_weber_modpoly(
                    curve, weber_modular_polynomial, source_f, neighbor_f);
            } catch (const std::domain_error&) {
                measured.normalized_codomain_us += elapsed_us(started);
                continue;
            }
            measured.normalized_codomain_us += elapsed_us(started);

            const auto [cached, inserted] = codomain_cache.try_emplace(
                std::make_pair(codomain.a(), codomain.b()), std::nullopt);
            if (!inserted) {
                ++measured.codomain_cache_hits;
                if (cached->second.has_value()) {
                    remember_source_lift(source_f);
                }
                continue;
            }
            ++measured.distinct_codomains;

            BmssIsogenyResult reconstruction = {
                Poly(field), Poly(field), Poly(field)};
            started = Clock::now();
            try {
                reconstruction = bmss_isogeny_reference(curve, codomain, ell);
            } catch (const std::runtime_error&) {
                measured.bmss_us += elapsed_us(started);
                // For p=1 mod 12, roots of Psi^f need not all be compatible
                // class-invariant lifts. Full BMSS validation is the required
                // safe discriminator.
                continue;
            }
            measured.bmss_us += elapsed_us(started);
            const mpz_class neighbor_j = codomain.j_invariant();
            const auto duplicate = std::find_if(
                output.kernels.begin(), output.kernels.end(),
                [&reconstruction](const ElkiesKernelResult& existing) {
                    return equal(existing.kernel, reconstruction.kernel);
                });
            if (duplicate != output.kernels.end()) {
                if (duplicate->neighbor_j != neighbor_j) {
                    throw std::runtime_error(
                        "duplicate Weber lifts disagree on an isogeny kernel");
                }
                remember_source_lift(source_f);
                cached->second = static_cast<std::size_t>(
                    duplicate - output.kernels.begin());
                continue;
            }
            started = Clock::now();
            ++measured.eigenvalue_attempts;
            const auto eigenvalue = try_frobenius_eigenvalue_from_isogeny_kernel(
                curve, reconstruction.kernel, ell);
            measured.eigenvalue_us += elapsed_us(started);
            if (!eigenvalue.has_value()) {
                continue;
            }
            remember_source_lift(source_f);
            const std::uint64_t trace_residue = trace_residue_from_eigenvalue(
                field.modulus(), ell, *eigenvalue);
            output.kernels.push_back(
                {ell, std::move(reconstruction.kernel), std::move(codomain),
                 neighbor_j, *eigenvalue, trace_residue});
            cached->second = output.kernels.size() - 1U;
        }
    }
    static_cast<void>(common_trace_residue(output.kernels));
    return output;
}

std::vector<ElkiesKernelResult> elkies_kernels_weber_bmss_reference(
    const Curve& curve,
    const SparseModularPolynomial& weber_modular_polynomial,
    ElkiesStageTimings* timings) {
    WeberElkiesLevelResult result = compute_weber_elkies_level_reference(
        curve, weber_modular_polynomial);
    if (timings != nullptr) {
        *timings = result.timings;
    }
    return std::move(result.kernels);
}

std::optional<std::uint64_t> elkies_trace_residue_weber_bmss_reference(
    const Curve& curve,
    const SparseModularPolynomial& weber_modular_polynomial) {
    return common_trace_residue(elkies_kernels_weber_bmss_reference(
        curve, weber_modular_polynomial, nullptr));
}

}  // namespace oneshotsea
