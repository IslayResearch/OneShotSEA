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

// The exact output boundary required from a direct modular-polynomial
// evaluator.  Sutherland's Algorithm 1 computes Phi_ell(x,Y) and optionally
// Phi_X(x,Y); Phi_Y(x,Y) is then the ordinary derivative in Y.  Keeping this
// object independent of a bivariate coefficient table lets a CRT/volcano
// backend feed the existing Elkies pipeline without materializing Phi_ell.
class ModularPolynomialSpecialization {
public:
    ModularPolynomialSpecialization(unsigned level, mpz_class source_x,
                                    Poly value, Poly x_derivative);

    unsigned level() const { return level_; }
    const mpz_class& source_x() const { return source_x_; }
    const Poly& value() const { return value_; }
    const Poly& x_derivative() const { return x_derivative_; }
    const Poly& y_derivative() const { return y_derivative_; }
    BivariateEvaluation evaluate_with_derivatives(const mpz_class& y) const;

private:
    unsigned level_;
    mpz_class source_x_;
    Poly value_;
    Poly x_derivative_;
    Poly y_derivative_;
};

class SparseModularPolynomial {
public:
    SparseModularPolynomial(unsigned level, std::vector<BivariateTerm> terms);

    static SparseModularPolynomial load(unsigned level, const std::string& path);
    unsigned level() const { return level_; }
    const std::vector<BivariateTerm>& terms() const { return terms_; }
    Poly evaluate_x(const Field& field, const mpz_class& x) const;
    // Table-backed reference implementation of the direct-specialization
    // boundary.  It computes Phi_ell(x,Y) and Phi_X(x,Y) together in one pass.
    ModularPolynomialSpecialization specialize_x_with_derivative(
        const Field& field, const mpz_class& x) const;
    BivariateEvaluation evaluate_with_derivatives(
        const Field& field, const mpz_class& x, const mpz_class& y) const;

private:
    unsigned level_;
    std::vector<BivariateTerm> terms_;
};

}  // namespace oneshotsea
