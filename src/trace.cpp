#include "oneshotsea/trace.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace oneshotsea {

struct TraceConstraints::LazyCache {
    std::mutex mutex;
    std::optional<std::vector<mpz_class>> residues;
    std::optional<mpz_class> candidate_count;
};

namespace {

struct Matrix2 {
    std::uint64_t a;
    std::uint64_t b;
    std::uint64_t c;
    std::uint64_t d;
};

std::uint64_t mul_mod(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t modulus) {
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(lhs) * static_cast<unsigned __int128>(rhs)) % modulus);
}

std::uint64_t add_mod(std::uint64_t lhs, std::uint64_t rhs,
                      std::uint64_t modulus) {
    return static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(lhs) + rhs) % modulus);
}

std::uint64_t sub_mod(std::uint64_t lhs, std::uint64_t rhs,
                      std::uint64_t modulus) {
    return lhs >= rhs ? lhs - rhs : modulus - (rhs - lhs);
}

std::uint64_t pow_mod(std::uint64_t base, std::uint64_t exponent, std::uint64_t modulus) {
    std::uint64_t result = 1;
    while (exponent != 0) {
        if ((exponent & 1U) != 0U) {
            result = mul_mod(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0) {
            base = mul_mod(base, base, modulus);
        }
    }
    return result;
}

int legendre_small(std::uint64_t value, std::uint64_t prime) {
    value %= prime;
    if (value == 0) {
        return 0;
    }
    const std::uint64_t symbol = pow_mod(value, (prime - 1U) / 2U, prime);
    if (symbol == 1) {
        return 1;
    }
    if (symbol == prime - 1U) {
        return -1;
    }
    throw std::invalid_argument("ell must be prime");
}

bool is_prime_small(std::uint64_t value) {
    if (value < 2U) {
        return false;
    }
    for (std::uint64_t divisor = 2U;
         divisor <= value / divisor; ++divisor) {
        if (value % divisor == 0U) {
            return false;
        }
    }
    return true;
}

Matrix2 matrix_mul(const Matrix2& lhs, const Matrix2& rhs,
                   std::uint64_t modulus) {
    return {
        add_mod(mul_mod(lhs.a, rhs.a, modulus),
                mul_mod(lhs.b, rhs.c, modulus), modulus),
        add_mod(mul_mod(lhs.a, rhs.b, modulus),
                mul_mod(lhs.b, rhs.d, modulus), modulus),
        add_mod(mul_mod(lhs.c, rhs.a, modulus),
                mul_mod(lhs.d, rhs.c, modulus), modulus),
        add_mod(mul_mod(lhs.c, rhs.b, modulus),
                mul_mod(lhs.d, rhs.d, modulus), modulus),
    };
}

Matrix2 matrix_pow(Matrix2 base, std::uint64_t exponent,
                   std::uint64_t modulus) {
    Matrix2 result{1U, 0U, 0U, 1U};
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = matrix_mul(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = matrix_mul(base, base, modulus);
        }
    }
    return result;
}

bool is_scalar(const Matrix2& matrix) {
    return matrix.b == 0U && matrix.c == 0U && matrix.a == matrix.d;
}

std::vector<std::uint64_t> distinct_prime_divisors(std::uint64_t value) {
    std::vector<std::uint64_t> divisors;
    for (std::uint64_t divisor = 2U;
         divisor <= value / divisor; ++divisor) {
        if (value % divisor != 0U) {
            continue;
        }
        divisors.push_back(divisor);
        do {
            value /= divisor;
        } while (value % divisor == 0U);
    }
    if (value > 1U) {
        divisors.push_back(value);
    }
    return divisors;
}

void require_projective_input(std::uint64_t ell, const mpz_class& prime) {
    if (ell < 3U || (ell & 1U) == 0U || !is_prime_small(ell)) {
        throw std::invalid_argument("projective Frobenius requires an odd prime ell");
    }
    if (prime < 2 || mpz_fdiv_ui(prime.get_mpz_t(), ell) == 0U) {
        throw std::invalid_argument(
            "projective Frobenius requires nonzero characteristic modulo ell");
    }
}

mpz_class first_congruent_at_least(const mpz_class& lower, const mpz_class& residue,
                                   const mpz_class& modulus) {
    mpz_class quotient;
    const mpz_class numerator = lower - residue;
    mpz_cdiv_q(quotient.get_mpz_t(), numerator.get_mpz_t(), modulus.get_mpz_t());
    return residue + quotient * modulus;
}

mpz_class size_as_integer(std::size_t value) {
    return mpz_class(std::to_string(value));
}

