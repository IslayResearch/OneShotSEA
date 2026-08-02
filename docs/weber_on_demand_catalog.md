# Catalog-authenticated Weber levels through 997

## Purpose

The checked-in 77-level schedule through 401 is enough for representative
`p125` point counts, but pinning one complete manifest made every higher level
unusable even though the already content-addressed upstream archive contains
all admissible primes below 1000.  The source catalog removes that trust
ceiling without checking hundreds of megabytes of rarely used tables into Git.

This is selective materialization of specialized Weber-f modular polynomials,
not Sutherland's direct finite-field evaluation algorithm.  It is a bounded
bridge for `p125`, `p130`, and larger experiments while the full
isogeny-volcano/explicit-CRT evaluator remains future work.

## Trust chain

The accepted chain is:

1. `phi1.tar.gz` must have SHA-256
   `4ecc78a3163ba7232d67e3b2f5e678a2dbc038c7ee4a9d2e8c00c9e0b5a58176`.
2. Each upstream symmetric row is parsed with exact syntax, expanded into both
   orientations, and checked for monic normalization, the
   `X^ell Y^ell=-1` sign, and Weber weight sparsity.
3. `SOURCE_CATALOG.txt` binds the normalized size and SHA-256 of every one of
   the 166 admissible levels from 5 through 997.  The C++ binary pins catalog
   digest `031c35989f12d8f93c3a992014d6275edb93a21a3a9c70b4b78ce317e7db5dd5`.
4. A directory manifest may select any nonempty catalog subset.  Every actual
   `phi_*.txt` must be declared, every declaration must exactly match the
   catalog, and the runtime file bytes must match the declaration.
5. The search identity separately hashes the exact selected table contents
   through the configured maximum level.  Mid-run drift fails closed.

A forged table and a recomputed self-consistent manifest therefore still fail
unless the table bytes match the pinned source catalog.  Removing a valid table
can at worst make the SEA result incomplete: incomplete curves are not sound
rejections and do not advance the checkpoint under the default policy.

## Materialization

To build the ordinary complete schedule through 409:

```sh
python3 tools/fetch_weber_tables.py \
  --archive /path/to/phi1.tar.gz \
  --output /tmp/weber-through-409 --max-level 409
```

To build a compact experiment with only selected levels:

```sh
python3 tools/fetch_weber_tables.py \
  --archive /path/to/phi1.tar.gz \
  --output /tmp/weber-selected --levels 409,419,421,997
```

The output must not contain tables outside the requested selection.  This
prevents a narrower request from silently inheriting stale files.  Both modes
write the pinned catalog and a subset manifest, then immediately verify the
result.

The deterministic benchmark accepts an optional maximum level:

```sh
make build/benchmark_p125_poly_trusted
./build/benchmark_p125_poly_trusted sea /tmp/weber-selected 997
```

## 416-bit validation

On the fixed X1(27) p125 benchmark curve, a directory containing only level
409 produced two compatible normalized isogenies, one independently recovered
Frobenius eigenvalue and its validated conjugate, and exact trace residue
`19 mod 409`.  A retained independent Magma full count returned trace
`-534284869337319737295513917655253909609824180266230842767530862`,
which is 19 modulo 409 and also matches the exact family prior 418 modulo 432.
Timings were:

| Level | Result | Roots | BMSS | Eigenvalue | SEA total |
|---:|---|---:|---:|---:|---:|
| 409 | exact, two kernels | 0.683 s | 1.620 s | 2.795 s | 5.118 s |
| 997 | no rational neighbor | 2.088 s | 0 | 0 | 2.140 s |

The level-997 run is a top-of-catalog parsing, specialization, Frobenius, gcd,
and fail-closed no-root test; it is not positive residue evidence.  Replaying
the unchanged checked-in level-401 schedule produced the established exact
projection SHA-256
`8055a435d1abd535574867a55169168635ac683c2ed9e065df135d7440f4b8e6`.
The exact binary, build log, native projections, table manifests, Magma
launcher/runtime identities, point-count transcript, corrected 126-digit
order, and self-audit are retained in the
[checksummed evidence bundle](../artifacts/local/p125-weber-catalog-magma-20260802/README.md).

Unit and integration gates additionally cover selective offline
materialization, all 77 current files against the 166-level catalog, accepted
catalog subsets, missing/extra/altered files, catalog tampering, and a forged
self-consistent manifest.

## Asymptotic boundary

This change removes 401 as an implementation/trust ceiling and more than
doubles the available prime-level range.  It does not change the heuristic
`p^(1/8+o(1))` curve-search exponent, nor does it prove unbounded SEA scaling.
The finite source catalog stops at 997, and individual full table payloads grow
roughly quadratically with the level (the normalized level-997 table is about
10.8 MB).  A literal asymptotic implementation must generate or directly
evaluate the needed specializations as levels grow with `log p`, using the
isogeny-volcano/explicit-CRT algorithms rather than a finite archive.
