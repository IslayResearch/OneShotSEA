# OneShotSEA

OneShotSEA is a custom, CAS-free Schoof--Elkies--Atkin search engine for
one-shot elliptic-curve primality certificates beyond 400 bits.  This branch
implements the SEA route directly rather than relying on CM construction, and
targets `nextprime(10^125)` (`p125`, 416 bits) before continuing to
`nextprime(10^130)` (`p130`, 432 bits).

## What this branch claims

The branch contains a complete search path for the current authenticated
level schedule:

1. deterministically generate certificate-compatible curves from the optimized
   X1(27) family, prove their torsion metadata, and retain an exact Weber-f
   source coordinate;
2. specialize authenticated Weber modular polynomials, recover normalized
   isogenies with BMSS, prove exact Frobenius trace residues, and combine them
   with certified low-level Atkin constraints and an exact CRT;
3. enumerate the complete bounded trace set and reject a curve early only when
   every curve/twist order candidate has exact `n^4`-smooth part below the
   verifier threshold; and
4. complete the point count, construct an exact-order Montgomery point, emit a
   canonical one-line certificate, and accept it only if the pinned upstream
   verifier returns true.

Production point counting uses only project-local C++20 and GMP code.  Magma is
an independent test oracle; Sage, PARI/GP, Magma, and another SEA implementation
are not called by the search path.

This is a working search implementation, not a certificate announcement.  No
new `p125` certificate has been found yet, and the `p130` continuation has not
started.  The checked-in production schedule contains 77 Weber-f levels
through 401.  A pinned normalized source catalog now permits selective,
authenticated materialization of all 166 admissible archive levels through
997 without checking their roughly quadratic payload growth into Git.  This is
still bounded source data, not an unbounded-input asymptotic implementation;
direct isogeny-volcano/explicit-CRT evaluation remains the intended extension.

## SEA path implemented here

- **Specialized modular polynomials.**  Checked-in and selectively
  materialized Weber-f tables are authenticated against a catalog derived from
  one content-addressed source archive.  The implementation specializes them
  over the target field, extracts rational neighbors, reconstructs BMSS
  isogenies, and recovers Frobenius eigenvalues in the kernel quotient algebra.
- **Exact reuse.**  A table-verified 24th-root covariance shares modular roots
  across compatible Weber lifts.  A fully validated first isogeny kernel may
  derive the characteristic-polynomial conjugate eigenvalue; the independent
  recovery path remains available for differential tests.
- **Sound early abort.**  Early rejection uses a complete Hasse-compatible
  trace set and exact full-`n^4` smooth parts for both signs.  Partial
  smoothness, learned scores, and incomplete trace sets never produce a sound
  rejection.
- **Certificate-oriented curves.**  The production X1(27) point-four family
  proves a conservative cyclic divisor 108 and, for `p125`, a group-order
  divisor 432.  Certificate assembly independently proves the exact order of
  the emitted point; the family metadata is not trusted as a substitute.
- **Deterministic parallel search.**  Curves may execute concurrently against
  one immutable smooth cache, but progress, checkpoints, and a winning
  certificate are committed in global-index order.

The authoritative as-built description is
[`docs/sea_design.md`](docs/sea_design.md).  The exact execution and failure
boundaries are in [`docs/search_pipeline.md`](docs/search_pipeline.md) and the
specialized point-counting details are in
[`docs/weber_implementation.md`](docs/weber_implementation.md).

## Latest arithmetic improvement

Large polynomial exponentiations now combine exact limb-aligned Kronecker
convolution with one reusable reverse-polynomial reduction context.  The
packed products made reciprocal quotient reduction profitable: for monic
moduli of degree at least 96, an inverse of the reversed modulus is prepared
once and reused throughout `powmod`.  Smaller and nonmonic quotient rings keep
the former long reducer.

On one deterministic `p125` curve, reverse-order baseline/candidate brackets
measured:

| workload | baseline mean | branch mean | speedup |
|---|---:|---:|---:|
| degree-194 quotient Frobenius | 2.115 s | 1.192 s | 1.775x |
| degree-401 quotient Frobenius | 7.949 s | 2.473 s | 3.214x |
| complete 55-level SEA stage | 56.074 s | 42.229 s | 1.328x |

All four full-SEA baseline/candidate projections were identical, with SHA-256
`8055a435d1abd535574867a55169168635ac683c2ed9e065df135d7440f4b8e6`.
The proof, historical negative comparison, threshold ablation, raw timing
values, benchmark identities, and limitations are in
[`docs/reciprocal_reduction_ab.md`](docs/reciprocal_reduction_ab.md) and
[`artifacts/local/p125-reciprocal-kronecker-20260802/result.json`](artifacts/local/p125-reciprocal-kronecker-20260802/result.json).

The same prepared reducer is now retained for the entire kernel quotient ring,
instead of being rebuilt for polynomial Frobenius and then abandoned during
eigenvalue point arithmetic.  A compile-time off/on level-409 bracket reduced
median eigenvalue recovery from 2.837 to 2.282 seconds (1.243x) and median SEA
from 5.155 to 4.597 seconds (1.121x).  All six semantic projections were
identical; this changes arithmetic cost only, not trace evidence.

This result establishes arithmetic throughput on one fixed curve.  It is not a
certificate-yield measurement.

## Build and reproduce

The native build requires a C++20 compiler and GMP.  Smooth-engine tests also
require OpenMP (`libomp` with Apple Clang or the GCC runtime on Linux).

