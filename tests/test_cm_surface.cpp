#include "oneshotsea/cm_surface.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/weber.hpp"
#include "oneshotsea/weber_cm_surface.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function>
bool rejects(Function&& function) {
    try {
        function();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

std::vector<mpz_class> hilbert_minus_71_coefficients() {
    return {
        mpz_class("737707086760731113357714241006081263"),
        mpz_class("-425319473946139603274605151187659"),
        mpz_class("5138800366453976780323726329446"),
        mpz_class("-823534263439730779968091389"),
        mpz_class("98394038810047812049302"),
        mpz_class("-3091990138604570"),
        mpz_class("313645809715"),
        1};
}

std::vector<mpz_class> weber_class_minus_71_coefficients() {
    // x^7+x^6-x^5-x^4-x^3+x^2+2x-1, in ascending order.
    return {-1, 2, 1, -1, -1, -1, 1, 1};
}

void test_minus_71_surface() {
    const auto order = oneshotsea::validate_sutherland_suitable_order(
        5, -71, 1);
    const auto selected = oneshotsea::select_sutherland_crt_primes(
        order, 1009, 1, 1000);
    check(!selected.empty() && selected.front().prime == 1811 &&
              selected.front().trace == 12 &&
              selected.front().volcano_parameter == 2,
          "D=-71 selector produces the retained p=1811 witness");
    const oneshotsea::SutherlandCrtPrime witness = selected.front();
    const oneshotsea::Field field(witness.prime);
    const oneshotsea::Poly hilbert(
        field, hilbert_minus_71_coefficients());

    const auto surfaces = oneshotsea::enumerate_cm_interpolation_surfaces(
        order, witness, hilbert, 1811);
    const std::vector<mpz_class> expected_roots =
        {313, 1073, 1288, 1312, 1402, 1767, 1808};
    check(surfaces.level() == 5U &&
              surfaces.auxiliary_prime() == 1811 &&
              surfaces.all_surface_invariants() == expected_roots &&
              surfaces.surface_curves().size() == 7U &&
              surfaces.exact_group_order() == 1800 &&
              surfaces.horizontal_edges_per_surface() == 2U,
          "H_-71 splits into the seven expected CM surface invariants");

    const auto phi5 = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/j/phi_5.txt");
    for (const auto& surface : surfaces.surface_curves()) {
        check(surface.j_invariant == surface.curve.j_invariant() &&
                  hilbert.evaluate(surface.j_invariant) == 0 &&
                  oneshotsea::count_points_bruteforce(surface.curve) == 1800 &&
                  surface.edges.size() == 6U &&
                  surface.x_candidates_tested <= 1811U,
              "admitted surface curve has the HCP root and trace-sign order");
        const std::vector<mpz_class> weber_lifts =
            oneshotsea::weber_f_lifts(field, surface.j_invariant);
        check(weber_lifts.size() == 2U &&
                  field.add(weber_lifts.front(), weber_lifts.back()) == 0,
              "p=11 mod 12 CM surface has exactly the Weber +/- pair");

        const oneshotsea::Poly specialized =
            phi5.evaluate_x(field, surface.j_invariant);
        std::size_t horizontal = 0U;
        std::size_t descending = 0U;
        for (const auto& edge : surface.edges) {
            const mpz_class neighbor = edge.isogeny.codomain.j_invariant();
            check(specialized.evaluate(neighbor) == 0 &&
                      oneshotsea::count_points_bruteforce(
                          edge.isogeny.codomain) == 1800,
                  "classified CM edge is an authenticated Phi_5 neighbor with equal order");
            if (edge.codomain_on_surface) {
                ++horizontal;
                check(hilbert.evaluate(neighbor) == 0,
                      "horizontal edge lands on an H_-71 root");
            } else {
                ++descending;
                check(hilbert.evaluate(neighbor) != 0,
                      "descending edge leaves the H_-71 surface");
            }
        }
        check(horizontal == 2U && descending == 4U,
              "split level five yields two horizontal and four descending edges");
    }

    const oneshotsea::Field target_field(193);
    const std::vector<mpz_class> target_powers =
        oneshotsea::lifted_target_powers(target_field, 20, 6);
    const auto native_residue =
        oneshotsea::specialize_classical_from_cm_surfaces(
            surfaces, target_powers);
    const auto table_residue =
        oneshotsea::specialize_sparse_modpoly_for_crt_reference(
            phi5, target_powers, witness.prime);
    check(native_residue.prime == witness.prime &&
              native_residue.value_coefficients ==
                  table_residue.value_coefficients &&
              native_residue.x_derivative_coefficients ==
                  table_residue.x_derivative_coefficients,
          "table-free CM interpolation matches both Phi_5 specialization channels");

    const oneshotsea::Poly weber_class_polynomial(
        field, weber_class_minus_71_coefficients());
    const auto phi37_weber = oneshotsea::SparseModularPolynomial::load(
        37, "data/modpoly/weber_f/phi_37.txt");
    const auto weber_specialization =
        oneshotsea::specialize_weber_from_cm_surfaces(
            surfaces, weber_class_polynomial, {phi37_weber}, target_powers);
    const auto phi5_weber = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/weber_f/phi_5.txt");
    const auto weber_table_residue =
        oneshotsea::specialize_sparse_modpoly_for_crt_reference(
            phi5_weber, target_powers, witness.prime);
    check(weber_specialization.surface_invariant_count == 7U &&
              weber_specialization.floor_invariant_count == 28U &&
              weber_specialization.orientation_relation_count == 1U &&
              weber_specialization.relative_sign_coefficient == 1810 &&
              weber_specialization.residue.value_coefficients ==
                  weber_table_residue.value_coefficients &&
              weber_specialization.residue.x_derivative_coefficients ==
                  weber_table_residue.x_derivative_coefficients,
          "target-table-free Weber orientation matches both authenticated Phi_5 channels");

    oneshotsea::Poly negative_weber_class =
        oneshotsea::Poly::constant(field, 1);
    for (const mpz_class& root :
         std::vector<mpz_class>({11, 37, 491, 1028, 1091, 1188, 1586})) {
        negative_weber_class = oneshotsea::mul(
            negative_weber_class,
            oneshotsea::Poly(field, {root, 1}));
    }
    const auto globally_negated =
        oneshotsea::specialize_weber_from_cm_surfaces(
            surfaces, negative_weber_class, {phi37_weber}, target_powers);
    check(globally_negated.relative_sign_coefficient == 1810 &&
              globally_negated.residue.value_coefficients ==
                  weber_specialization.residue.value_coefficients &&
              globally_negated.residue.x_derivative_coefficients ==
                  weber_specialization.residue.x_derivative_coefficients,
          "global Weber sign choice leaves the normalized modular polynomial unchanged");

    for (std::size_t probe_degree = 0U; probe_degree < 7U;
         ++probe_degree) {
        std::vector<mpz_class> probe(7U, 0);
        probe.front() = 1;
        if (probe_degree != 0U) {
            probe[probe_degree] = 1;
        }
        const auto native_probe =
            oneshotsea::specialize_weber_from_cm_surfaces(
                surfaces, weber_class_polynomial, {phi37_weber}, probe);
        const auto table_probe =
            oneshotsea::specialize_sparse_modpoly_for_crt_reference(
                phi5_weber, probe, witness.prime);
        check(native_probe.residue.value_coefficients ==
                      table_probe.value_coefficients &&
                  native_probe.residue.x_derivative_coefficients ==
                      table_probe.x_derivative_coefficients,
              "Weber interpolation agrees on every lifted-power basis probe");
    }

    const auto phi19_weber = oneshotsea::SparseModularPolynomial::load(
        19, "data/modpoly/weber_f/phi_19.txt");
    const oneshotsea::SparseModularPolynomial malformed_phi37(
        37, {{38, 0, 1}, {0, 38, 1}, {37, 37, 1}});
    oneshotsea::Poly mixed_sign_class = oneshotsea::Poly::constant(field, 1);
    for (const mpz_class& root :
         std::vector<mpz_class>({1800, 37, 491, 1028, 1091, 1188, 1586})) {
        mixed_sign_class = oneshotsea::mul(
            mixed_sign_class,
            oneshotsea::Poly(field, {field.neg(root), 1}));
    }
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::specialize_weber_from_cm_surfaces(
                      surfaces, weber_class_polynomial, {}, target_powers));
          }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial, {phi19_weber},
                          target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial, {phi5_weber},
                          target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial,
                          {phi37_weber, phi37_weber}, target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial, {malformed_phi37},
                          target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, mixed_sign_class, {phi37_weber},
                          target_powers));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::specialize_weber_from_cm_surfaces(
                          surfaces, weber_class_polynomial, {phi37_weber},
                          {1, 2}));
              }),
          "Weber orientation rejects missing, insufficient, target-level, duplicate, malformed, mixed-sign, and malformed-power evidence");
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::specialize_classical_from_cm_surfaces(
                      surfaces, {1, 2}));
          }) &&
              rejects([&] {
                  std::vector<mpz_class> invalid = target_powers;
                  invalid.front() = 2;
                  static_cast<void>(
                      oneshotsea::specialize_classical_from_cm_surfaces(
                          surfaces, invalid));
              }) &&
              rejects([&] {
                  std::vector<mpz_class> invalid = target_powers;
                  invalid.back() = -1;
                  static_cast<void>(
                      oneshotsea::specialize_classical_from_cm_surfaces(
                          surfaces, invalid));
              }),
          "CM specialization rejects malformed target-power evidence");

    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::enumerate_cm_interpolation_surfaces(
                      order, witness, hilbert, 1));
          }) &&
              rejects([&] {
                  auto malformed_witness = witness;
                  ++malformed_witness.trace;
                  static_cast<void>(
                      oneshotsea::enumerate_cm_interpolation_surfaces(
                          order, malformed_witness, hilbert, 1811));
              }) &&
              rejects([&] {
                  const oneshotsea::Poly wrong_degree(field, {1, 0, 1});
                  static_cast<void>(
                      oneshotsea::enumerate_cm_interpolation_surfaces(
                          order, witness, wrong_degree, 1811));
              }),
          "CM surface admission fails closed on caps, witnesses, and HCP shape");

    oneshotsea::Poly repeated = oneshotsea::Poly::constant(field, 1);
    for (const mpz_class& root :
         std::vector<mpz_class>({1, 1, 2, 3, 4, 5, 6})) {
        repeated = oneshotsea::mul(
            repeated, oneshotsea::Poly(field, {field.neg(root), 1}));
    }
    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::enumerate_cm_interpolation_surfaces(
                      order, witness, repeated, 1811));
          }),
          "CM surface admission rejects a repeated-root degree-seven impostor");
}

}  // namespace

int main() {
    try {
        test_minus_71_surface();
        std::cout << "CM interpolation surface tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "CM interpolation surface tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
