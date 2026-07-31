#include "oneshotsea/smooth_bounded.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <omp.h>

namespace oneshotsea {
namespace {

constexpr std::size_t kMaximumBlockItems = 65536U;

std::size_t checked_power_of_two_ceiling(std::size_t value) {
    std::size_t result = 1U;
    while (result < value) {
        if (result > std::numeric_limits<std::size_t>::max() / 2U) {
            throw std::length_error("smooth batch padding overflows size_t");
        }
        result *= 2U;
    }
    return result;
}

void multiply_mod(mpz_class& output, const mpz_class& left,
                  const mpz_class& right, const mpz_class& modulus,
                  mpz_class& scratch) {
    // Reducing from a separate temporary matters for the memory bound.  GMP
    // does not normally release an mpz allocation after an in-place mod, so
    // multiplying into output would leave every table entry sized for the
    // unreduced two-modulus-limb product.
    mpz_mul(scratch.get_mpz_t(), left.get_mpz_t(), right.get_mpz_t());
    mpz_mod(output.get_mpz_t(), scratch.get_mpz_t(), modulus.get_mpz_t());
}

std::vector<std::vector<mpz_class>> product_tree(
    std::span<const mpz_class> orders, int thread_count) {
    const std::size_t padded = checked_power_of_two_ceiling(orders.size());
    std::vector<std::vector<mpz_class>> levels;
    levels.emplace_back(padded, 1);
#pragma omp parallel for num_threads(thread_count) schedule(static)
    for (std::size_t index = 0; index < orders.size(); ++index) {
        levels.front()[index] = orders[index];
    }
    while (levels.back().size() != 1U) {
        const std::vector<mpz_class>& previous = levels.back();
        std::vector<mpz_class> next(previous.size() / 2U);
#pragma omp parallel for num_threads(thread_count) schedule(dynamic, 64)
        for (std::size_t index = 0; index < next.size(); ++index) {
            mpz_mul(next[index].get_mpz_t(),
                    previous[2U * index].get_mpz_t(),
                    previous[2U * index + 1U].get_mpz_t());
        }
        levels.push_back(std::move(next));
    }
    return levels;
}

std::size_t block_capacity(std::size_t modulus_limbs,
                           std::size_t max_auxiliary_bytes,
                           std::size_t chunk_count) {
    if (modulus_limbs == 0U) {
        throw std::logic_error("smooth root product has no limbs");
    }
    if (modulus_limbs > std::numeric_limits<std::size_t>::max() /
                            (2U * sizeof(mp_limb_t))) {
        throw std::overflow_error("smooth root limb size overflows size_t");
    }
    const std::size_t bytes_per_item =
        2U * modulus_limbs * sizeof(mp_limb_t);
    const std::size_t by_memory = max_auxiliary_bytes / bytes_per_item;
    if (by_memory == 0U) {
        throw std::invalid_argument(
            "smooth root auxiliary byte cap is too small for one block item");
    }
    return std::min({by_memory, kMaximumBlockItems, chunk_count});
}

std::vector<mpz_class> local_powers(
    const mpz_class& radix, const mpz_class& modulus,
    std::size_t count, int thread_count) {
    std::vector<mpz_class> powers(count, 1);
    if (count == 1U) {
        return powers;
    }
    powers[1] = radix;
    mpz_class serial_scratch;
    for (std::size_t midpoint = 2U; midpoint < count;
         midpoint *= 2U) {
        multiply_mod(powers[midpoint], powers[midpoint / 2U],
                     powers[midpoint / 2U], modulus, serial_scratch);
        const std::size_t end = std::min(2U * midpoint, count);
#pragma omp parallel num_threads(thread_count)
        {
            mpz_class scratch;
#pragma omp for schedule(dynamic, 1)
            for (std::size_t index = midpoint + 1U; index < end; ++index) {
                multiply_mod(powers[index], powers[midpoint],
                             powers[index - midpoint], modulus, scratch);
            }
        }
    }
    return powers;
}

mpz_class bounded_root_remainder(
    const smooth_base& base, const mpz_class& modulus,
    int thread_count, std::size_t max_auxiliary_bytes) {
    const std::size_t modulus_limbs = mpz_size(modulus.get_mpz_t());
    if (modulus_limbs >= static_cast<std::size_t>(
                             std::numeric_limits<mp_size_t>::max())) {
        throw std::overflow_error("smooth root chunk size overflows mp_size_t");
    }
    const std::size_t chunk_limbs = modulus_limbs + 1U;
    const std::size_t product_limbs = mpz_size(base.P);
    if (product_limbs == 0U || mpz_sgn(base.P) <= 0) {
        throw std::invalid_argument("smooth prime product is not positive");
    }
    const std::size_t chunks =
        product_limbs / chunk_limbs +
        static_cast<std::size_t>(product_limbs % chunk_limbs != 0U);
    const std::size_t capacity = block_capacity(
        modulus_limbs, max_auxiliary_bytes, chunks);

    mpz_class radix = 1;
    if (chunk_limbs > std::numeric_limits<mp_bitcnt_t>::max() /
                          static_cast<mp_bitcnt_t>(GMP_NUMB_BITS)) {
        throw std::overflow_error("smooth root radix exponent overflows");
    }
    const mp_bitcnt_t exponent =
        static_cast<mp_bitcnt_t>(chunk_limbs) *
        static_cast<mp_bitcnt_t>(GMP_NUMB_BITS);
    mpz_mul_2exp(radix.get_mpz_t(), radix.get_mpz_t(), exponent);
    mpz_mod(radix.get_mpz_t(), radix.get_mpz_t(), modulus.get_mpz_t());
    std::vector<mpz_class> powers = local_powers(
        radix, modulus, capacity, thread_count);
    mpz_class block_factor;
    mpz_powm_ui(block_factor.get_mpz_t(), radix.get_mpz_t(),
                static_cast<unsigned long>(capacity), modulus.get_mpz_t());

    std::vector<mpz_class> parts(capacity);
    mpz_class offset = 1;
    mpz_class result = 0;
    mp_srcptr product_data = mpz_limbs_read(base.P);
    for (std::size_t begin = 0; begin < chunks; begin += capacity) {
        const std::size_t count = std::min(capacity, chunks - begin);
#pragma omp parallel num_threads(thread_count)
        {
            mpz_class scratch;
#pragma omp for schedule(static)
            for (std::size_t local = 0; local < count; ++local) {
                mpz_set_ui(parts[local].get_mpz_t(), 0U);
                const std::size_t chunk_index = begin + local;
                const std::size_t lower = chunk_index * chunk_limbs;
                if (lower >= product_limbs) {
                    continue;
                }
                std::size_t length =
                    std::min(chunk_limbs, product_limbs - lower);
                while (length > 0 &&
                       product_data[lower + length - 1] == 0U) {
                    --length;
                }
                if (length == 0) {
                    continue;
                }
                mpz_t view;
                mpz_roinit_n(view, product_data + lower,
                             static_cast<mp_size_t>(length));
                mpz_mod(parts[local].get_mpz_t(), view,
                        modulus.get_mpz_t());
                if (local != 0U) {
                    multiply_mod(parts[local], parts[local], powers[local],
                                 modulus, scratch);
                }
            }
        }

        mpz_class block_sum = 0;
        for (std::size_t local = 0; local < count; ++local) {
            block_sum += parts[local];
        }
        mpz_mod(block_sum.get_mpz_t(), block_sum.get_mpz_t(),
                modulus.get_mpz_t());
        if (begin != 0U) {
            mpz_class scratch;
            multiply_mod(block_sum, block_sum, offset, modulus, scratch);
        }
        result += block_sum;
        mpz_mod(result.get_mpz_t(), result.get_mpz_t(),
                modulus.get_mpz_t());
        mpz_class scratch;
        multiply_mod(offset, offset, block_factor, modulus, scratch);
    }
    return result;
}

mpz_class smooth_part_from_remainder(const mpz_class& order,
                                     const mpz_class& remainder) {
    if (remainder == 0) {
        return order;
    }
    const std::size_t bit_length = mpz_sizeinbase(order.get_mpz_t(), 2);
    std::size_t covered = 1U;
    unsigned squarings = 0U;
    while (covered < bit_length) {
        if (covered > std::numeric_limits<std::size_t>::max() / 2U) {
            throw std::overflow_error("smooth order bit length is excessive");
        }
        covered *= 2U;
        ++squarings;
    }
    mpz_class powered = remainder;
    mpz_class scratch;
    for (unsigned index = 0; index < squarings; ++index) {
        multiply_mod(powered, powered, powered, order, scratch);
    }
    mpz_class result;
    mpz_gcd(result.get_mpz_t(), powered.get_mpz_t(), order.get_mpz_t());
    return result;
}

}  // namespace

std::vector<mpz_class> bounded_smooth_parts(
    const smooth_base& base, std::span<const mpz_class> orders,
    int thread_count, std::size_t max_auxiliary_bytes) {
    if (orders.empty()) {
        return {};
    }
    if (thread_count < 0) {
        throw std::invalid_argument("smooth thread count must be nonnegative");
    }
    if (thread_count == 0) {
        thread_count = omp_get_max_threads();
    }
    if (thread_count <= 0 || max_auxiliary_bytes == 0U) {
        throw std::invalid_argument("invalid bounded smooth-part resource cap");
    }
    for (const mpz_class& order : orders) {
        if (order <= 1) {
            throw std::invalid_argument("smooth-part order must exceed one");
        }
    }

    std::vector<std::vector<mpz_class>> levels =
        product_tree(orders, thread_count);
    const mpz_class root_remainder = bounded_root_remainder(
        base, levels.back().front(), thread_count, max_auxiliary_bytes);

    std::vector<mpz_class> remainders(1U, root_remainder);
    for (std::size_t depth = levels.size() - 1U; depth != 0U; --depth) {
        const std::vector<mpz_class>& moduli = levels[depth - 1U];
        std::vector<mpz_class> children(moduli.size());
#pragma omp parallel for num_threads(thread_count) schedule(dynamic, 64)
        for (std::size_t index = 0; index < moduli.size(); ++index) {
            mpz_mod(children[index].get_mpz_t(),
                    remainders[index / 2U].get_mpz_t(),
                    moduli[index].get_mpz_t());
        }
        remainders = std::move(children);
    }

    std::vector<mpz_class> result(orders.size());
#pragma omp parallel for num_threads(thread_count) schedule(dynamic, 32)
    for (std::size_t index = 0; index < orders.size(); ++index) {
        result[index] = smooth_part_from_remainder(
            orders[index], remainders[index]);
    }
    return result;
}

}  // namespace oneshotsea
