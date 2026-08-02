#include "oneshotsea/poly.hpp"
#include "oneshotsea/search_pipeline.hpp"
#include "oneshotsea/sea.hpp"
#include "oneshotsea/x1_27_probe.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kSeed = UINT64_C(202607300000);
constexpr std::uint64_t kGlobalIndex = UINT64_C(1000030);
constexpr std::uint64_t kMaxLevel = 401U;
constexpr std::size_t kTraceCap = 16U;
constexpr std::size_t kSeaThreads = 1U;

mpz_class target_prime() {
    return mpz_class(
        "10000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000237");
}

std::uint64_t elapsed_us(const Clock::time_point& started) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started);
    if (elapsed.count() < 0) {
        throw std::logic_error("steady clock moved backwards");
    }
    return static_cast<std::uint64_t>(elapsed.count());
}

std::uint64_t parse_u64(std::string_view encoded, const char* label) {
    if (encoded.empty()) {
        throw std::invalid_argument(std::string(label) + " is empty");
    }
    std::size_t consumed = 0U;
    const unsigned long long parsed = std::stoull(std::string(encoded),
                                                  &consumed, 10);
    if (consumed != encoded.size()) {
        throw std::invalid_argument(std::string(label) +
                                    " is not an unsigned integer");
    }
    if (parsed > std::numeric_limits<std::uint64_t>::max()) {
        throw std::out_of_range(std::string(label) + " is too large");
    }
    return static_cast<std::uint64_t>(parsed);
}

void print_bool(const char* label, bool value) {
    std::cout << label << '=' << (value ? "true" : "false") << '\n';
}

template <typename Integer>
void print_vector(const char* label, const std::vector<Integer>& values) {
    std::cout << label << ".size=" << values.size() << '\n';
    for (std::size_t index = 0U; index < values.size(); ++index) {
        std::cout << label << '[' << index << "]=" << values[index] << '\n';
    }
}

void print_optional_u64(const char* label,
                        const std::optional<std::uint64_t>& value) {
    print_bool((std::string(label) + ".present").c_str(), value.has_value());
    if (value.has_value()) {
        std::cout << label << ".value=" << *value << '\n';
    }
}

void print_poly(const char* label, const oneshotsea::Poly& polynomial) {
    std::cout << label << ".degree=" << polynomial.degree() << '\n';
    print_vector((std::string(label) + ".coefficients").c_str(),
                 polynomial.coefficients());
}

void print_probe_counters(const oneshotsea::X127ProbeRejections& counters) {
    std::cout << "generator.counters.u_samples=" << counters.u_samples << '\n'
              << "generator.counters.u_polynomials_without_roots="
              << counters.u_polynomials_without_roots << '\n'
              << "generator.counters.x1_points=" << counters.x1_points << '\n'
              << "generator.counters.exceptional_map_points="
              << counters.exceptional_map_points << '\n'
              << "generator.counters.singular_curves="
              << counters.singular_curves << '\n'
              << "generator.counters.exceptional_j=" << counters.exceptional_j
              << '\n'
              << "generator.counters.exact_order_27_failures="
              << counters.exact_order_27_failures << '\n'
              << "generator.counters.points_without_weber_lifts="
              << counters.points_without_weber_lifts << '\n'
              << "generator.counters.weber_lifts=" << counters.weber_lifts
              << '\n'
              << "generator.counters.nonsquare_explicit_montgomery_u="
              << counters.nonsquare_explicit_montgomery_u << '\n'
              << "generator.counters.points_without_explicit_montgomery_model="
              << counters.points_without_explicit_montgomery_model << '\n'
              << "generator.counters.full_two_torsion_failures="
              << counters.full_two_torsion_failures << '\n'
              << "generator.counters.point_four_rejections="
              << counters.point_four_rejections << '\n'
              << "generator.counters.accepted=" << counters.accepted << '\n';
}

