# Magma point-counting oracle

This directory provides an independent correctness oracle for the custom SEA
implementation.  It counts the points on the nonsingular short Weierstrass
curve

```text
E: y^2 = x^3 + a*x + b over GF(p)
```

for a prime `p > 3`, and reports `#E(F_p)` and the Frobenius trace
`t = p + 1 - #E(F_p)`.  It is test/validation infrastructure only.  Nothing in
the production search may import it, invoke it, or require Magma.

## Invocation

The Python wrapper accepts base-10 integers and prints exactly one compact JSON
object on standard output:

```sh
python3 oracle/point_count.py --magma /path/to/magma 97 2 3
```

```json
{"p":97,"a":2,"b":3,"order":100,"trace":-2}
```

The coefficients in the response are their canonical representatives modulo
`p`.  Arbitrarily large and negative input coefficients are accepted.  Errors
are written to standard error and return a nonzero status.

Instead of `--magma`, set `MAGMA` or put a `magma` launcher on `PATH`:

```sh
MAGMA=/path/to/magma python3 oracle/point_count.py 97 2 3
```

On the current development machine Magma is installed outside `PATH`, so the
exact command is:

```sh
MAGMA=/Users/agent/Documents/Codex/t24-search/private/magma-local/install/magma \
  python3 oracle/point_count.py 97 2 3
```

The installed launcher reports its version in the interactive banner, not via
GNU-style `--version` (which it rejects):

```sh
printf 'quit;\n' | /path/to/magma
```

The current installation reports `Magma V2.29-1`.

## Deterministic corpus audit

`corpus_audit.py` builds a deterministic, streaming differential corpus over
explicit bit-size buckets. Each nonsingular short-Weierstrass curve is counted
by Magma. Magma's exact `IsPrime` gate validates every deterministically
generated field characteristic before it is supplied to the native program.
The native definition-level Schoof implementation must reproduce the configured
trace residues at every size and, for feasible smaller buckets, the complete
order and trace. Echoed inputs, canonical encodings, the Hasse bound, the list
and product of completed Schoof levels, and the sufficient CRT modulus are all
checked fail-closed. The output directory is create-only and contains a
line-oriented record stream plus a manifest binding the complete invocation,
Git commit and worktree state, host and Python identity, native executable and
its non-system dynamic dependencies, Magma engine, installation dependency
tree, and parsed runtime version, both Magma programs, bootstrap, driver,
record count, and SHA-256 digest. The native executable and
oracle sources are copied into a per-run `inputs/` snapshot directory and
executed from there. All source and snapshot hashes are rechecked before a run
can be marked complete.

A one-curve-per-size breadth smoke test is:

```sh
python3 oracle/corpus_audit.py \
  --magma-runtime /path/to/actual/magma-executable \
  --magma-root /path/to/magma-installation \
  --native build/oneshotsea \
  --output-dir artifacts/local/oracle-corpus-smoke-20260802 \
  --seed 202608020001 \
  --bit-sizes 16,32,64,128,256,416 \
  --curves-per-size 1 \
  --complete-count-max-bits 32 --max-ell 19 \
  --residue-levels 3,5,7 \
  --command-timeout-seconds 3600 --max-output-bytes 1048576 \
  --max-prime-attempts 1000000
```

The planned 10,000-curve small/medium complete-count audit is explicit rather
than hidden behind a test default:

```sh
python3 oracle/corpus_audit.py \
  --magma-runtime /path/to/actual/magma-executable \
  --magma-root /path/to/magma-installation \
  --native build/oneshotsea \
  --output-dir artifacts/local/oracle-corpus-10000-20260802 \
  --seed 202608020002 \
  --bit-sizes 16,20,24,28,32 \
  --curves-per-size 2000 \
  --complete-count-max-bits 32 --max-ell 19 \
  --residue-levels 3,5,7 \
  --command-timeout-seconds 3600 --max-output-bytes 1048576 \
  --max-prime-attempts 1000000
```

