#pragma once

#include "oneshotsea/smooth_cache.hpp"

#include <cstddef>
#include <span>
#include <vector>

#include <gmpxx.h>

namespace oneshotsea {

// Compute exact smooth parts against one immutable squarefree prime product.
// The root reduction is algebraically identical to smooth_parts, but processes
// product limbs in reusable blocks so auxiliary GMP storage is bounded by
// max_auxiliary_bytes rather than by the size of base.P.
std::vector<mpz_class> bounded_smooth_parts(
    const smooth_base& base, std::span<const mpz_class> orders,
    int thread_count, std::size_t max_auxiliary_bytes);

}  // namespace oneshotsea
