# Upstream reuse and license audit

Status: source audit only; no upstream code or data has been copied into this repository.

This audit covers the two upstream snapshots below. Links in this document are commit-pinned so that file and line references remain meaningful.

| Repository | Audited commit | Repository-level license |
|---|---:|---|
| [`AndrewVSutherland/OneShotPrimalityProofs`](https://github.com/AndrewVSutherland/OneShotPrimalityProofs/tree/47d27c2691380c4ecb84f22aaad21f907b84bae4) | `47d27c2691380c4ecb84f22aaad21f907b84bae4` | MIT, copyright 2026 Andrew Sutherland |
| [`AndrewVSutherland2/OneShotFastECPP`](https://github.com/AndrewVSutherland2/OneShotFastECPP/tree/88da82fbcda4471746b5df34f008dcfa5cc28d2d) | `88da82fbcda4471746b5df34f008dcfa5cc28d2d` | MIT at the repository root, but with embedded GPL components described below |

SHA-256 values for the recommended source candidates at these snapshots are:

```text
e0ba3b8a7ed2ff48bd2fd824642bf67b0954a9f03f57daeb4ac4302691e1b666  voneshot.py
4fe25ccc9dda43a00b042445e9a43080ec282fdfb7e2570b82a2824ef3aa32cb  ecpp/smooth.c
cf3f9d2c9e2354d4cc8643772cccbf997e34e05c5d5267e0b46ab039ad1cea3c  ecpp/smooth.h
040b2831104053bf06d439edb4797c886532dbaf8f3b377bc6ec04c2778d6897  ecpp/curve.c
5dd7fc22c7c1b50c2f264aa86c1a3293dda1d1b0d41adb5ad816cd6ec7420498  ecpp/curve.h
7bd7550942eec81ff23d8ccdfaf5748e2b5fce511b45fd15d3afa755f0455622  ecpp/fproot.c
3cd64049beddb6bdc54dbeaaca184b4c64f359eda111b3e11a55920a417c67e2  ecpp/fproot.h
f77920b3b841aa87e013dc1a1fad6fc3accb2e38aec5629ad81e7a186293c066  ecpp/invj.c
474acd29b214674012f74e981b4b0fb441a4189ca64b49be86e4742e80606da7  ecpp/invj.h
```

`OneShotSEA` currently has no `LICENSE` file. Before vendoring anything, choose an outbound license and add an attribution/third-party-notice policy. The cleanest permissive route is to reuse only the MIT files identified below and avoid the bundled `classpoly` and `ff_poly` trees.

This is an engineering audit, not legal advice.

## Executive recommendation

Reuse a small, well-separated set of upstream components:

1. Pin and copy the unmodified canonical [`voneshot.py`](https://github.com/AndrewVSutherland/OneShotPrimalityProofs/blob/47d27c2691380c4ecb84f22aaad21f907b84bae4/voneshot.py) plus the upstream MIT license. It is the acceptance oracle and has no non-stdlib Python dependency.
2. Import the MIT `ecpp/smooth.c` and `ecpp/smooth.h` pair from `OneShotFastECPP`, with its tests adapted to SEA-generated orders. This is the strongest immediately reusable production component.
3. Adapt the MIT `ecpp/curve.c` Montgomery ladder and `mont_assemble` logic to the project's field and polynomial APIs. Reuse the algorithm and tests, but do not pull the GPL polynomial stack in through its current dependencies.
4. Use the MIT `ecpp/fproot.c` and `ecpp/invj.c` implementations as references or as an explicitly de-GPL'd bootstrap path: remove the `zp_poly` bridge and expose only the operations needed by SEA. They are useful for a CPU reference implementation, not a good final GPU or high-throughput SEA backend.
5. Do not vendor `classpoly_v1.0.3`, `ff_poly_v2.0.0`, or their `zp_poly`/`zn_poly` descendants unless the entire distributed combined work is intentionally made GPL-compatible.
6. Do not bulk-copy `phi_files/`. Initially use only the classical `phi_j_ell` tables needed for differential tests, with a manifest, provenance, and checksums. For production, generate or directly instantiate the additional levels required by the SEA design. The upstream bundle stops at level 71 and is insufficient for unique 416- or 432-bit trace reconstruction.

This plan reuses the mature certificate tail while leaving all substantive SEA trace computation, early abort, scheduling, and specialized modular-polynomial work in this project, as required by the task statement.

## Exact reusable files and functions

### Canonical certificate and verifier

The canonical verifier is the highest-priority import.

| File/function | Why it is worth reusing | Dependency/build implications |
|---|---|---|
| [`voneshot.py`](https://github.com/AndrewVSutherland/OneShotPrimalityProofs/blob/47d27c2691380c4ecb84f22aaad21f907b84bae4/voneshot.py) in full | The required unmodified verifier and definitive interpretation of `p A x0 m q1 ... qk`. Its self-tests include malformed-certificate cases. | Python 3 standard library only: `math.gcd` and `math.isqrt`. No PARI, Sage, Magma, or `gmpy2`. |
| [`xdbl`, `xadd`, `ladder`](https://github.com/AndrewVSutherland/OneShotPrimalityProofs/blob/47d27c2691380c4ecb84f22aaad21f907b84bae4/voneshot.py#L38-L80) | Small, readable reference for Montgomery x-only arithmetic. Ideal for differential tests of the native ladder. | Pure Python. Keep this verifier copy unchanged; put any test helpers elsewhere. |
| [`remainder_tree`, `prime_divisors`, `is_smooth`](https://github.com/AndrewVSutherland/OneShotPrimalityProofs/blob/47d27c2691380c4ecb84f22aaad21f907b84bae4/voneshot.py#L98-L160) | Independent reference for factor-list and smoothness semantics. | Pure Python; intended for verification, not search throughput. |
| [`verify`](https://github.com/AndrewVSutherland/OneShotPrimalityProofs/blob/47d27c2691380c4ecb84f22aaad21f907b84bae4/voneshot.py#L191-L288) | Enforces the size window, nonsingularity, exact large-prime list, minimality bound, and exact point order. | Run as a subprocess in end-to-end tests so accidental imports or edits cannot weaken it. |
| [`oneshot.gp`](https://github.com/AndrewVSutherland/OneShotPrimalityProofs/blob/47d27c2691380c4ecb84f22aaad21f907b84bae4/oneshot.gp), especially `smoothpart`, `sc_try`, and `scbound` | Compact mathematical prototype and a small-field/random-curve oracle for certificate construction. It is not a production dependency and must not be used for SEA point counts. | Requires PARI/GP. Oracle/test use only. |

The `p80/` programs add `gmpy2`, PARI/GP subprocesses, and CM-specific search logic. They are useful historical references but should not be imported for the SEA implementation.

### Smooth-part extraction and selection of `m`

The complete [`ecpp/smooth.h`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.h) / [`ecpp/smooth.c`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c) pair is directly useful and independent of the CM machinery.

| Function(s) | Role in OneShotSEA |
|---|---|
| [`sieve_primes_range`, `smooth_base_build_range`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c#L69-L160) | Build a segmented product of the primes in a smoothness interval, once per search configuration. |
| [`smooth_base_save`, `smooth_base_load`, `smooth_base_selfcheck`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c#L169-L234) | Reusable prime-product cache/checkpoint format. The format is native-limb and therefore architecture- and GMP-limb-size-dependent; add explicit endianness/limb metadata before sharing caches between local ARM and RunPod x86 hosts. |
| [`smooth_parts_multi`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c#L273-L402) | Bernstein remainder-tree extraction of exact smooth parts for a batch of candidate orders. This should sit immediately after a full order or a sufficiently small SEA order candidate set is obtained. |
| [`cert_bounds`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c#L411-L423) | Computes `L`, the Hasse upper bound, `n^2`, and `n^4` with the same integer formulas as the verifier. |
| [`factor_smooth`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c#L486-L511) | Factors a known `n^4`-smooth integer using trial division and Pollard rho. |
| [`smooth_topup`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c#L555-L588) | Bounded near-miss recovery when an early smoothness rung was used. This is a heuristic performance aid, not proof logic. |
| [`build_m2`, `build_m`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smooth.c#L590-L630) | Selects a divisor `m` satisfying `L < m < L*r` and emits exactly the large `q_i` required by `voneshot.py`. |

Also reuse or adapt [`ecpp/smoothtest.c`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/smoothtest.c) and [`ecpp/test_smooth.py`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/test_smooth.py). Test A is directly reusable; Test B currently obtains orders through the CM-specific `dscan` tool and should be rewritten to consume SEA test vectors.

Build dependencies for this pair are C11/GNU C, GMP 6+, OpenMP, and `libm`. It uses GMP's `mpz_limbs_read`, `mpz_limbs_write`, and `mpz_roinit_n` APIs as well as OpenMP tasks/loops. The imported code should retain a CPU-only serial fallback for toolchains without OpenMP and should replace the native-limb cache header before cloud cache exchange.

### Montgomery certificate assembly

The useful source is [`ecpp/curve.c`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/curve.c) with [`ecpp/curve.h`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/curve.h):

| Function/region | Role in OneShotSEA |
|---|---|
| [`xdbl`, `xadd`, `ladder`, `ladder_is_O`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/curve.c#L13-L65) | Native x-only point arithmetic for both a Montgomery curve and its twist. Differentially test against the Python verifier. |
| [`mont_assemble`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/curve.c#L67-L152) | Converts `j` to a Montgomery coefficient, distinguishes the order of the curve and twist, projects a random x-coordinate by `N/m`, and checks exact order prime-by-prime. This is the certificate tail the SEA search needs after it finds a good order. |
| [`oneshot.c` winner tail](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/oneshot.c#L394-L419) | Shows the correct sequence `build_m` -> `mont_assemble` -> canonical one-line emission. Adapt this small region; do not import the surrounding CM scheduler. |

`mont_assemble` is not standalone. It currently depends on:

- `fp_ctx`, `fp_poly`, `fp_find_all_roots`, and field operations from `fproot.c`;
- `cornacchia_sqrtmodp` from `cornacchia.c`; and
- `factor_smooth` from `smooth.c`.

The recommended integration is to make `mont_assemble` consume OneShotSEA's own finite-field, polynomial-root, and square-root interfaces. The SEA implementation will already require these operations. Importing the CM-oriented `cornacchia` context merely to get Tonelli-Shanks would couple unrelated layers. Preserve the algorithm and checks, but remove that dependency.

One subtle requirement from the CM driver should remain visible: a desired `m` must divide the group exponent, not merely the group order. The CM driver handles this with [`exponent_part`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/oneshot.c#L246-L261). A random SEA curve does not provide the CM group-structure metadata used there. The assembly routine's exact-order test is authoritative; failures must cause a candidate retry or a different `m`, never a certificate emission.

### Finite-field and polynomial arithmetic

There are three distinct implementations in `OneShotFastECPP`; they should not be treated as one permissively licensed library.

#### `ecpp/fproot.c`: useful MIT reference with a GPL link edge

[`ecpp/fproot.c`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/fproot.c) and [`ecpp/fproot.h`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/fproot.h) provide:

- multi-limb Montgomery `F_p` operations (`fp_init`, `fp_mul`, `fp_add`, `fp_sub`, `fp_pow`, `fp_inv`);
- flat Montgomery-coefficient polynomials;
- Kronecker-substitution multiplication, Barrett-style modular reduction, polynomial gcd/division; and
- `fp_find_root` / `fp_find_all_roots` using Cantor-Zassenhaus-style splitting.

This code supports the target size (its fixed bound is documented as roughly 1900 bits), and the low-degree path is suitable as a simple CPU reference. It is not a complete SEA polynomial API: most multiplication, reduction, gcd, and exponentiation functions are `static`, so a production import needs an intentional public API and tests.

The current file includes `zp_poly.h` and, at degrees at least 1024, dispatches gcd and exact division to GPL `zp_poly` (`fpoly_gcd_fast` and `fpoly_divexact_fast`). Merely observing that SEA polynomials are normally below the dispatch threshold does not remove the compiled/link dependency. For a permissive build, remove this bridge and its include, route all degrees through the file's own `fpoly_gcd` / `fpoly_divexact`, and add a compile/link test that no `zp_*` symbol remains. Keep this as a reference backend while profiling a purpose-built SEA backend.

#### `classpoly_v1.0.3/zp_poly_*`: technically capable, GPL

The `zp_poly` files implement arbitrary-size `F_p[x]` division, half-gcd/gcd, modular exponentiation, composition, roots, and factorization. The most relevant APIs are `zp_poly_gcd`, `zp_poly_div`, `zp_poly_mod_pow_xn`, `zp_poly_find_roots`, and `zp_poly_factor`. They are explicitly GPL version 2 or later as part of `classpoly`; do not import them into a permissively licensed OneShotSEA binary.

#### `ff_poly_v2.0.0`: GPL and the wrong field size for production

`ff_poly` is an optimized word-size finite-field/polynomial package and includes `zn_poly` for fast polynomial multiplication. It is useful to understand the upstream CM implementation and for small-field oracle tests, but its base field is machine-word sized and therefore cannot directly represent the 416- or 432-bit production fields. It is GPL-covered and should not be a OneShotSEA dependency.

### Modular-polynomial loaders and tables

The permissive project-local loader is [`ecpp/invj.c`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/invj.c) / [`ecpp/invj.h`](https://github.com/AndrewVSutherland2/OneShotFastECPP/blob/88da82fbcda4471746b5df34f008dcfa5cc28d2d/ecpp/invj.h):

- `invj_load_phi` parses classical symmetric tables named `phi_j_<ell>.txt`, expanding each stored off-diagonal term;
- `invj_load` parses invariant-to-`j` relations named `phi_<inv>_j.txt`; and
- `invj_jroots` directly instantiates a loaded bivariate polynomial at a field element and finds all roots.

`invj_load_phi` is a useful baseline table parser. `invj_jroots` is a useful correctness/reference path for direct `Phi_ell(j,Y)` evaluation, but it is not the specialized production evaluator required by the prompt and it inherits the `fproot` dependency described above.

The audited `phi_files/` bundle contains 873 files and occupies 48 MiB in the checkout:

- 20 classical `j` tables for prime levels `2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71`;
- 43 invariant-to-`j` relations; and
- 810 enumeration relations for Atkin, Weber, eta, and related class invariants.

The format of a classical table is one line `[a,b] coefficient`, storing only `a >= b` because `Phi_ell` is symmetric. The invariant-to-`j` files use sparse expressions such as `+1*X^3-X*Y...`.

The 20 available classical levels have product

```text
557940830126698960967415390 = 2^88.8502...
```

Even if every level yielded an exact trace residue, this is far below the modulus needed to identify a trace in the Hasse interval. A straightforward all-small-primes product first exceeds `2^210` (the 416-bit target's approximate `4 sqrt(p)` requirement) at level 163, and first exceeds `2^218` for the 432-bit target at level 167. Atkin constraints can reduce the candidate set, but the level-71 bundle cannot by itself guarantee unique reconstruction. It is suitable for bring-up, classification tests, early-abort experiments, and a classical baseline only.

Representative checksums at the audited commit are:

```text
5cc1f074cbd56c2d232bd317f72c724b5161bce9c9ffda3bc167c9c0e0ec3dc0  phi_j_2.txt
f61a17e6cda3d62efd132ad2516e14039fdbf387493e42d8be81b5fdc3e298de  phi_j_71.txt
0bada022cc2ba4ae46196af17b667870b0cce2d2a4134c58043ce56b5826cd17  phi_f2_j.txt
20e0bd974ae163cb0425623277ee423ba317370e1ab09890b43f8b4d35d1ca8b  phi_a71_j.txt
```

The digest of the sorted list of SHA-256 records for all 873 table files is:

```text
f4d74d638de4ab058890ef88f8f8e290eee2e622dddb3813e21295c1df0fdada
```

The upstream root MIT license appears intended to cover the repository's project files, but the tables have no per-file license or provenance metadata beyond the README's reference to A. Sutherland's modular-polynomial database. Because the same repository mixes MIT project code and explicitly GPL source trees, do not assume the root MIT notice conclusively relicenses database-derived tables. Before redistributing them, obtain a clear license statement from the upstream owner or regenerate the required tables from a documented algorithm. In either case, OneShotSEA's manifest must record source/generator version, normalization, checksum, size, and level.

For the initial import, include no alternative-invariant tables. They are CM enumeration data and do not become a specialized SEA implementation merely by being present. Add a family only after the SEA design specifies how a curve is lifted to that invariant, how trace information is recovered, and why the resulting hot path is smaller or faster than classical `j`.

## License compatibility matrix

| Component | Effective license | Compatibility and obligations |
|---|---|---|
| `OneShotPrimalityProofs` files, including `voneshot.py` and `oneshot.gp` | MIT | Compatible with permissive or GPL project licensing. Preserve the copyright and permission notice in copies/substantial portions. |
| `OneShotFastECPP` root project code, especially `ecpp/smooth.*`, `ecpp/curve.*`, `ecpp/oneshot.c`, and `ecpp/invj.*` | Root MIT, with no contrary per-file notice in these files | Reusable in a permissive project with the root MIT notice and source attribution. Avoid copying regions explicitly described as ports of GPL formulas, notably `ecpp/cminv.c`. |
| `ecpp/fproot.*` by itself | Root MIT wrapper/reference code | Permissive only after removing the compiled `zp_poly` dependency. The unmodified linked program includes GPL code. |
| `classpoly_v1.0.3/*`, including `zp_poly_*` and `class_inv_mpz.c` | GPL-2.0-or-later by explicit source notices | MIT code may be combined into a GPL work, but distributing a binary/derivative containing these files requires GPL compliance for the combined work. Not suitable for a permissive-only distribution. |
| `ff_poly_v2.0.0/*` | GPL v2 license file; source notices refer to it | Treat as GPL-covered. Do not vendor or link it into a permissive OneShotSEA binary. |
| bundled `zn_poly` | Explicitly GPL version 2 or version 3 | Same copyleft issue; it is pulled in by `ff_poly`. |
| `phi_files/*.txt` | Ambiguous in this mixed-license repository | Do not redistribute until clarified, or regenerate. Mathematical/factual status does not remove the need for a clear provenance and redistribution policy. |
| GMP | LGPL-3.0-or-later or GPL-2.0-or-later upstream choice | Dynamic linking is straightforward for a permissive project; source/static distribution needs the corresponding LGPL/GPL notices and relinking obligations. Confirm the packaging mode used for releases. |
| GCC `libgomp` / LLVM `libomp` | Runtime-specific licenses/exceptions | External system dependency is preferable. Document which OpenMP runtime is used; do not vendor it casually. |
| PARI/GP | GPL, external test/oracle process | Safe as an external development/test dependency; do not link it into production. |
| Magma | Proprietary, external local oracle | Use only as an external validation process. Do not redistribute Magma or generated proprietary components. |
| CUDA toolkit/runtime | NVIDIA terms | Keep CUDA optional and separately built. Review redistributable-runtime terms before shipping binaries or containers. |

The repository-level MIT file in `OneShotFastECPP` does not override explicit GPL notices within `classpoly` and `zn_poly`, nor the GPL license shipped inside `ff_poly`. A single executable that links these objects should be treated as GPL-covered for distribution purposes.

## Build and portability implications

The upstream build is tuned for 64-bit Linux/x86 with GCC:

```text
-O3 -march=native -m64 -funroll-loops -std=gnu11 -fopenmp
```

It builds static `ff_poly`, then GPL `classpoly`, then the ECPP programs and links GMP and `libm`. The top-level classpoly makefile defaults to `-static`. This structure should not be copied into OneShotSEA.

The current local host is Darwin/arm64. `/usr/bin/gcc` is Apple Clang, not GCC; Homebrew GMP 6.3.0 and LLVM OpenMP 22.1.8 are installed, but no `gcc-13` or `gcc-14` was found in `PATH`. Consequences:

- upstream `-fopenmp` will not work with Apple Clang without explicit Homebrew `libomp` include/link flags;
- `-march=native` and the static-link defaults are not portable between Apple arm64 and RunPod Linux/x86_64;
- native GMP-limb cache files must not be assumed portable across those hosts; and
- `-march=native` must not be used for portable release artifacts or heterogeneous cloud workers.

Use a small project-owned build (CMake or a portable Makefile) with feature checks for GMP, OpenMP, CUDA, architecture-specific intrinsics, and sanitizer builds. Keep these target boundaries:

```text
oneshot_core       field/poly/curve/SEA code; GMP; no CAS
oneshot_smooth     smooth.c-derived CPU batch engine; GMP; optional OpenMP
oneshot_verify     invokes pinned Python verifier
oracle_magma       tests only; external process
oracle_pari        tests only; external process
cuda_kernels       optional; no CAS and no GPL polynomial libraries
```

Add CI/link audits that fail if production binaries acquire `pari`, `magma`, `ff_poly`, `classpoly`, `zp_*`, or `zn_poly` symbols/dependencies.

## Recommended minimal import plan

### Import 1: proof oracle

Create a `third_party/oneshot_primality_proofs/` directory containing only:

```text
voneshot.py
LICENSE
UPSTREAM.md       # repository URL, commit, retrieval date, SHA-256
```

Keep `voneshot.py` byte-for-byte unchanged and test its file hash before final-certificate validation. Do not make the production generator depend on Python.

### Import 2: smoothness engine

Bring in:

```text
src/smooth.c
src/smooth.h
tests/test_smooth_oracle.py   # adapted from upstream Test A and Test B
```

Preserve upstream MIT attribution in file headers or a `THIRD_PARTY_NOTICES.md`. First make only namespace/build/cache-portability changes. Verify against PARI/Magma and a simple Python factorization oracle before optimizing. Feed both `N = p + 1 - t` and its twist order `2p + 2 - N` to the same batch.

### Import 3: certificate tail

Port the four x-only helpers and `mont_assemble` into a narrow interface such as:

```text
assemble_certificate(p, j, order, trace, m, seed) -> (A, x0) or failure
```

Back it with OneShotSEA's field, polynomial-root, and square-root implementations. Use `build_m` unchanged initially. Differentially test native ladders against the pinned verifier, then run the final emitted line through the verifier as a subprocess.

### Import 4: reference polynomial/table path

For the CPU reference path only:

- adapt `invj_load_phi` as the classical table parser;
- adapt the low-degree `fproot` field/polynomial code;
- remove `#include "zp_poly.h"`, `fpoly_to_zp`, `fpoly_from_zp`, and both fast bridge functions;
- route gcd/division to the local schoolbook implementations; and
- expose/test only the operations SEA needs.

Do not use this import as a substitute for custom SEA logic. Its purpose is differential testing and the classical-`j` ablation.

### Import 5: tables only when needed

Start with a tiny table set, for example `Phi_2`, `Phi_3`, and `Phi_5`, to validate normalization, instantiation, roots, Elkies/Atkin classification, and trace residues. Prefer a reproducible generator over copying the entire upstream bundle. If upstream files are redistributed after license clarification, list every file and SHA-256 in a checked manifest.

Extend beyond level 71 with the project's specialized/on-demand evaluator. A production SEA run for `p125`/`p130` must not silently stop with an 88.85-bit trace modulus or call classpoly, PARI, Magma, or another SEA implementation to fill the gap.

## Explicit non-import list

Do not copy or vendor these in the minimal implementation:

- `classpoly_v1.0.3/` (GPL and CM-specific);
- `ff_poly_v2.0.0/` and its `zn_poly/` subtree (GPL, word-size field);
- `ecpp/cminv.c` / `cminv.h` (CM-only and explicitly ports GPL formulas);
- `ecpp/cm_method.c`, `dscan.c`, `cornacchia.c`, and the CM process scheduler, except as algorithmic references;
- the full 873-file `phi_files/` bundle; or
- preexisting certificates as search inputs.

Existing certificates remain useful only as verifier regression vectors. They must never be represented as output of the new SEA search.

## Acceptance checks for any later import

Before merging reused code or data, require all of the following:

1. The exact upstream commit and path are recorded.
2. The applicable license and copyright notice are present.
3. `rg`/link inspection finds no accidental `classpoly`, `ff_poly`, `zp_*`, or `zn_poly` dependency in permissive production targets.
4. Oracle-only PARI and Magma calls are absent from production binaries and search scripts.
5. Every table has normalization documentation, provenance, size, and SHA-256.
6. ARM64 CPU, x86_64 CPU, and optional CUDA builds do not exchange undocumented native-limb cache files.
7. The unmodified pinned `voneshot.py` accepts every emitted regression certificate and rejects malformed variants.
