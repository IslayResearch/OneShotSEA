#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <gmp.h>

#include "smooth.h"

static int failures;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void test_sieve(void)
{
    static const uint64_t expected[] = {11, 13, 17, 19, 23, 29};
    uint64_t *primes = NULL;
    uint64_t count = sieve_primes_range(10, 30, &primes, 3);

    check(count == sizeof(expected) / sizeof(expected[0]),
          "sieve count for (10, 30]");
    if (count == sizeof(expected) / sizeof(expected[0])) {
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            if (primes[i] != expected[i]) {
                fprintf(stderr,
                        "FAIL: sieve prime[%zu], expected %" PRIu64
                        ", got %" PRIu64 "\n",
                        i, expected[i], primes[i]);
                failures++;
            }
        }
    }
    free(primes);
}

static void test_bases_and_smooth_parts(void)
{
    static const unsigned long primes_through_97[] = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41,
        43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97,
    };
    static const char *inputs[] = {
        "2094336",       /* 2^8 * 3^4 * 101 */
        "15142609375",   /* 5^6 * 97^2 * 103 */
        "10403",         /* 101 * 103 */
        "351232",        /* 2^10 * 7^3 */
    };
    static const char *expected_parts[] = {
        "20736",         /* 2^8 * 3^4 */
        "147015625",     /* 5^6 * 97^2 */
        "1",
        "351232",
    };
    enum { batch_size = sizeof(inputs) / sizeof(inputs[0]) };
    smooth_base full;
    smooth_base segments[2];
    mpz_t expected_product;
    mpz_t inputs_z[batch_size];
    mpz_t parts[batch_size];
    mpz_t multi_parts[batch_size];
    mpz_t expected_part;

    smooth_base_build(&full, 100, 3);
    check(full.lo == 0 && full.y == 100, "full smooth-base range metadata");
    check(full.nprimes == 25, "full smooth-base prime count");
    check(smooth_base_selfcheck(&full) == 1, "full smooth-base selfcheck");

    mpz_init_set_ui(expected_product, 1);
    for (size_t i = 0;
         i < sizeof(primes_through_97) / sizeof(primes_through_97[0]);
         i++) {
        mpz_mul_ui(expected_product, expected_product, primes_through_97[i]);
    }
    check(mpz_cmp(full.P, expected_product) == 0,
          "full smooth-base exact prime product");

    smooth_base_build_range(&segments[0], 0, 47, 2);
    smooth_base_build_range(&segments[1], 47, 100, 2);
    check(smooth_base_selfcheck(&segments[0]) == 1,
          "lower segment selfcheck");
    check(smooth_base_selfcheck(&segments[1]) == 1,
          "upper segment selfcheck");
    mpz_mul(expected_product, segments[0].P, segments[1].P);
    check(mpz_cmp(full.P, expected_product) == 0,
          "disjoint segment products reconstruct full base");

    for (size_t i = 0; i < batch_size; i++) {
        mpz_init_set_str(inputs_z[i], inputs[i], 10);
        mpz_init(parts[i]);
        mpz_init(multi_parts[i]);
    }
    smooth_parts(&full, inputs_z, batch_size, parts, 3);
    smooth_parts_multi(segments, 2, inputs_z, batch_size, multi_parts, 3);
    mpz_init(expected_part);
    for (size_t i = 0; i < batch_size; i++) {
        mpz_set_str(expected_part, expected_parts[i], 10);
        check(mpz_cmp(parts[i], expected_part) == 0,
              "single-base smooth part preserves exact prime powers");
        check(mpz_cmp(multi_parts[i], parts[i]) == 0,
              "multi-segment smooth part equals full-base result");
        mpz_clear(inputs_z[i]);
        mpz_clear(parts[i]);
        mpz_clear(multi_parts[i]);
    }
    mpz_clear(expected_part);

    mpz_clear(expected_product);
    smooth_base_clear(&segments[1]);
    smooth_base_clear(&segments[0]);
    smooth_base_clear(&full);
}