```sh
make all
make test test-poly-square test-atkin test-factor test-certificate \
  test-search-pipeline test-cli test-verifier test-performance-artifacts
```

Build and replay the deterministic 416-bit SEA benchmark without the 5.4 GB
smooth cache:

```sh
make build/benchmark_p125_poly_trusted
./build/benchmark_p125_poly_trusted sea data/modpoly/weber_f \
  > /tmp/oneshotsea-p125.projection \
  2> /tmp/oneshotsea-p125.timing
```

The complete suite includes the independent Magma point-count oracle and
native/Magma differential tests:

```sh
MAGMA=/path/to/magma make test-all
```

The production search additionally needs the authenticated exact-smooth cache.
Its construction, trust boundary, checkpoint identity, and complete `search`
invocation are documented in
[`docs/search_pipeline.md`](docs/search_pipeline.md).

Additional cataloged Weber levels can be materialized into a separate compact
directory from the pinned archive:

```sh
python3 tools/fetch_weber_tables.py \
  --archive /path/to/phi1.tar.gz \
  --output /tmp/weber-extra --levels 409,419,421
```

The archive, source catalog, selected table bytes, and emitted manifest are all
checked before the C++ production path accepts the directory.  See
[`docs/weber_on_demand_catalog.md`](docs/weber_on_demand_catalog.md).

## Validation boundary

The retained validation for this branch includes:

- independent schoolbook differentials for products and squares across the
  Kronecker dispatch boundary and through degree 401 over both small fields and
  the 416-bit target field;
- independent long-reducer differentials for reciprocal reduction across its
  degree-96 dispatch boundary, through degree 401, and on nonmonic, sparse,
  repeated-factor, and high-degree-input cases;
- AddressSanitizer and UndefinedBehaviorSanitizer runs of the core and
  polynomial differential suites;
- native Schoof, classical-j Elkies, Weber/BMSS, Atkin, trace/CRT, smoothness,
  checkpoint, certificate, and canonical-verifier tests;
- independent Magma point counts and native/Magma trace-residue differentials;
- a 10,000-curve Weber/Magma corpus with sound early-abort replay; and
- deterministic full-SEA A/B projection equality at `p125`; and
- catalog-authenticated level-409 and level-997 runs on the 416-bit target,
  including a retained, checksummed Magma full-count match for the exact
  level-409 residue.

These gates test the implementation independently in several directions, but
they do not turn the smooth-order yield model into a theorem or prove that a
finite search will find a certificate.

## Asymptotic claim and its limit

Let `n=ceil(log2 p)`, let the verifier smoothness bound be `B=n^4`, and let its
required certified divisor be `L=p^(1/2+o(1))`.  The standard Dickman--Mertens
heuristic gives

```text
Pr[an order has a suitable B-smooth divisor] = p^(-1/8+o(1)),
```

so the expected search examines `p^(1/8+o(1))` curves.  Under the usual
small-Elkies-prime heuristic, custom SEA point counting, exact smooth-part
extraction, and certificate assembly contribute only factors polynomial in
`log p`; they are absorbed by the `o(1)` exponent.  This is the intended
asymptotic separation from the CM search term `p^(1/4+o(1))`.

Both exponents are heuristic.  Curve/twist dependence, torsion conditioning,
group-exponent restrictions, and exact-order representability affect the
constant and may affect lower-order terms.  Moreover, a literal asymptotic
implementation must grow its modular-polynomial levels with `log p`; even the
selectively materialized level-997 source catalog remains finite and does not
satisfy that requirement on its own.  The detailed
derivation and measured boundary are in
[`docs/sea_design.md`](docs/sea_design.md#05-asymptotic-expectation-and-measured-boundary).

The specialized-table strategy follows the modular-function and direct-
evaluation directions in:

- Broker, Lauter, and Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402);
- Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).

## Suggested review path

A focused mathematical review does not require reading the historical
benchmark log:

1. [`src/sea.cpp`](src/sea.cpp) for exact/Atkin constraint separation and
   bounded trace completion;
2. [`src/elkies.cpp`](src/elkies.cpp) and [`src/isogeny.cpp`](src/isogeny.cpp)
   for Weber covariance, normalized codomains, BMSS reconstruction, and
   Frobenius eigenvalues;
3. [`src/early_abort.cpp`](src/early_abort.cpp) for the sound rejection proof
   boundary;
4. [`src/certificate.cpp`](src/certificate.cpp) for exact-order assembly and
   verifier-facing inequalities; and
5. [`src/poly.cpp`](src/poly.cpp) plus
   [`docs/kronecker_convolution.md`](docs/kronecker_convolution.md) for the
   latest arithmetic change.

[`docs/bottleneck_registry.md`](docs/bottleneck_registry.md) distinguishes
accepted optimizations, rejected experiments, open outcome gates, and the next
measured work.  Cloud-operation notes and historical A/B records remain under
`docs/`, but they are supporting evidence rather than the purpose of this
branch.

## Upstream context

- [AndrewVSutherland/OneShotPrimalityProofs](https://github.com/AndrewVSutherland/OneShotPrimalityProofs)
- [AndrewVSutherland2/OneShotFastECPP](https://github.com/AndrewVSutherland2/OneShotFastECPP)
- [AndrewVSutherland/DANGER3](https://github.com/AndrewVSutherland/DANGER3)