std::size_t checked_product_size(std::size_t lhs, std::size_t rhs) {
    if (rhs != 0U && lhs > std::numeric_limits<std::size_t>::max() / rhs) {
        throw std::length_error(
            "factored trace constraint exceeds addressable memory");
    }
    return lhs * rhs;
}

std::vector<mpz_class> build_half_residues(
    const mpz_class& modulus,
    const std::vector<std::uint64_t>& component_moduli,
    const std::vector<std::vector<std::uint64_t>>& component_allowed,
    const std::vector<std::size_t>& indices) {
    std::vector<mpz_class> residues{mpz_class(0)};
    for (const std::size_t index : indices) {
        const std::vector<std::uint64_t>& allowed =
            component_allowed[index];
        if (allowed.empty()) {
            return {};
        }
        const std::size_t next_size =
            checked_product_size(residues.size(), allowed.size());
        const mpz_class component_modulus(
            std::to_string(component_moduli[index]));
        const mpz_class quotient = modulus / component_modulus;
        mpz_class inverse;
        const mpz_class quotient_mod_component =
            quotient % component_modulus;
        if (mpz_invert(inverse.get_mpz_t(),
                       quotient_mod_component.get_mpz_t(),
                       component_modulus.get_mpz_t()) == 0) {
            throw std::logic_error(
                "factored trace CRT inverse does not exist");
        }
        std::vector<mpz_class> contributions;
        contributions.reserve(allowed.size());
        for (const std::uint64_t small_residue : allowed) {
            mpz_class contribution = quotient * inverse *
                mpz_class(std::to_string(small_residue));
            mpz_mod(contribution.get_mpz_t(), contribution.get_mpz_t(),
                    modulus.get_mpz_t());
            contributions.push_back(std::move(contribution));
        }

        std::vector<mpz_class> next;
        next.reserve(next_size);
        for (const mpz_class& residue : residues) {
            for (const mpz_class& contribution : contributions) {
                mpz_class combined = residue + contribution;
                if (combined >= modulus) {
                    combined -= modulus;
                }
                next.push_back(std::move(combined));
            }
        }
        residues = std::move(next);
    }
    std::sort(residues.begin(), residues.end());
    if (std::adjacent_find(residues.begin(), residues.end()) !=
        residues.end()) {
        throw std::logic_error(
            "factored trace CRT produced duplicate half residues");
    }
    return residues;
}

struct MeetInTheMiddleResidues {
    std::vector<mpz_class> left;
    std::vector<mpz_class> right;
};

MeetInTheMiddleResidues build_meet_in_the_middle(
    const mpz_class& modulus,
    const std::vector<std::uint64_t>& component_moduli,
    const std::vector<std::vector<std::uint64_t>>& component_allowed) {
    if (component_moduli.size() != component_allowed.size()) {
        throw std::logic_error(
            "factored trace constraint metadata disagrees");
    }
    std::vector<std::size_t> order(component_moduli.size());
    for (std::size_t index = 0U; index < order.size(); ++index) {
        order[index] = index;
    }
    std::stable_sort(
        order.begin(), order.end(),
        [&component_allowed](std::size_t lhs, std::size_t rhs) {
            return component_allowed[lhs].size() >
                component_allowed[rhs].size();
        });

    std::vector<std::size_t> left_indices;
    std::vector<std::size_t> right_indices;
    mpz_class left_size = 1;
    mpz_class right_size = 1;
    for (const std::size_t index : order) {
        const mpz_class factor = size_as_integer(
            component_allowed[index].size());
        if (left_size <= right_size) {
            left_indices.push_back(index);
            left_size *= factor;
        } else {
            right_indices.push_back(index);
            right_size *= factor;
        }
    }
    return {
        build_half_residues(modulus, component_moduli,
                            component_allowed, left_indices),
        build_half_residues(modulus, component_moduli,
                            component_allowed, right_indices),
    };
}

std::size_t sorted_range_size(const std::vector<mpz_class>& values,
                              const mpz_class& lower,
                              const mpz_class& upper) {
    if (lower > upper) {
        return 0U;
    }
    const auto begin = std::lower_bound(values.begin(), values.end(), lower);
    const auto end = std::upper_bound(begin, values.end(), upper);
    return static_cast<std::size_t>(end - begin);
}

