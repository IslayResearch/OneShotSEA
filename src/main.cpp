#include "oneshotsea/curve.hpp"
#include "oneshotsea/elkies.hpp"
#include "oneshotsea/modpoly.hpp"
#include "oneshotsea/schoof.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace {

std::map<std::string, std::string> parse_options(int argc, char** argv, int begin) {
    std::map<std::string, std::string> options;
    for (int i = begin; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument.rfind("--", 0) != 0 || i + 1 >= argc) {
            throw std::invalid_argument("expected --name value option, got: " + argument);
        }
        options[argument.substr(2)] = argv[++i];
    }
    return options;
}

const std::string& required(const std::map<std::string, std::string>& options,
                            const std::string& name) {
    const auto found = options.find(name);
    if (found == options.end()) {
        throw std::invalid_argument("missing --" + name);
    }
    return found->second;
}

std::uint64_t required_u64(const std::map<std::string, std::string>& options,
                           const std::string& name) {
    const std::string& text = required(options, name);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("--" + name + " must be an unsigned 64-bit integer");
    }
    return value;
}

unsigned required_unsigned(const std::map<std::string, std::string>& options,
                           const std::string& name) {
    const std::uint64_t value = required_u64(options, name);
    if (value > std::numeric_limits<unsigned>::max()) {
        throw std::invalid_argument("--" + name + " is out of range");
    }
    return static_cast<unsigned>(value);
}