void print_sample(const oneshotsea::X127ProbeResult& generated) {
    if (!generated.sample.has_value()) {
        throw std::runtime_error("fixed X1(27) input did not admit a sample");
    }
    const oneshotsea::X127ProbeSample& sample = *generated.sample;
    print_probe_counters(generated.counters);
    std::cout << "generator.schema=" << oneshotsea::kX127ProbeSchema << '\n'
              << "generator.version=" << oneshotsea::kX127ProbeGeneratorVersion
              << '\n'
              << "generator.formula_sha256="
              << oneshotsea::kX127FormulaSourceSha256 << '\n'
              << "generator.global_index=" << sample.global_index << '\n'
              << "generator.x1_u=" << sample.x1_u << '\n'
              << "generator.x1_v=" << sample.x1_v << '\n'
              << "generator.tate_r=" << sample.tate_r << '\n'
              << "generator.tate_s=" << sample.tate_s << '\n'
              << "generator.tate_a1=" << sample.tate_a1 << '\n'
              << "generator.tate_a2=" << sample.tate_a2 << '\n'
              << "generator.tate_a3=" << sample.tate_a3 << '\n'
              << "generator.tate_curve.a=" << sample.tate_curve.a() << '\n'
              << "generator.tate_curve.b=" << sample.tate_curve.b() << '\n'
              << "generator.tate_point.x=" << sample.tate_point_x << '\n'
              << "generator.tate_point.y=" << sample.tate_point_y << '\n'
              << "generator.explicit_montgomery_coefficient="
              << sample.explicit_montgomery_coefficient << '\n'
              << "generator.weber_f=" << sample.pair.weber_f << '\n'
              << "generator.j=" << sample.pair.j_invariant << '\n'
              << "generator.twist_parameter=" << sample.pair.twist_parameter
              << '\n'
              << "generator.curve.a=" << sample.pair.curve.a() << '\n'
              << "generator.curve.b=" << sample.pair.curve.b() << '\n'
              << "generator.twist.a=" << sample.pair.twist.a() << '\n'
              << "generator.twist.b=" << sample.pair.twist.b() << '\n'
              << "generator.rejected_samples=" << sample.pair.rejected_samples
              << '\n'
              << "generator.selected_side="
              << oneshotsea::x1_27_canonical_side_name(sample.selected_side)
              << '\n';
    print_bool("generator.full_rational_two_torsion",
               sample.has_full_rational_two_torsion);
    print_bool("generator.point_order_four", sample.has_point_order_four);
    std::cout << "generator.cyclic_divisor=" << sample.cyclic_divisor << '\n'
              << "generator.group_divisor=" << sample.group_divisor << '\n'
              << "generator.opposite_order_residue="
              << sample.opposite_order_residue << '\n';
}

oneshotsea::ExactTracePrior trace_prior(
    const mpz_class& prime, const oneshotsea::X127ProbeSample& sample) {
    const std::uint64_t divisor = sample.group_divisor;
    if (divisor == 0U) {
        throw std::logic_error("fixed X1(27) sample has zero group divisor");
    }
    const std::uint64_t p_plus_one =
        (mpz_fdiv_ui(prime.get_mpz_t(), divisor) + 1U) % divisor;
    const std::uint64_t residue =
        sample.selected_side == oneshotsea::X127CanonicalSide::curve
            ? p_plus_one
            : (divisor - p_plus_one) % divisor;
    return oneshotsea::ExactTracePrior(prime, divisor, residue);
}

oneshotsea::Poly deterministic_modulus(const oneshotsea::Field& field,
                                       std::size_t degree) {
    std::vector<mpz_class> coefficients(degree + 1U);
    for (std::size_t index = 0U; index < degree; ++index) {
        coefficients[index] = oneshotsea::deterministic_residue(
            field, UINT64_C(0x70313235706f6c79),
            static_cast<std::uint64_t>(index),
            UINT64_C(0x66726f62656e6975) ^
                static_cast<std::uint64_t>(degree));
    }
    coefficients.back() = 1;
    return oneshotsea::Poly(field, std::move(coefficients));
}

