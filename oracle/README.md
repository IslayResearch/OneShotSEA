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

## Tests

Run the oracle tests from the repository root:

```sh
MAGMA=/path/to/magma python3 oracle/test_point_count.py -v
```

The fixtures include `j=0`, `j=1728`/supersingular, ordinary, negative
coefficient, small-field, and medium-field curves.  Their expected orders are
fixed and independently checked by a definition-level exhaustive counter
before they are compared with Magma.  Invalid singular and composite inputs
are also tested.