std::size_t count_shifted_range(
    const std::vector<mpz_class>& sorted_values,
    const mpz_class& modulus,
    const mpz_class& shift,
    const mpz_class& target_lower,
    const mpz_class& target_upper) {
    const mpz_class width = target_upper - target_lower;
    mpz_class shifted_lower = target_lower - shift;
    mpz_mod(shifted_lower.get_mpz_t(), shifted_lower.get_mpz_t(),
            modulus.get_mpz_t());
    const mpz_class unreduced_upper = shifted_lower + width;
    if (unreduced_upper < modulus) {
        return sorted_range_size(
            sorted_values, shifted_lower, unreduced_upper);
    }
    const std::size_t high = sorted_range_size(
        sorted_values, shifted_lower, modulus - 1);
    const std::size_t low = sorted_range_size(
        sorted_values, 0, unreduced_upper - modulus);
    if (low > std::numeric_limits<std::size_t>::max() - high) {
        throw std::overflow_error("factored trace range count overflow");
    }
    return high + low;
}

template <typename Visitor>
void visit_shifted_range(const std::vector<mpz_class>& sorted_values,
                         const mpz_class& modulus,
                         const mpz_class& shift,
                         const mpz_class& target_lower,
                         const mpz_class& target_upper,
                         const Visitor& visitor) {
    const mpz_class width = target_upper - target_lower;
    mpz_class shifted_lower = target_lower - shift;
    mpz_mod(shifted_lower.get_mpz_t(), shifted_lower.get_mpz_t(),
            modulus.get_mpz_t());
    const mpz_class unreduced_upper = shifted_lower + width;
    const auto visit_linear = [&](const mpz_class& lower,
                                  const mpz_class& upper) {
        const auto begin =
            std::lower_bound(sorted_values.begin(), sorted_values.end(), lower);
        const auto end =
            std::upper_bound(begin, sorted_values.end(), upper);
        for (auto current = begin; current != end; ++current) {
            visitor(*current);
        }
    };
    if (unreduced_upper < modulus) {
        visit_linear(shifted_lower, unreduced_upper);
        return;
    }
    visit_linear(shifted_lower, modulus - 1);
    visit_linear(0, unreduced_upper - modulus);
}

mpz_class count_pairs_in_range(const MeetInTheMiddleResidues& halves,
                               const mpz_class& modulus,
                               const mpz_class& target_lower,
                               const mpz_class& target_upper) {
    mpz_class count = 0;
    for (const mpz_class& left : halves.left) {
        count += size_as_integer(count_shifted_range(
            halves.right, modulus, left, target_lower, target_upper));
    }
    return count;
}

mpz_class factored_cardinality(
    const std::vector<std::vector<std::uint64_t>>& component_allowed) {
    mpz_class count = 1;
    for (const auto& allowed : component_allowed) {
        count *= size_as_integer(allowed.size());
    }
    return count;
}

}  // namespace

TraceConstraints::TraceConstraints(mpz_class prime)
    : prime_(std::move(prime)), modulus_(1),
      cache_(std::make_shared<LazyCache>()) {
    if (prime_ <= 3 || mpz_even_p(prime_.get_mpz_t()) != 0) {
        throw std::invalid_argument("trace constraints require an odd p greater than 3");
    }
    const mpz_class four_p = 4 * prime_;
    mpz_sqrt(hasse_radius_.get_mpz_t(), four_p.get_mpz_t());
}

void TraceConstraints::refine(std::uint64_t ell,
                              const std::vector<std::uint64_t>& allowed) {
    if (ell < 2) {
        throw std::invalid_argument("invalid SEA modulus");
    }
    if (ell > std::numeric_limits<unsigned long>::max()) {
        throw std::invalid_argument("SEA modulus does not fit unsigned long");
    }
    const mpz_class ell_integer(static_cast<unsigned long>(ell));
    mpz_class common_divisor;
    mpz_gcd(common_divisor.get_mpz_t(), modulus_.get_mpz_t(), ell_integer.get_mpz_t());
    if (common_divisor != 1) {
        throw std::invalid_argument("SEA modulus is not coprime to accumulated modulus");
    }
    std::vector<std::uint64_t> canonical_allowed;
    canonical_allowed.reserve(allowed.size());
    for (const std::uint64_t residue : allowed) {
        canonical_allowed.push_back(residue % ell);
    }
    std::sort(canonical_allowed.begin(), canonical_allowed.end());
    canonical_allowed.erase(
        std::unique(canonical_allowed.begin(), canonical_allowed.end()),
        canonical_allowed.end());

    const mpz_class new_modulus = modulus_ * ell_integer;
    std::vector<std::uint64_t> next_moduli = component_moduli_;
    std::vector<std::vector<std::uint64_t>> next_allowed =
        component_allowed_;
    next_moduli.push_back(ell);
    next_allowed.push_back(std::move(canonical_allowed));
    std::shared_ptr<LazyCache> next_cache = std::make_shared<LazyCache>();
    modulus_ = new_modulus;
    component_moduli_ = std::move(next_moduli);
    component_allowed_ = std::move(next_allowed);
    cache_ = std::move(next_cache);
}