int run_frobenius(std::uint64_t degree_u64, std::uint64_t repetitions) {
    if (degree_u64 == 0U ||
        degree_u64 > static_cast<std::uint64_t>(
                         std::numeric_limits<std::size_t>::max() - 1U)) {
        throw std::invalid_argument("DEGREE is out of range");
    }
    if (repetitions == 0U) {
        throw std::invalid_argument("REPETITIONS must be positive");
    }
    const std::size_t degree = static_cast<std::size_t>(degree_u64);
    const mpz_class prime = target_prime();
    const oneshotsea::Field field(prime);
    const oneshotsea::X127ProbeResult generated =
        oneshotsea::deterministic_x1_27_search_curve(
            prime, kSeed, kGlobalIndex, true);
    if (!generated.sample.has_value()) {
        throw std::runtime_error("fixed X1(27) input did not admit a sample");
    }
    const oneshotsea::Curve& curve = generated.sample->pair.curve;
    const oneshotsea::Poly modulus = deterministic_modulus(field, degree);
    const oneshotsea::Poly x = oneshotsea::Poly::x(field);
    const oneshotsea::Poly curve_rhs(
        field, {curve.b(), curve.a(), mpz_class(0), mpz_class(1)});

    oneshotsea::Poly x_to_p(field);
    oneshotsea::Poly rhs_to_half(field);
    const Clock::time_point started = Clock::now();
    for (std::uint64_t repetition = 0U; repetition < repetitions;
         ++repetition) {
        oneshotsea::Poly current_x =
            oneshotsea::powmod(x, prime, modulus);
        oneshotsea::Poly current_rhs = oneshotsea::powmod(
            curve_rhs, (prime - 1) / 2, modulus);
        if (repetition == 0U) {
            x_to_p = std::move(current_x);
            rhs_to_half = std::move(current_rhs);
        } else if (!oneshotsea::equal(x_to_p, current_x) ||
                   !oneshotsea::equal(rhs_to_half, current_rhs)) {
            throw std::runtime_error(
                "repeated quotient Frobenius outputs differ");
        }
    }
    const std::uint64_t measured_us = elapsed_us(started);

    std::cout << "projection.schema=oneshotsea.p125-poly-trusted.v1\n"
              << "projection.mode=frobenius\n"
              << "prime=" << prime << '\n'
              << "seed=" << kSeed << '\n'
              << "global_index=" << kGlobalIndex << '\n'
              << "point_four_required=true\n"
              << "degree=" << degree << '\n'
              << "repetitions=" << repetitions << '\n';
    print_sample(generated);
    print_poly("quotient.modulus", modulus);
    print_poly("frobenius.x_to_p", x_to_p);
    print_poly("frobenius.rhs_to_half", rhs_to_half);

    std::cerr << "timing.schema=oneshotsea.p125-poly-trusted-timing.v1\n"
              << "timing.mode=frobenius\n"
              << "timing.degree=" << degree << '\n'
              << "timing.repetitions=" << repetitions << '\n'
              << "timing.elapsed_us=" << measured_us << '\n';
    return 0;
}

void print_level(std::size_t index,
                 const oneshotsea::WeberSeaLevelRecord& level) {
    const std::string prefix = "sea.level[" + std::to_string(index) + "]";
    std::cout << prefix << ".ell=" << level.ell << '\n';
    print_bool((prefix + ".exact").c_str(), level.exact);
    print_optional_u64((prefix + ".trace_residue").c_str(),
                       level.trace_residue);
    std::cout << prefix << ".exact_modulus=" << level.exact_modulus << '\n'
              << prefix << ".constraint_modulus=" << level.constraint_modulus
              << '\n'
              << prefix << ".exact_trace_candidate_count="
              << level.exact_trace_candidate_count << '\n'
              << prefix << ".trace_candidate_count="
              << level.trace_candidate_count << '\n';
    print_optional_u64((prefix + ".atkin_projective_order").c_str(),
                       level.atkin_projective_order);
    std::cout << prefix << ".atkin_residue_count="
              << level.atkin_residue_count << '\n'
              << prefix << ".compatible_source_lifts="
              << level.compatible_source_lifts << '\n'
              << prefix << ".modular_root_workers="
              << level.timings.modular_root_workers << '\n'
              << prefix << ".modular_root_orbits="
              << level.timings.modular_root_orbits << '\n'
              << prefix << ".modular_root_reused_lifts="
              << level.timings.modular_root_reused_lifts << '\n';
    print_bool((prefix + ".modular_root_orbit_reuse").c_str(),
               level.timings.modular_root_orbit_reuse);
    std::cout << prefix << ".lift_pairs=" << level.timings.lift_pairs << '\n'
              << prefix << ".distinct_codomains="
              << level.timings.distinct_codomains << '\n'
              << prefix << ".codomain_cache_hits="
              << level.timings.codomain_cache_hits << '\n';
    print_bool((prefix + ".conjugate_eigenvalue_reuse").c_str(),
               level.timings.conjugate_eigenvalue_reuse);
    std::cout << prefix << ".eigenvalue_attempts="
              << level.timings.eigenvalue_attempts << '\n'
              << prefix << ".independent_eigenvalue_recoveries="
              << level.timings.independent_eigenvalue_recoveries << '\n'
              << prefix << ".conjugate_eigenvalues_derived="
              << level.timings.conjugate_eigenvalues_derived << '\n';
}

