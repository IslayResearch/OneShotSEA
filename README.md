# OneShotSEA: custom direct-SEA branch

This branch adds a target-table-free SEA path to the existing one-shot
primality-proof search.  Its main deliverable is a classical `j` evaluator for

```text
Phi_ell(j,Y)       and       d/dX Phi_ell(j,Y)
```

that constructs only the two univariate specializations SEA needs.  It uses
the CRT/isogeny-volcano algorithms of Bröker--Lauter--Sutherland and
Sutherland, rather than calling PARI/GP, Magma, Sage, or an external SEA
implementation.

The evaluator is wired into `oneshotsea search` as an **opt-in, identity-bound
tail** after the authenticated Weber-table levels and before the optional
Schoof fallback.  It retains the existing SEA state, so a curve that survives
the early smoothness screen continues without repeating earlier levels.

This is a reviewable implementation milestone, not a claim that the full
large-prime search is finished.  In particular:

- the default search remains table-backed;
- no new `nextprime(10^125)` certificate is claimed here;
- the classical direct path is working and independently tested at levels 5,
  7, and 11 on the 416-bit `p125` fixture;
- the faster Weber direct path is only partially implemented and is not yet
  admitted as a production trust anchor; and
- the intended `p^(1/8+o(1))` search scaling is a conditional heuristic, not a
  benchmark result established by this repository.

## Branch delta

| Area | What this branch adds |
|---|---|
| Direct modular-polynomial evaluation | Suitable-order selection, witnessed auxiliary primes, target-field lifting, isogeny-volcano interpolation, and exact centered CRT reconstruction without a target-level bivariate table |
| Classical CM surface construction | Internally derived `D=-7*3^(2n)` ring class polynomials from fixed exact `Phi_3` resultants, complete surface admission, and Vélu enumeration of all `ell+1` cyclic quotients |
| SEA consumption | Direct BMSS/Frobenius Elkies residues and certified Atkin factor-degree constraints |
| Retained-state runner | Strict increasing direct-level schedules, exact-versus-Atkin completion rules, transactional progress, and sound bounded early screening |
| Search integration | Optional classical direct tail, checkpoint/schedule binding, live and retained telemetry, retained-state continuation, and lazy cross-curve reuse of immutable CM/CRT contexts |
| Independent validation | Differential table oracles, independent division-polynomial and Vélu checks, independent Schoof congruences, progress replay, corruption tests, and sanitizer coverage |

The inherited curve generators, smoothness engine, certificate builder,
canonical verifier, checkpointing, and cloud scripts are integration context;
they are not reimplemented by this branch.

## Build and validate

The native build requires a C++20 compiler and GMP.

```sh
/usr/bin/make -j4
/usr/bin/make \
  test-direct-modpoly \
  test-prime-isogeny \
  test-cm-surface \
  test-search-pipeline \
  test-cli \
  test-progress-audit \
  test-x1-27-probe
```

`make test-all` also runs the inherited repository-wide suite.  Magma is an
optional, separate point-count oracle:

```sh
MAGMA=/path/to/magma /usr/bin/make test-all
```

The focused tests cover both positive results and fail-closed behavior,
including malformed witnesses, insufficient CRT coverage, incomplete CM
surfaces, mixed signs, duplicate relations, bad schedules, identity mutation,
and incomplete point counts.

## Use the opt-in local search tail

Add an increasing list of distinct primes greater than three to a normal
`oneshotsea search` command:

```sh
./build/oneshotsea search \
  ...existing search arguments... \
  --classical-direct-levels 7,11 \
  --classical-direct-max-prime-candidates 1000000 \
  --classical-direct-max-x-candidates 1000000
```

The direct policy version, ordered level list, and both failure caps are
included in the schedule digest.  Changing them invalidates an existing
checkpoint.  Exhausting a cap is reported as an implementation limit; it is
never treated as a mathematical rejection.

This path is currently admitted only by the local `oneshotsea search` CLI.
The checked RunPod and AWS launch/audit wrappers intentionally do not accept
these options yet, so an operator must not add them to a remote production
run until those wrappers and their strict artifact auditor are extended and
requalified.  The current p125 RunPod search therefore remains on the audited
default table-backed schedule.

Within one local search invocation, each curve-independent CM/CRT level is
prepared lazily at its first use and then shared read-only by all curve
workers. Unused later levels are never prepared. The summary reports the
number of contexts actually prepared and their cumulative setup time. The
first-use curve timing already includes its preparation or wait, so honest
benchmarks report both fields but do not add setup to end-to-end wall time a
second time.

Per-level direct records include the suitable-order discriminant and class
number, auxiliary-prime count, exact residue or Atkin projective order,
accumulated exact/effective moduli and trace-candidate counts, and elapsed
time.  Given an independently obtained final trace, replay table and direct
evidence with:

```sh
python3 tools/audit_sea_progress.py \
  --progress runs/p125/worker-0.ndjson \
  --index GLOBAL_INDEX \
  --trace INDEPENDENT_TRACE
```

See [the search-pipeline guide](docs/search_pipeline.md) for the complete
command, checkpoint trust model, telemetry schema, and operational limits.

## Evidence currently in the repository

