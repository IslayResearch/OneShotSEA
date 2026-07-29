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

class SparseModularPolynomial {
public:
    SparseModularPolynomial(unsigned level, std::vector<BivariateTerm> terms);

    static SparseModularPolynomial load(unsigned level, const std::string& path);
    unsigned level() const { return level_; }
    const std::vector<BivariateTerm>& terms() const { return terms_; }
    Poly evaluate_x(const Field& field, const mpz_class& x) const;

private:
    unsigned level_;
    std::vector<BivariateTerm> terms_;
};

}  // namespace oneshotsea