void print_constraints(const char* label,
                       const oneshotsea::TraceConstraints& constraints) {
    std::cout << label << ".prime=" << constraints.prime() << '\n'
              << label << ".modulus=" << constraints.modulus() << '\n'
              << label << ".hasse_radius=" << constraints.hasse_radius()
              << '\n'
              << label << ".candidate_count="
              << constraints.candidate_count() << '\n';
    print_vector((std::string(label) + ".residues").c_str(),
                 constraints.residues());
}

int run_sea(const std::string& table_directory, std::uint64_t max_level) {
    if (max_level < 5U ||
        max_level > static_cast<std::uint64_t>(
                        std::numeric_limits<unsigned>::max())) {
        throw std::invalid_argument("MAX_LEVEL is out of range");
    }
    const mpz_class prime = target_prime();
    // Authenticate and bind the full production table corpus before starting
    // either the generation or SEA timer.  This keeps the measured interval
    // focused on the candidate hot path while making projection equality
    // meaningful only for the exact same trusted inputs.
    const std::string table_manifest_sha256 =
        oneshotsea::weber_table_manifest_sha256(table_directory, max_level);
    const Clock::time_point total_started = Clock::now();
    const Clock::time_point generation_started = Clock::now();
    const oneshotsea::X127ProbeResult generated =
        oneshotsea::deterministic_x1_27_search_curve(
            prime, kSeed, kGlobalIndex, true);
    const std::uint64_t generation_us = elapsed_us(generation_started);
    if (!generated.sample.has_value()) {
        throw std::runtime_error("fixed X1(27) input did not admit a sample");
    }
    const oneshotsea::X127ProbeSample& sample = *generated.sample;
    const oneshotsea::ExactTracePrior prior = trace_prior(prime, sample);

    const Clock::time_point sea_started = Clock::now();
    const oneshotsea::WeberSeaResult result =
        oneshotsea::run_weber_sea_reference(
            sample.pair.curve, table_directory, max_level, kTraceCap, {},
            kSeaThreads, true, true, {}, prior, sample.pair.weber_f);
    const std::uint64_t sea_us = elapsed_us(sea_started);
    const std::uint64_t total_us = elapsed_us(total_started);

    std::uint64_t source_lifts_us = 0U;
    std::uint64_t modular_roots_us = 0U;
    std::uint64_t normalized_codomain_us = 0U;
    std::uint64_t bmss_us = 0U;
    std::uint64_t eigenvalue_us = 0U;
    for (const oneshotsea::WeberSeaLevelRecord& level : result.levels) {
        source_lifts_us += level.timings.source_lifts_us;
        modular_roots_us += level.timings.modular_roots_us;
        normalized_codomain_us += level.timings.normalized_codomain_us;
        bmss_us += level.timings.bmss_us;
        eigenvalue_us += level.timings.eigenvalue_us;
    }

    std::cout << "projection.schema=oneshotsea.p125-poly-trusted.v1\n"
              << "projection.mode=sea\n"
              << "prime=" << prime << '\n'
              << "seed=" << kSeed << '\n'
              << "global_index=" << kGlobalIndex << '\n'
              << "point_four_required=true\n"
              << "sea.max_level=" << max_level << '\n'
              << "sea.trace_cap=" << kTraceCap << '\n'
              << "sea.threads=" << kSeaThreads << '\n'
              << "sea.schedule=increasing\n"
              << "sea.table_manifest_sha256=" << table_manifest_sha256 << '\n'
              << "sea.known_source_lift=true\n";
    print_bool("sea.root_orbit_reuse", true);
    print_bool("sea.conjugate_eigenvalue_reuse", true);
    print_sample(generated);
    std::cout << "sea.trace_prior.modulus=" << prior.modulus() << '\n'
              << "sea.trace_prior.residue=" << prior.residue() << '\n'
              << "sea.levels.size=" << result.levels.size() << '\n';
    for (std::size_t index = 0U; index < result.levels.size(); ++index) {
        print_level(index, result.levels[index]);
    }
    print_constraints("sea.exact_constraints", result.constraints);
    print_constraints("sea.effective_constraints",
                      result.effective_constraints);
    std::cout << "sea.atkin_constraints.size="
              << result.atkin_constraints.size() << '\n';
    for (std::size_t index = 0U; index < result.atkin_constraints.size();
         ++index) {
        const oneshotsea::AtkinConstraint& constraint =
            result.atkin_constraints[index];
        const std::string prefix =
            "sea.atkin_constraints[" + std::to_string(index) + "]";
        std::cout << prefix << ".ell=" << constraint.ell << '\n'
                  << prefix << ".projective_order="
                  << constraint.projective_order << '\n';
        print_vector((prefix + ".trace_residues").c_str(),
                     constraint.trace_residues);
    }
    print_vector("sea.compatible_source_lifts",
                 result.compatible_source_lifts);
    print_bool("sea.traces.present", result.traces.has_value());
    if (result.traces.has_value()) {
        print_vector("sea.traces", *result.traces);
    }
    std::cout << "sea.schoof_fallback_levels.size="
              << result.schoof_fallback_levels.size() << '\n';
    for (std::size_t index = 0U;
         index < result.schoof_fallback_levels.size(); ++index) {
        const oneshotsea::SchoofFallbackLevelRecord& level =
            result.schoof_fallback_levels[index];
        const std::string prefix =
            "sea.schoof_fallback_levels[" + std::to_string(index) + "]";
        std::cout << prefix << ".ell=" << level.ell << '\n'
                  << prefix << ".trace_residue=" << level.trace_residue << '\n'
                  << prefix << ".exact_modulus=" << level.exact_modulus << '\n'
                  << prefix << ".constraint_modulus="
                  << level.constraint_modulus << '\n'
                  << prefix << ".exact_trace_candidate_count="
                  << level.exact_trace_candidate_count << '\n'
                  << prefix << ".trace_candidate_count="
                  << level.trace_candidate_count << '\n';
    }

    std::cerr << "timing.schema=oneshotsea.p125-poly-trusted-timing.v1\n"
              << "timing.mode=sea\n"
              << "timing.generation_us=" << generation_us << '\n'
              << "timing.sea_us=" << sea_us << '\n'
              << "timing.total_us=" << total_us << '\n'
              << "timing.source_lifts_us=" << source_lifts_us << '\n'
              << "timing.modular_roots_us=" << modular_roots_us << '\n'
              << "timing.normalized_codomain_us=" << normalized_codomain_us
              << '\n'
              << "timing.bmss_us=" << bmss_us << '\n'
              << "timing.eigenvalue_us=" << eigenvalue_us << '\n';
    return 0;
}

void usage(const char* executable) {
    std::cerr << "usage: " << executable
              << " frobenius DEGREE REPETITIONS\n"
              << "       " << executable
              << " sea TABLE_DIRECTORY [MAX_LEVEL]\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 2 && std::string_view(argv[1]) == "frobenius") {
            if (argc != 4) {
                usage(argv[0]);
                return 2;
            }
            return run_frobenius(parse_u64(argv[2], "DEGREE"),
                                  parse_u64(argv[3], "REPETITIONS"));
        }
        if (argc >= 2 && std::string_view(argv[1]) == "sea") {
            if (argc != 3 && argc != 4) {
                usage(argv[0]);
                return 2;
            }
            const std::uint64_t max_level =
                argc == 4 ? parse_u64(argv[3], "MAX_LEVEL") : kMaxLevel;
            return run_sea(argv[2], max_level);
        }
        usage(argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