static void test_cert_bounds(void)
{
    mpz_t p;
    mpz_t lower;
    mpz_t hasse;
    unsigned long bits = 0;
    uint64_t n2 = 0;
    uint64_t n4 = 0;

    mpz_init_set_ui(p, 101);
    mpz_init(lower);
    mpz_init(hasse);
    cert_bounds(p, lower, hasse, &bits, &n2, &n4);
    check(bits == 7, "cert_bounds bit length");
    check(n2 == 49 && n4 == 2401, "cert_bounds n^2 and n^4");
    check(mpz_cmp_ui(lower, 17) == 0, "cert_bounds lower bound");
    check(mpz_cmp_ui(hasse, 122) == 0, "cert_bounds Hasse bound");

    mpz_clear(hasse);
    mpz_clear(lower);
    mpz_clear(p);
}

static void test_factor_smooth(void)
{
    static const uint64_t expected_primes[] = {2, 3, 65537, 99991};
    static const int expected_exponents[] = {5, 3, 2, 1};
    uint64_t primes[16];
    int exponents[16];
    mpz_t value;
    mpz_t reconstructed;
    mpz_t power;

    mpz_init_set_ui(value, 1);
    mpz_mul_2exp(value, value, 5);
    mpz_mul_ui(value, value, 27);
    mpz_mul_ui(value, value, 65537);
    mpz_mul_ui(value, value, 65537);
    mpz_mul_ui(value, value, 99991);

    int count = factor_smooth(value, primes, exponents, 16);
    check(count == 4, "factor_smooth distinct-factor count");
    if (count == 4) {
        for (int i = 0; i < count; i++) {
            check(primes[i] == expected_primes[i],
                  "factor_smooth sorted prime");
            check(exponents[i] == expected_exponents[i],
                  "factor_smooth exponent");
        }
    }

    mpz_init_set_ui(reconstructed, 1);
    mpz_init(power);
    for (int i = 0; i < count; i++) {
        mpz_ui_pow_ui(power, (unsigned long)primes[i],
                      (unsigned long)exponents[i]);
        mpz_mul(reconstructed, reconstructed, power);
    }
    check(mpz_cmp(reconstructed, value) == 0,
          "factor_smooth factors reconstruct input");

    mpz_clear(power);
    mpz_clear(reconstructed);
    mpz_clear(value);
}

static void test_build_m(void)
{
    mpz_t smooth;
    mpz_t lower;
    mpz_t m;
    mpz_t lower_times_r;
    uint64_t qs[16];
    int nq = -1;
    uint64_t factors[16];
    int exponents[16];

    /* S = 2^3 * 3^2 * 101 * 103.  Largest-first selection gives 103*101. */
    mpz_init_set_ui(smooth, 749016);
    mpz_init_set_ui(lower, 1000);
    mpz_init(m);
    mpz_init(lower_times_r);

    int ok = build_m(m, qs, &nq, smooth, lower, 100, 10000);
    check(ok == 1, "build_m succeeds");
    check(mpz_cmp_ui(m, 10403) == 0, "build_m selected divisor");
    check(mpz_divisible_p(smooth, m) != 0, "build_m result divides S");
    check(mpz_cmp(m, lower) > 0, "build_m result exceeds L");

    int factor_count = factor_smooth(m, factors, exponents, 16);
    check(factor_count == 2, "build_m result distinct-factor count");
    check(factor_count > 0 && factors[0] == 101,
          "build_m result least prime");
    if (factor_count > 0) {
        mpz_mul_ui(lower_times_r, lower, (unsigned long)factors[0]);
        check(mpz_cmp(m, lower_times_r) < 0,
              "build_m result satisfies m < L*r");
    }
    check(nq == 2, "build_m large-factor count");
    check(nq == 2 && qs[0] == 101 && qs[1] == 103,
          "build_m large-factor list is exact and ascending");

    mpz_clear(lower_times_r);
    mpz_clear(m);
    mpz_clear(lower);
    mpz_clear(smooth);
}

int main(void)
{
    test_sieve();
    test_bases_and_smooth_parts();
    test_cert_bounds();
    test_factor_smooth();
    test_build_m();

    if (failures != 0) {
        fprintf(stderr, "%d smooth-engine check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("smooth engine tests passed");
    return EXIT_SUCCESS;
}