void TraceConstraints::refine_exact(std::uint64_t ell,
                                    std::uint64_t residue) {
    TraceConstraints updated = *this;
    updated.refine(ell, {residue});
    if (updated.candidate_count() == 0) {
        throw std::runtime_error(
            "exact trace residue eliminated every Hasse-compatible trace");
    }
    *this = std::move(updated);
}

mpz_class TraceConstraints::candidate_count() const {
    std::lock_guard<std::mutex> cache_lock(cache_->mutex);
    if (cache_->candidate_count.has_value()) {
        return *cache_->candidate_count;
    }
    const mpz_class residue_count =
        factored_cardinality(component_allowed_);
    if (residue_count == 0) {
        cache_->candidate_count = 0;
        return 0;
    }
    const mpz_class lower = -hasse_radius_;
    const mpz_class upper = hasse_radius_;
    const mpz_class interval_size = upper - lower + 1;
    mpz_class complete_cycles;
    mpz_class remainder;
    mpz_fdiv_qr(complete_cycles.get_mpz_t(), remainder.get_mpz_t(),
                interval_size.get_mpz_t(), modulus_.get_mpz_t());
    mpz_class count = complete_cycles * residue_count;
    if (remainder != 0) {
        const MeetInTheMiddleResidues halves = build_meet_in_the_middle(
            modulus_, component_moduli_, component_allowed_);
        mpz_class start = lower;
        mpz_mod(start.get_mpz_t(), start.get_mpz_t(), modulus_.get_mpz_t());
        const mpz_class unreduced_end = start + remainder - 1;
        if (unreduced_end < modulus_) {
            count += count_pairs_in_range(
                halves, modulus_, start, unreduced_end);
        } else {
            count += count_pairs_in_range(
                halves, modulus_, start, modulus_ - 1);
            count += count_pairs_in_range(
                halves, modulus_, 0, unreduced_end - modulus_);
        }
    }
    cache_->candidate_count = count;
    return count;
}

std::optional<std::vector<mpz_class>> TraceConstraints::enumerate(std::size_t cap) const {
    const mpz_class count = candidate_count();
    if (!mpz_fits_ulong_p(count.get_mpz_t()) || count.get_ui() > cap) {
        return std::nullopt;
    }
    const std::size_t count_size = static_cast<std::size_t>(count.get_ui());
    std::vector<mpz_class> traces;
    traces.reserve(count_size);
    const mpz_class lower = -hasse_radius_;
    const mpz_class upper = hasse_radius_;
    const MeetInTheMiddleResidues halves = build_meet_in_the_middle(
        modulus_, component_moduli_, component_allowed_);
    const mpz_class interval_size = upper - lower + 1;
    if (interval_size >= modulus_) {
        // If the bounded result fits cap while the interval spans a complete
        // modulus cycle, then the full CRT residue set itself also fits cap.
        for (const mpz_class& left : halves.left) {
            for (const mpz_class& right : halves.right) {
                mpz_class residue = left + right;
                if (residue >= modulus_) {
                    residue -= modulus_;
                }
                for (mpz_class trace = first_congruent_at_least(
                         lower, residue, modulus_);
                     trace <= upper; trace += modulus_) {
                    traces.push_back(trace);
                }
            }
        }
    } else {
        mpz_class start = lower;
        mpz_mod(start.get_mpz_t(), start.get_mpz_t(), modulus_.get_mpz_t());
        const mpz_class unreduced_end = start + interval_size - 1;
        const auto visit_target = [&](const mpz_class& target_lower,
                                      const mpz_class& target_upper) {
            for (const mpz_class& left : halves.left) {
                visit_shifted_range(
                    halves.right, modulus_, left,
                    target_lower, target_upper,
                    [&](const mpz_class& right) {
                        mpz_class residue = left + right;
                        if (residue >= modulus_) {
                            residue -= modulus_;
                        }
                        const mpz_class trace = first_congruent_at_least(
                            lower, residue, modulus_);
                        if (trace < lower || trace > upper) {
                            throw std::logic_error(
                                "bounded trace range search escaped the Hasse interval");
                        }
                        traces.push_back(trace);
                    });
            }
        };
        if (unreduced_end < modulus_) {
            visit_target(start, unreduced_end);
        } else {
            visit_target(start, modulus_ - 1);
            visit_target(0, unreduced_end - modulus_);
        }
    }
    std::sort(traces.begin(), traces.end());
    if (traces.size() != count_size ||
        std::adjacent_find(traces.begin(), traces.end()) != traces.end()) {
        throw std::logic_error(
            "bounded factored trace enumeration disagrees with its exact count");
    }
    return traces;
}

