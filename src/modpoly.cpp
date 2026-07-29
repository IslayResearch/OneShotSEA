#include "oneshotsea/modpoly.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace oneshotsea {

SparseModularPolynomial::SparseModularPolynomial(
    unsigned level, std::vector<BivariateTerm> terms)
    : level_(level), terms_(std::move(terms)) {
    if (level_ < 2 || terms_.empty()) {
        throw std::invalid_argument("invalid sparse modular polynomial");
    }
}

SparseModularPolynomial SparseModularPolynomial::load(unsigned level,
                                                       const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open modular polynomial: " + path);
    }
    std::vector<BivariateTerm> terms;
    std::string line;
    unsigned line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        std::istringstream row(line);
        unsigned x_degree = 0;
        unsigned y_degree = 0;
        std::string coefficient;
        if (!(row >> x_degree)) {
            continue;
        }
        if (!(row >> y_degree >> coefficient)) {
            throw std::runtime_error("malformed modular polynomial line " +
                                     std::to_string(line_number));
        }
        std::string extra;
        if (row >> extra) {
            throw std::runtime_error("extra data on modular polynomial line " +
                                     std::to_string(line_number));
        }
        terms.push_back({x_degree, y_degree, parse_integer(coefficient)});
    }
    return SparseModularPolynomial(level, std::move(terms));
}

Poly SparseModularPolynomial::evaluate_x(const Field& field, const mpz_class& x) const {
    unsigned maximum_y = 0;
    for (const auto& term : terms_) {
        if (term.y_degree > maximum_y) {
            maximum_y = term.y_degree;
        }
    }
    std::vector<mpz_class> coefficients(static_cast<std::size_t>(maximum_y) + 1U, 0);
    for (const auto& term : terms_) {
        const mpz_class x_power = field.pow(x, term.x_degree);
        coefficients[term.y_degree] = field.add(
            coefficients[term.y_degree], field.mul(term.coefficient, x_power));
    }
    return Poly(field, std::move(coefficients));
}

BivariateEvaluation SparseModularPolynomial::evaluate_with_derivatives(
    const Field& field, const mpz_class& x, const mpz_class& y) const {
    unsigned maximum_x = 0;
    unsigned maximum_y = 0;
    for (const BivariateTerm& term : terms_) {
        maximum_x = std::max(maximum_x, term.x_degree);
        maximum_y = std::max(maximum_y, term.y_degree);
    }
    std::vector<mpz_class> x_powers(static_cast<std::size_t>(maximum_x) + 1U, 1);
    std::vector<mpz_class> y_powers(static_cast<std::size_t>(maximum_y) + 1U, 1);
    for (std::size_t index = 1; index < x_powers.size(); ++index) {
        x_powers[index] = field.mul(x_powers[index - 1U], x);
    }
    for (std::size_t index = 1; index < y_powers.size(); ++index) {
        y_powers[index] = field.mul(y_powers[index - 1U], y);
    }

    BivariateEvaluation result{0, 0, 0};
    for (const BivariateTerm& term : terms_) {
        const mpz_class coefficient = field.normalize(term.coefficient);
        result.value = field.add(
            result.value,
            field.mul(coefficient,
                      field.mul(x_powers[term.x_degree],
                                y_powers[term.y_degree])));
        if (term.x_degree != 0U) {
            result.x_derivative = field.add(
                result.x_derivative,
                field.mul(field.mul(coefficient, term.x_degree),
                          field.mul(x_powers[term.x_degree - 1U],
                                    y_powers[term.y_degree])));
        }
        if (term.y_degree != 0U) {
            result.y_derivative = field.add(
                result.y_derivative,
                field.mul(field.mul(coefficient, term.y_degree),
                          field.mul(x_powers[term.x_degree],
                                    y_powers[term.y_degree - 1U])));
        }
    }
    return result;
}

}  // namespace oneshotsea
