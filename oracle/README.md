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
Git commit and worktree state, host and Python identity, native executable,
Magma engine and parsed runtime version, both Magma programs, bootstrap,
driver, record count, and SHA-256 digest. The native executable and
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
scientific driver and both Magma programs before a fresh Python interpreter
loads them; those exact executing bytes are then copied into the artifact and
rechecked at completion.

This driver audits the independent native Schoof path. It does **not** expose
or certify every intermediate root/classification in the production Weber-f
pipeline, and it is not the separate 10,000-curve sound/heuristic early-abort
state audit described in `docs/sea_design.md`. Do not claim either requirement
from a successful corpus run. A failed comparison writes a failed manifest and
stops at the first mismatch; completed records already flushed to disk remain
hashed as a partial artifact. An interrupt is recorded separately and exits
with status 130.

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

The fixtures include `j=0`, `j=1728`/supersingular, ordinary, negative
coefficient, small-field, and medium-field curves.  Their expected orders are
fixed and independently checked by a definition-level exhaustive counter
before they are compared with Magma.  Invalid singular and composite inputs
are also tested.