- On the 416-bit `p125` fixture, the callback-free classical path reconstructs
  levels 5, 7, and 11 from 34, 37, and 43 witnessed auxiliary primes and
  obtains trace residues 3, 5, and 10.  The level-11 result is checked against
  an independent Schoof characteristic equation; levels 5 and 7 also agree
  coefficient-for-coefficient with authenticated full-table paths.
- The resulting exact modulus is only `385`, so the fixture deliberately
  remains incomplete for unique trace recovery.  The code does not turn this
  partial evidence into a certificate.
- A small-field level-7 no-root fixture agrees with an independent full-table
  oracle on both the Atkin projective order and its trace-residue set.
- A production-search fixture starts with ten Hasse-compatible traces,
  continues retained state through direct levels 7 and 11, isolates the exact
  trace, and passes the unchanged canonical certificate verifier without a
  repeated table pass or Schoof fallback.

On the current local 416-bit X1(27) fixture, preparing the shared level-7 and
level-11 contexts takes about 3.1 s.  The first direct evaluation therefore
takes about 3.2 s, while a second independently generated curve using the same
contexts takes about 0.10 s; both curves' direct evidence is checked against
independent Schoof residues.  This roughly 30x cold-to-warm difference proves
that the intended reuse is active, but it is a two-curve engineering sample,
not an asymptotic benchmark or a production-throughput distribution.

## Correctness and trust boundary

The classical path derives its class state internally and uses an exact
integer form of the published classical coefficient-height bound.  A positive
Elkies result is then checked by BMSS isogeny reconstruction and Frobenius.
The no-root Atkin result has no equivalent local kernel witness, so it depends
more directly on the reconstructed specialization; the implementation admits
it only after square-free equal-degree factorization and fails closed on any
inconsistency.

The Weber producer is deliberately outside that production boundary.  It can
reproduce signed level-5 residues from caller-supplied Weber class/orientation
state, but class-polynomial authentication, orientation-relation provenance,
and a normalization-specific coefficient-height proof remain open.

Early aborts may save work but may not create false rejections.  Smoothness
rejection is sound only after enumerating the complete Hasse-compatible trace
set and checking both the curve and twist orders.  Atkin constraints may help
that bounded screen, but they cannot satisfy the unique-trace gate used to
construct a certificate.

## Asymptotic status

Let `n = ceil(log2 p)`, use the verifier's smoothness bound `B=n^4`, and seek a
certified divisor of size `p^(1/2+o(1))`.  Under the usual Dickman--Mertens
heuristic, the success probability for a random curve order is
`p^(-1/8+o(1))`, giving `p^(1/8+o(1))` expected curves.  If each SEA point count
costs `poly(n)`, that cost is absorbed by the `o(1)` term.  This is the intended
advantage over the CM search term `p^(1/4+o(1))`.

The current architecture is compatible with that argument: it streams
specializations and avoids a finite target-level table catalog.  It does not
yet prove the end-to-end scaling claim.  The practical auxiliary-prime search
is bounded to 64-bit primes and uses the paper's heuristic fixed-`v` selector;
large-level memory and production throughput still require measurement and
optimization. Curve-independent preparation is now reused across local search
workers and has a target-sized cold/warm regression; broader level schedules
and multi-curve throughput distributions remain to be measured.

## Why this is worth expert review

The branch now crosses the useful review threshold: it produces independently
checked SEA evidence on a 416-bit target and exercises the same retained state
inside the production search.  Review can therefore focus on the novel,
bounded trust boundary before performance work makes it harder to change:

1. suitable-order and `(p,t,v,D)` predicates;
2. fixed-`Phi_3` ring-class resultants and exact factor removal;
3. CM-surface admission and complete Vélu edge enumeration;
4. interpolation normalization, target-field lifting, height bounds, and CRT;
5. BMSS/Frobenius consumption and Atkin factor-degree certification; and
6. retained-state completion rules, early screening, and schedule identity.

A positive review would validate the mathematical and integration core needed
for the intended one-shot SEA search.  It would not endorse a finished Weber
evaluator, a measured crossover point, or a new large primality certificate.

## Code and design map

- [`src/direct_modpoly.cpp`](src/direct_modpoly.cpp): suitable orders,
  auxiliary-prime witnesses, target lifts, and exact CRT.
- [`src/class_polynomial.cpp`](src/class_polynomial.cpp): three-power ring
  class polynomials from fixed `Phi_3` resultants.
- [`src/prime_isogeny.cpp`](src/prime_isogeny.cpp): auxiliary-field point
  arithmetic, rational cyclic kernels, and Vélu quotients.
- [`src/cm_surface.cpp`](src/cm_surface.cpp): classical surface enumeration and
  direct specialization.
- [`src/weber_cm_surface.cpp`](src/weber_cm_surface.cpp): experimental signed
  Weber specialization.
- [`src/sea.cpp`](src/sea.cpp): direct levels, Elkies/Atkin consumption, and
  retained-state completion.
- [`src/search_pipeline.cpp`](src/search_pipeline.cpp) and
  [`src/main.cpp`](src/main.cpp): production-tail integration, identity, CLI,
  and telemetry.
- [`tools/audit_sea_progress.py`](tools/audit_sea_progress.py): independent
  replay of table and direct SEA records.
- [Direct CRT producer design](docs/explicit_crt_producer.md)
- [SEA design and proof obligations](docs/sea_design.md)
- [Production search pipeline](docs/search_pipeline.md)

## References

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