The timeout and output cap apply independently to each native or Magma child
process. Both must be positive, and the output cap may not exceed 64 MiB. The
prime-attempt limit bounds the total deterministic SHAKE candidates considered
for each field, including candidates removed by the Miller-Rabin prefilter. The
native reference Schoof implementation currently limits `--max-ell` and every
`--residue-levels` entry to odd primes at most 37. Singular deterministic curves
consume their index before the driver advances, so records never silently
reuse a curve index. A successful final record at `UINT64_MAX` reports a null
next cursor, meaning that the index space is exhausted. Complete-count runs
independently re-run and compare every Schoof level reported by the completion,
even when it was not named in `--residue-levels`.

`--magma-runtime` is the actual engine executable and `--magma-root` is its
installation root. The driver invokes that bound engine directly; it does not
trust a user-supplied launcher to select the asserted binary. For the local
installation these are, respectively:

```text
/Users/agent/Documents/Codex/t24-search/private/magma-local/install/bin/magma.exe
/Users/agent/Documents/Codex/t24-search/private/magma-local/install
```

The driver clears runtime path overrides, reconstructs the standard Magma paths
from the bound root, disables user startup files, and forces single-threaded
numerical-library settings for oracle calls. A small bootstrap copies the
scientific driver, shared audit module, and both Magma programs before a fresh
Python interpreter loads them. The driver refuses direct execution outside
that bootstrap context and attests the complete loaded module code, including
nested code objects, against the copied source without executing the original
source. Those exact executing bytes and the native/Magma dependency identities
are copied into the artifact and rechecked at completion.

This driver audits the independent native Schoof path. It does **not** expose
or certify every intermediate root/classification in the production Weber-f
pipeline, and it is not the separate 10,000-curve sound/heuristic early-abort
state audit described in `docs/sea_design.md`. Do not claim either requirement
from a successful corpus run. A failed comparison writes a failed manifest and
stops at the first mismatch; completed records already flushed to disk remain
hashed as a partial artifact. An interrupt is recorded separately and exits
with status 130.

## Production Weber-state corpus audit

`weber_corpus_audit.py` exercises the actual deterministic Weber curve
generator and production Weber SEA runner.  For each deterministically generated
prime it asks Magma for the curve and twist orders, then independently replays
every emitted exact and effective CRT state.  The replay checks the known
Weber-f lift and its map to `j`, the canonical curve model, the least-nonsquare
quadratic twist, the full-rational-2-torsion trace prior, exact Elkies residues,
certified Atkin projective orders and their complete allowed residue sets,
retained exact Schoof fallback state, all candidate counts and trace lists, and
the final certified singleton, with exact-only provenance reported separately.
The curve and twist orders must sum to
`2*p+2`, and no intermediate constraint may eliminate the Magma trace.
`unconstrained` means that no residue evidence was committed, not that the
Frobenius discriminant was mathematically unclassifiable.  In particular, a
trace-zero supersingular specialization at trusted level 5 or 7 may be
non-square-free and therefore fail closed as unconstrained; when the classical
factor pattern is usable, its certified Atkin order and full residue set are
still replayed.  A square-discriminant Weber specialization may likewise fail
to produce a usable normalized codomain/eigenvalue pair and remain
unconstrained; its CRT state must be unchanged, while every claimed exact
Elkies residue is still checked against Magma.  Ordinary nonsquare downgrades at
trusted levels 5 and 7 are rejected.

The table source is copied into the create-only artifact before execution.  The
native emitter authenticates that copied table set, while the driver binds and
completion-rechecks every original and copied Weber/classical-j table file.
Nondeterministic timing counters are type/range checked but deliberately omitted
from `records.ndjson`, so two identical scientific runs have the same record
digest.  This first production-state slice explicitly records
`smoothness_audited:false`; it must not be cited as an audit of the smooth-part
engine or sound/heuristic early-abort decision.

Corpus/record schema v2 also stores a separately executed, fully validated
fallback-off native counterfactual for the same prime and curve index.  It
captures both ways the implemented heuristic policy can skip work: failure to
reach the configured first-pass trace cap, and failure of the fresh exact
cap-one continuation after a sound smoothness survivor.  The counterfactual is
run with `schoof_fallback=0`; the offline report combines it only with the
explicit `skip_incomplete_curves=1` policy and never attributes those skips to
sound rejection.

A direct-runtime breadth smoke is:

```sh
python3 oracle/weber_corpus_audit.py \
  --magma-runtime /path/to/actual/magma-executable \
  --magma-root /path/to/magma-installation \
  --native build/oracle_weber_audit \
  --table-dir data/modpoly/weber_f \
  --output-dir artifacts/local/weber-oracle-smoke-20260802 \
  --seed 202608020003 \
  --bit-sizes 16,24,32,48,64,96,128,192,256 \
  --curves-per-size 1 --max-level 193 --trace-cap 16 --sea-threads 1 \
  --command-timeout-seconds 3600 --max-output-bytes 8388608 \
  --max-prime-attempts 1000000
```

A bounded representative multi-limb capture using that same driver is retained
at
[`artifacts/local/weber-oracle-multilimb-20260803`](../artifacts/local/weber-oracle-multilimb-20260803/README.md).
It contains complete native/Magma curve and twist counts at 64, 128, 256, and
416 bits, including every normalized intermediate exact-Elkies,
certified-Atkin, unconstrained, and exact-Schoof record.  The accepted capture
was rebuilt and run from a clean detached checkout of its named commit; its
exact executable and capture/oracle scripts are retained.  A fresh checkout
can authenticate and replay all retained mathematical claims without a Magma
license:

```sh
python3 oracle/retained_weber_corpus.py \
  artifacts/local/weber-oracle-multilimb-20260803
```

The existing `test-weber-corpus` target runs this compact gate.  It complements
the statistically broad 16-32-bit corpus below; four deterministic curves are
representative multi-limb coverage, not a yield or random-population claim.

The same create-only, bounded-child, direct Magma runtime/root, full-module
bootstrap attestation, source/dependency snapshot, identity drift,
partial-manifest, interrupt, and `UINT64_MAX` cursor contracts as the
reference-Schoof corpus apply. `--trace-cap` may not exceed 4096, and the
deterministic Weber generator is rejected if it requires more than 4096
admission candidates for a record; these hard bounds prevent hostile or corrupt
native output from turning the independent replay into unbounded work.

For the required small/medium early-abort corpus, use only buckets through 32
bits so an independent trial-factor audit can prove exact `n^4`-smooth parts
without importing the production smooth cache:

```sh
python3 oracle/weber_corpus_audit.py \
  --magma-runtime /path/to/actual/magma-executable \
  --magma-root /path/to/magma-installation \
  --native build/oracle_weber_audit \
  --table-dir data/modpoly/weber_f \
  --output-dir artifacts/local/weber-oracle-10000-20260802 \
  --seed 202608020005 \
  --bit-sizes 16,20,24,28,32 --curves-per-size 2000 \
  --max-level 193 --trace-cap 16 --sea-threads 1 \
  --command-timeout-seconds 3600 --max-output-bytes 8388608 \
  --max-prime-attempts 1000000

python3 oracle/weber_early_abort_audit.py \
  artifacts/local/weber-oracle-10000-20260802 \
  --output artifacts/local/weber-early-abort-10000-20260802.json
```

The offline audit streams the canonical corpus and rechecks its manifest and
record digest, reconstructs the complete early trace set from the effective
CRT residue classes, requires the Magma trace even when it occupies the least
favorable list position, and independently trial-factors every curve/twist
order.  It reports the exact sound policy and the explicit
`--schoof-fallback 0 --skip-incomplete-curves 1` counterfactual separately,
including first-pass and cap-one second-pass skips.  In particular, a
heuristic false negative means the true curve or twist has exact smooth part
above `L`; it is never folded into the zero-false-negative sound result.  The
tool deliberately refuses buckets above 32 bits rather than attempting an
unbounded trial factorization.

## Tests

Run the oracle tests from the repository root:

```sh
MAGMA=/path/to/magma python3 oracle/test_point_count.py -v
```

The corpus driver's fail-closed streaming and identity contracts use fake
executables and need no Magma license:

```sh
python3 tests/test_oracle_corpus_audit.py -v
```

The production Weber emitter and corpus replay have a real native p=101 Atkin
plus retained-fallback fixture and a reproducible fake-Magma end-to-end run:

```sh
make test-weber-audit
make test-weber-corpus
make test-weber-early-abort-audit
```

The fixtures include `j=0`, `j=1728`/supersingular, ordinary, negative
coefficient, small-field, and medium-field curves.  Their expected orders are
fixed and independently checked by a definition-level exhaustive counter
before they are compared with Magma.  Invalid singular and composite inputs
are also tested.
