# OneShotSEA

A custom Schoof-Elkies-Atkin search for one-shot elliptic-curve primality proofs beyond 400 bits.

The initial objective is a reproducible one-shot ECPP certificate for `nextprime(10^125)` using an independently implemented SEA point-counting engine, early aborts tailored to certificate discovery, and specialized modular-polynomial techniques. The implementation may use the local multicore machine and deterministic RunPod GPU workers. Magma is available locally as an independent test oracle, but is not part of the production search.

See [revised_prompt.md](revised_prompt.md) for the full task specification, validation requirements, deliverables, and completion criteria.

## Related projects

- [AndrewVSutherland/OneShotPrimalityProofs](https://github.com/AndrewVSutherland/OneShotPrimalityProofs)
- [AndrewVSutherland/DANGER3](https://github.com/AndrewVSutherland/DANGER3)
- [AndrewVSutherland2/OneShotFastECPP](https://github.com/AndrewVSutherland2/OneShotFastECPP)

## Status

Implementation and the real `p125` search are in progress; no certificate is
claimed until the unmodified pinned verifier accepts it. The repository
currently includes:

- a portable GMP-backed finite-field layer with thresholded exact packed,
  Karatsuba, and specialized-square convolution, exact-cost windowed quotient
  exponentiation, native Schoof reference, and independent Python/Magma
  oracle paths;
- polynomial-subring Frobenius eigenvalue powers with a retained general
  quotient-Element reference path and exact differential coverage;
- an authenticated 77-level Weber-f schedule through level 401, normalized
  BMSS isogeny recovery, exact Frobenius residues, verified 24th-root
  source-lift orbit reuse, and exact conjugate-eigenvalue reuse;
- certified low-level Atkin constraints, exact CRT/Hasse trace enumeration,
  deterministic Montgomery-compatible curve generation, and curve/twist
  sharing;
- conservative full-bound smoothness early abort using the pinned MIT engine
  and a content-authenticated portable 5.4 GB `p125` cache;
- rolling multi-curve execution against one shared immutable smooth cache,
  with deterministic in-order checkpoint and certificate publication;
- opt-in, schedule-bound X1(11) and X1(27) curve families with independently
  checked torsion, full rational `E[2]`, and optional point-order-four filters;
- exhaustive certificate-divisor search, exact Montgomery point-order checks,
  crash-safe checkpoints, identity-bound artifacts, and the pinned canonical
  verifier; and
- deterministic local, RunPod, and tagged/bounded AWS worker operations with
  non-overlapping range sharding and artifact retrieval.

Current algorithms and open outcome gates are documented in
[the search pipeline](docs/search_pipeline.md),
[the Weber implementation](docs/weber_implementation.md), and
[the bottleneck registry](docs/bottleneck_registry.md).  Reproducible measured
ablations, including limitations and still-missing comparisons, are collected
in [the performance report](docs/performance_report.md).

The native build needs GMP. Smooth-engine tests additionally need OpenMP
(`libomp` with Apple Clang; GCC's OpenMP runtime on Linux). Build and run the
local, CAS-free tests with:

```sh
make all
make test test-cli test-reference test-verifier test-vendor \
  test-performance-artifacts \
  test-smooth test-smooth-cache test-runpod
```

Run the full suite, including the independent Magma oracle, by setting its
local launcher:

```sh
MAGMA=/path/to/magma make test-all
```
