#include "oneshotsea/elkies.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/prime_isogeny.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

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

oneshotsea::Curve curve_from_j(const oneshotsea::Field& field,
                               const mpz_class& j) {
    const mpz_class normalized = field.normalize(j);
    if (normalized == 0 || normalized == field.normalize(1728)) {
        throw std::invalid_argument("test curve_from_j excludes ramified j");
    }
    const mpz_class k = field.divide(normalized, field.sub(1728, normalized));
    oneshotsea::Curve curve(field, field.mul(3, k), field.mul(2, k));
    check(!curve.is_singular() && curve.j_invariant() == normalized,
          "j-model construction validates its invariant");
    return curve;
}

mpz_class least_nonsquare(const oneshotsea::Field& field) {
    for (mpz_class candidate = 2; candidate < field.modulus(); ++candidate) {
        if (field.legendre(candidate) == -1) {
            return candidate;
        }
    }
    throw std::logic_error("prime-field test fixture has no nonsquare");
}

void test_affine_group_law() {
    const oneshotsea::Curve curve(oneshotsea::Field(101), 2, 3);
    const oneshotsea::AffinePoint point{3, 6, false};
    check(oneshotsea::affine_point_is_on_curve(curve, point),
          "affine fixture lies on its curve");
    const auto negative = oneshotsea::affine_point_negate(curve, point);
    check(negative.x == 3 && negative.y == 95 && !negative.infinity &&
              oneshotsea::affine_point_add(curve, point, negative).infinity,
          "affine negation and inverse addition agree");
    const auto doubled = oneshotsea::affine_point_add(curve, point, point);
    check(oneshotsea::affine_point_is_on_curve(curve, doubled) &&
              !doubled.infinity &&
              oneshotsea::affine_scalar_multiply(curve, 2, point).x ==
                  doubled.x &&
              oneshotsea::affine_scalar_multiply(curve, 2, point).y ==
                  doubled.y &&
              oneshotsea::affine_scalar_multiply(curve, 0, point).infinity,
          "affine double-and-add matches group addition");
    check(rejects([&] {
              static_cast<void>(oneshotsea::affine_point_add(
                  curve, point, {104, 6, false}));
          }) &&
              rejects([&] {
                  static_cast<void>(oneshotsea::affine_scalar_multiply(
                      curve, -1, point));
              }),
          "affine arithmetic rejects noncanonical points and negative scalars");
}

void test_full_rational_five_torsion() {
    // D=-19 has Hilbert class polynomial X+884736.  The norm equation
    // 4*131=7^2-5^2*(-19) puts the maximal-order CM curve above the floor of
    // its 5-volcano, and #E(F_131)=125 selects the trace +7 twist.  Here
    // E[5] is rational and yields all six cyclic subgroups required by the
    // surface row of BLS Algorithm 2.1.
    const oneshotsea::Field field(131);
    oneshotsea::Curve curve = curve_from_j(field, -884736);
    if (oneshotsea::count_points_bruteforce(curve) != 125) {
        curve = curve.quadratic_twist(2);
    }
    check(curve.j_invariant() == field.normalize(-884736) &&
              oneshotsea::count_points_bruteforce(curve) == 125,
          "CM fixture selects the trace +7 curve");

    const auto enumerated = oneshotsea::enumerate_rational_prime_isogenies(
        curve, 5, 125, 131);
    check(enumerated.isogenies.size() == 6U &&
              enumerated.x_candidates_tested <= 131U &&
              enumerated.group_order_level_valuation == 3U,
          "full rational E[5] yields exactly six witnessed subgroups");

    const auto phi5 = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/j/phi_5.txt");
    const oneshotsea::Poly specialized =
        phi5.evaluate_x(field, curve.j_invariant());
    std::set<std::string> neighbor_invariants;
    std::set<std::string> kernel_identifiers;
    for (const auto& isogeny : enumerated.isogenies) {
        check(isogeny.kernel.degree() == 2 &&
                  isogeny.kernel.leading_coefficient() == 1 &&
                  !isogeny.generator.infinity &&
                  oneshotsea::affine_scalar_multiply(
                      curve, 5, isogeny.generator)
                      .infinity,
              "enumerated isogeny retains an exact-order kernel witness");
        const oneshotsea::Curve reference =
            oneshotsea::velu_codomain_reference(curve, isogeny.kernel, 5);
        check(reference.a() == isogeny.codomain.a() &&
                  reference.b() == isogeny.codomain.b(),
              "point-sum Velu agrees with the division-kernel reference");
        const mpz_class neighbor = isogeny.codomain.j_invariant();
        check(specialized.evaluate(neighbor) == 0 &&
                  oneshotsea::count_points_bruteforce(isogeny.codomain) ==
                      125,
              "Velu codomain is an authenticated Phi_5 neighbor in the same isogeny class");
        neighbor_invariants.insert(neighbor.get_str());
        kernel_identifiers.insert(
            isogeny.kernel.coefficient(0).get_str() + ":" +
            isogeny.kernel.coefficient(1).get_str());

        const auto alternate = oneshotsea::affine_scalar_multiply(
            curve, 2, isogeny.generator);
        const auto alternate_kernel =
            oneshotsea::cyclic_kernel_polynomial(curve, alternate, 5);
        const auto alternate_codomain =
            oneshotsea::velu_codomain_from_cyclic_subgroup(
                curve, alternate, 5);
        check(oneshotsea::equal(alternate_kernel, isogeny.kernel) &&
                  alternate_codomain.a() == isogeny.codomain.a() &&
                  alternate_codomain.b() == isogeny.codomain.b(),
              "a different generator of one subgroup yields the same quotient");
    }
    check(kernel_identifiers.size() == 6U,
          "all six rational cyclic subgroups have distinct kernels");
    // The maximal order of discriminant -19 has class number one.  Its two
    // horizontal 5-isogenies are endomorphisms and therefore share the source
    // j-invariant as codomain; the four descending edges are distinct.
    check(neighbor_invariants.size() == 5U &&
              neighbor_invariants.count(curve.j_invariant().get_str()) == 1U,
          "the class-number-one fixture retains the expected horizontal self-neighbor multiplicity");

    const auto compact =
        oneshotsea::enumerate_rational_prime_isogeny_neighbors(
            curve, 5, 125, 131);
    std::multiset<std::string> full_neighbors;
    std::multiset<std::string> compact_neighbors;
    for (const auto& isogeny : enumerated.isogenies) {
        full_neighbors.insert(isogeny.codomain.j_invariant().get_str());
    }
    for (const auto& neighbor : compact.neighbors) {
        compact_neighbors.insert(neighbor.codomain.j_invariant().get_str());
        check(!neighbor.generator.infinity &&
                  oneshotsea::affine_scalar_multiply(
                      curve, 5, neighbor.generator).infinity,
              "compact neighbor retains an exact-order generator witness");
    }
    check(compact.neighbors.size() == 6U &&
              compact.x_candidates_tested ==
                  enumerated.x_candidates_tested &&
              compact.group_order_level_valuation ==
                  enumerated.group_order_level_valuation &&
              compact_neighbors == full_neighbors,
          "compact projective-line enumeration exactly matches the independent kernel-retaining path");

    check(rejects([&] {
              static_cast<void>(
                  oneshotsea::enumerate_rational_prime_isogenies(
                      curve, 5, 125, 1));
          }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::enumerate_rational_prime_isogenies(
                          curve, 5, 124, 131));
              }) &&
              rejects([&] {
                  static_cast<void>(
                      oneshotsea::enumerate_rational_prime_isogenies(
                          curve.quadratic_twist(2), 5, 125, 131));
              }),
          "enumeration fails closed on its cap, invalid order, and wrong twist");
}

