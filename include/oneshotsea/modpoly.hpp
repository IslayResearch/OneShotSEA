#pragma once

#include "oneshotsea/poly.hpp"

#include <string>
#include <vector>

namespace oneshotsea {

struct BivariateTerm {
    unsigned x_degree;
    unsigned y_degree;
    mpz_class coefficient;
};

struct BivariateEvaluation {
    mpz_class value;
    mpz_class x_derivative;
    mpz_class y_derivative;
};

class SparseModularPolynomial {
public:
    SparseModularPolynomial(unsigned level, std::vector<BivariateTerm> terms);

    static SparseModularPolynomial load(unsigned level, const std::string& path);
    unsigned level() const { return level_; }
    const std::vector<BivariateTerm>& terms() const { return terms_; }
    Poly evaluate_x(const Field& field, const mpz_class& x) const;
    BivariateEvaluation evaluate_with_derivatives(
        const Field& field, const mpz_class& x, const mpz_class& y) const;

private:
    unsigned level_;
    std::vector<BivariateTerm> terms_;
};

}  // namespace oneshotsea