void usage() {
    std::cerr
        << "usage:\n"
        << "  oneshotsea curve --p P --seed S --index I\n"
        << "  oneshotsea montgomery-curve --p P --seed S --index I\n"
        << "  oneshotsea point-count --p P --a A --b B\n"
        << "  oneshotsea schoof-residue --p P --a A --b B --ell L\n"
        << "  oneshotsea schoof-count --p P --a A --b B --max-ell L\n"
        << "  oneshotsea elkies-residue --p P --a A --b B --ell L --file PATH\n"
        << "  oneshotsea elkies-bmss-residue --p P --a A --b B --ell L --file PATH\n"
        << "  oneshotsea elkies-division-residue --p P --a A --b B --ell L\n"
        << "  oneshotsea modpoly --p P --a A --b B --level L --file PATH\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 2;
        }
        const std::string command = argv[1];
        const auto options = parse_options(argc, argv, 2);
        if (command == "curve" || command == "montgomery-curve") {
            const mpz_class p = oneshotsea::parse_integer(required(options, "p"));
            const std::uint64_t seed = required_u64(options, "seed");
            const std::uint64_t index = required_u64(options, "index");
            if (command == "montgomery-curve") {
                const oneshotsea::MontgomeryCurve curve =
                    oneshotsea::deterministic_montgomery_curve(p, seed, index);
                std::cout << "{\"p\":\"" << p << "\",\"seed\":" << seed
                          << ",\"index\":" << index << ",\"A\":\""
                          << curve.coefficient() << "\",\"singular\":"
                          << (curve.is_singular() ? "true" : "false");
                if (!curve.is_singular()) {
                    const oneshotsea::Curve short_curve = curve.short_weierstrass();
                    std::cout << ",\"a\":\"" << short_curve.a() << "\",\"b\":\""
                              << short_curve.b() << "\",\"j\":\""
                              << curve.j_invariant() << "\"";
                }
                std::cout << "}\n";
                return 0;
            }
            const oneshotsea::Curve curve = oneshotsea::deterministic_curve(p, seed, index);
            std::cout << "{\"p\":\"" << p << "\",\"seed\":" << seed
                      << ",\"index\":" << index << ",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"singular\":"
                      << (curve.is_singular() ? "true" : "false");
            if (!curve.is_singular()) {
                std::cout << ",\"j\":\"" << curve.j_invariant() << "\"";
            }
            std::cout << "}\n";
            return 0;
        }
        const mpz_class p = oneshotsea::parse_integer(required(options, "p"));
        oneshotsea::Field field(p);
        oneshotsea::Curve curve(field,
                                oneshotsea::parse_integer(required(options, "a")),
                                oneshotsea::parse_integer(required(options, "b")));
        if (curve.is_singular()) {
            throw std::invalid_argument("input curve is singular");
        }
        if (command == "point-count") {
            const mpz_class count = oneshotsea::count_points_bruteforce(curve);
            const mpz_class trace = p + 1 - count;
            std::cout << "{\"p\":\"" << p << "\",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"order\":\""
                      << count << "\",\"trace\":\"" << trace << "\"}\n";
            return 0;
        }
        if (command == "schoof-residue") {
            const std::uint64_t ell = required_u64(options, "ell");
            const std::uint64_t residue = oneshotsea::schoof_trace_mod_ell(curve, ell);
            std::cout << "{\"p\":\"" << p << "\",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"ell\":" << ell
                      << ",\"trace_residue\":" << residue << "}\n";
            return 0;
        }
        if (command == "schoof-count") {
            const std::uint64_t max_ell = required_u64(options, "max-ell");
            const auto result = oneshotsea::schoof_count_reference(curve, max_ell);
            std::cout << "{\"p\":\"" << p << "\",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"order\":\""
                      << result.order << "\",\"trace\":\"" << result.trace
                      << "\",\"residue_modulus\":\"" << result.residue_modulus
                      << "\",\"levels\":[";
            for (std::size_t index = 0; index < result.levels.size(); ++index) {
                if (index != 0) {
                    std::cout << ',';
                }
                std::cout << result.levels[index];
            }
            std::cout << "]}\n";
            return 0;
        }
        if (command == "elkies-residue" || command == "elkies-bmss-residue" ||
            command == "elkies-division-residue") {
            const std::uint64_t level = required_u64(options, "ell");
            std::vector<oneshotsea::ElkiesKernelResult> kernels;
            if (command == "elkies-residue" ||
                command == "elkies-bmss-residue") {
                if (level > std::numeric_limits<unsigned>::max()) {
                    throw std::invalid_argument("--ell is out of range");
                }
                const auto modular_polynomial =
                    oneshotsea::SparseModularPolynomial::load(
                        static_cast<unsigned>(level), required(options, "file"));
                if (command == "elkies-bmss-residue") {
                    kernels = oneshotsea::elkies_kernels_bmss_reference(
                        curve, modular_polynomial);
                } else {
                    kernels = oneshotsea::elkies_kernels_reference(
                        curve, modular_polynomial);
                }
            } else {
                kernels =
                    oneshotsea::elkies_kernels_division_reference(curve, level);
            }
            std::cout << "{\"p\":\"" << p << "\",\"a\":\"" << curve.a()
                      << "\",\"b\":\"" << curve.b() << "\",\"ell\":"
                      << level << ",\"elkies\":" << (kernels.empty() ? "false" : "true")
                      << ",\"kernel_count\":" << kernels.size();
            if (!kernels.empty()) {
                const std::uint64_t residue = kernels.front().trace_residue;
                for (const auto& kernel : kernels) {
                    if (kernel.trace_residue != residue) {
                        throw std::runtime_error(
                            "Elkies kernels imply inconsistent trace residues");
                    }
                }
                std::cout << ",\"trace_residue\":" << residue;
            }
            std::cout << "}\n";
            return 0;
        }
        if (command == "modpoly") {
            const unsigned level = required_unsigned(options, "level");
            const auto modular_polynomial = oneshotsea::SparseModularPolynomial::load(
                level, required(options, "file"));
            const oneshotsea::Poly specialized = modular_polynomial.evaluate_x(
                field, curve.j_invariant());
            const int root_count = oneshotsea::rational_root_count(specialized);
            std::cout << "{\"p\":\"" << p << "\",\"level\":" << level
                      << ",\"j\":\"" << curve.j_invariant()
                      << "\",\"degree\":" << specialized.degree()
                      << ",\"rational_roots\":" << root_count << "}\n";
            return 0;
        }
        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "oneshotsea: " << error.what() << '\n';
        return 1;
    }
}