void test_tonelli_shanks_volcano_fixture() {
    // A second class-number-one CM surface exercises the general
    // Tonelli-Shanks branch: 4*881=57^2-5^2*(-11), p=1 mod 4, and the trace
    // +57 curve has order 825 with full rational E[5].
    const oneshotsea::Field field(881);
    oneshotsea::Curve curve = curve_from_j(field, -32768);
    if (oneshotsea::count_points_bruteforce(curve) != 825) {
        curve = curve.quadratic_twist(least_nonsquare(field));
    }
    check(oneshotsea::count_points_bruteforce(curve) == 825,
          "D=-11 fixture selects the trace +57 curve");
    const auto enumerated = oneshotsea::enumerate_rational_prime_isogenies(
        curve, 5, 825, 881);
    check(enumerated.isogenies.size() == 6U &&
              enumerated.group_order_level_valuation == 2U,
          "Tonelli-Shanks sampling recovers full rational E[5]");

    const auto phi5 = oneshotsea::SparseModularPolynomial::load(
        5, "data/modpoly/j/phi_5.txt");
    const oneshotsea::Poly specialized =
        phi5.evaluate_x(field, curve.j_invariant());
    std::multiset<std::string> full_neighbors;
    for (const auto& isogeny : enumerated.isogenies) {
        const oneshotsea::Curve reference =
            oneshotsea::velu_codomain_reference(curve, isogeny.kernel, 5);
        check(reference.a() == isogeny.codomain.a() &&
                  reference.b() == isogeny.codomain.b() &&
                  specialized.evaluate(isogeny.codomain.j_invariant()) == 0 &&
                  oneshotsea::count_points_bruteforce(isogeny.codomain) ==
                      825,
              "Tonelli-Shanks fixture agrees with both independent isogeny oracles");
        full_neighbors.insert(
            isogeny.codomain.j_invariant().get_str());
    }

    const auto compact =
        oneshotsea::enumerate_rational_prime_isogeny_neighbors(
            curve, 5, 825, 881);
    std::multiset<std::string> compact_neighbors;
    for (const auto& neighbor : compact.neighbors) {
        check(!neighbor.generator.infinity &&
                  oneshotsea::affine_scalar_multiply(
                      curve, 5, neighbor.generator).infinity &&
                  specialized.evaluate(
                      neighbor.codomain.j_invariant()) == 0 &&
                  oneshotsea::count_points_bruteforce(
                      neighbor.codomain) == 825,
              "fast 64-bit neighbors retain exact-order and modular-polynomial evidence");
        compact_neighbors.insert(
            neighbor.codomain.j_invariant().get_str());
    }
    check(compact.neighbors.size() == 6U &&
              compact.x_candidates_tested ==
                  enumerated.x_candidates_tested &&
              compact.group_order_level_valuation ==
                  enumerated.group_order_level_valuation &&
              compact_neighbors == full_neighbors,
          "fast 64-bit projective-line enumeration matches the independent kernel path on the Tonelli-Shanks fixture");
}

}  // namespace

int main() {
    try {
        test_affine_group_law();
        test_full_rational_five_torsion();
        test_tonelli_shanks_volcano_fixture();
        std::cout << "auxiliary prime isogeny tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "auxiliary prime isogeny tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