const std::vector<mpz_class>& TraceConstraints::residues() const {
    std::lock_guard<std::mutex> cache_lock(cache_->mutex);
    if (cache_->residues.has_value()) {
        return *cache_->residues;
    }
    const MeetInTheMiddleResidues halves = build_meet_in_the_middle(
        modulus_, component_moduli_, component_allowed_);
    const std::size_t full_size =
        checked_product_size(halves.left.size(), halves.right.size());
    std::vector<mpz_class> materialized;
    materialized.reserve(full_size);
    for (const mpz_class& left : halves.left) {
        for (const mpz_class& right : halves.right) {
            mpz_class residue = left + right;
            if (residue >= modulus_) {
                residue -= modulus_;
            }
            materialized.push_back(std::move(residue));
        }
    }
    std::sort(materialized.begin(), materialized.end());
    if (std::adjacent_find(materialized.begin(), materialized.end()) !=
        materialized.end()) {
        throw std::logic_error(
            "factored trace CRT produced duplicate residues");
    }
    cache_->residues = std::move(materialized);
    return *cache_->residues;
}

std::vector<std::uint64_t> trace_residues_from_classification(
    std::uint64_t ell, const mpz_class& prime, bool has_rational_isogeny) {
    if (ell < 3 || (ell & 1U) == 0U) {
        throw std::invalid_argument("classification requires an odd prime ell");
    }
    const std::uint64_t p_mod_ell = mpz_fdiv_ui(prime.get_mpz_t(), ell);
    std::vector<std::uint64_t> residues;
    for (std::uint64_t trace = 0; trace < ell; ++trace) {
        const std::uint64_t trace_squared = mul_mod(trace, trace, ell);
        const std::uint64_t four_p = mul_mod(4U % ell, p_mod_ell, ell);
        const std::uint64_t discriminant = (trace_squared + ell - four_p) % ell;
        const int symbol = legendre_small(discriminant, ell);
        if ((has_rational_isogeny && symbol >= 0) ||
            (!has_rational_isogeny && symbol < 0)) {
            residues.push_back(trace);
        }
    }
    return residues;
}

std::uint64_t projective_frobenius_order(
    std::uint64_t ell, const mpz_class& prime,
    std::uint64_t trace_residue) {
    require_projective_input(ell, prime);
    trace_residue %= ell;
    const std::uint64_t p_mod_ell = mpz_fdiv_ui(prime.get_mpz_t(), ell);
    const std::uint64_t discriminant = sub_mod(
        mul_mod(trace_residue, trace_residue, ell),
        mul_mod(4U % ell, p_mod_ell, ell), ell);
    const int character = legendre_small(discriminant, ell);

    // A semisimple nonsplit/split element has projective order dividing
    // ell+1/ell-1 respectively.  A non-scalar repeated-root companion matrix
    // is unipotent in PGL and has order ell.
    std::uint64_t order =
        character < 0 ? ell + 1U : (character > 0 ? ell - 1U : ell);
    const Matrix2 frobenius{
        0U, (ell - p_mod_ell) % ell, 1U, trace_residue};
    if (!is_scalar(matrix_pow(frobenius, order, ell))) {
        throw std::logic_error("projective Frobenius order bound failed");
    }
    for (const std::uint64_t divisor : distinct_prime_divisors(order)) {
        while (order % divisor == 0U &&
               is_scalar(matrix_pow(frobenius, order / divisor, ell))) {
            order /= divisor;
        }
    }
    return order;
}

std::vector<std::uint64_t> atkin_trace_residues_from_projective_order(
    std::uint64_t ell, const mpz_class& prime,
    std::uint64_t projective_order) {
    require_projective_input(ell, prime);
    if (projective_order < 2U || (ell + 1U) % projective_order != 0U) {
        throw std::invalid_argument(
            "Atkin projective order must divide ell+1 and be at least two");
    }
    const std::uint64_t p_mod_ell = mpz_fdiv_ui(prime.get_mpz_t(), ell);
    std::vector<std::uint64_t> residues;
    for (std::uint64_t trace = 0U; trace < ell; ++trace) {
        const std::uint64_t discriminant = sub_mod(
            mul_mod(trace, trace, ell),
            mul_mod(4U % ell, p_mod_ell, ell), ell);
        if (legendre_small(discriminant, ell) < 0 &&
            projective_frobenius_order(ell, prime, trace) ==
                projective_order) {
            residues.push_back(trace);
        }
    }
    return residues;
}

}  // namespace oneshotsea
