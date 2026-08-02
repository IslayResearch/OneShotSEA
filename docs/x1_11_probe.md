# Bounded X1(11) torsion probe

This note documents the isolated X1(11) library and CLI probe implemented in
[the probe interface](../include/oneshotsea/x1_11_probe.hpp) and
[implementation](../src/x1_11_probe.cpp). The authenticated p125 measurement
is recorded in the
[compact result](../artifacts/local/p125-x1-11-probe-20260801/result.json).

The retained measurement below is a bounded experiment: it did not advance a
production cursor, alter the default curve schedule, or feed curves into SEA
or certificate assembly. At capture time, the working-tree and `HEAD` Git blob
identities were identical for both production files:

| File | Git blob |
|---|---|
| `src/search_pipeline.cpp` | `edbfba47d89c35bcf65e7adf29eae01293cbc0a6` |
| `src/weber_curve_generator.cpp` | `5a3391179634f465671c1aded085de1687c7cd11` |

After that capture and its independent checks, the same construction was
integrated as the explicit `--curve-family x1-11` production option.  Its
unbounded deterministic retry maps each `(seed,global_index)` to one admitted
curve, and `--x1-require-point4 1` selects the stronger branch.  Generator
version, formula digest, and point-four choice are schedule-bound.  The
default `weber-f` generator and its published schedule identity remain
byte-for-byte unchanged, and the X1 path does not use its divisor metadata to
bypass exact SEA, smoothness, assembly, or verifier gates.

## Construction and invariants

The implemented equation is the optimized MIT model
[X1opt11.txt](https://math.mit.edu/~drew/X1/X1opt11.txt):

```text
y^2 + (x^2 + 1)y + x = 0
r = xy + 1
s = 1 - x
```

The downloaded source hashed to
`19f76aef352cea9a6e1d3347977eb9286b03e70fa6b4afb8daea013ebbd6bd4c`.
This digest is compiled into the probe and emitted in every summary. With
`c=s(r-1)` and `b=rc`, the corresponding Tate normal form is

```text
v^2 + (1-c)uv - bv = u^3 - bu^2,
```

with distinguished point `(0,0)`. For `a=c-1` and `e=a^2-4b`, the probe uses
the short model

```text
Y^2 = X^3 + A X + B
A = 27(24ab-e^2)
B = 54(e^3-36abe+216b^2)
P = (3e,-108b).
```

Characteristics at most seven, characteristic eleven, composites, and primes
not congruent to one modulo four are rejected. Each nonsingular,
nonexceptional sample is checked directly to ensure that `P` is finite, lies
on the short curve, and satisfies `[11]P=O`. Since eleven is prime and
`P != O`, this proves that `P` has exact order eleven.

For every rational Weber lift `f`, the fixed normalization is

```text
j = (f^24-16)^3/f^24.
```

The explicit Montgomery gate sets

```text
U = 4 - f^24/16
A_M^2 = U
M: y^2 = x^3 + A_M x^2 + x.
```

The implementation independently validates the resulting Montgomery
invariant. Moreover,

```text
A_M^2 - 4 = -f^24/16 = -(f^12/4)^2.
```

Because p125 is one modulo four, `-1` is a square. Thus the Montgomery cubic
splits completely, and full rational `E[2]` is forced. Full rational
2-torsion is invariant under quadratic twist, so it also holds on the selected
Tate twist class; the code still factors that class's short cubic and requires
three roots as a runtime check.

For nonexceptional equal-j short curves, the probe identifies whether the Tate
model belongs to the canonical Weber curve or twist. If `(A_T,B_T)` is the
Tate model and `(A_C,B_C)` is the canonical model, it computes

```text
w = B_C A_T / (B_T A_C)
```

and validates `w^2=A_C/A_T` and `w^3=B_C/B_T`. A square `w` selects the
canonical curve; a nonsquare selects its quadratic twist.

The optional order-four decision follows Proposition 2 of
[Sutherland, *Constructing elliptic curves over finite fields with prescribed
torsion*](https://arxiv.org/pdf/0811.0296). For p125's `p=1 mod 4` and a cubic
with three rational roots, this is a bounded sequence of quadratic-character
tests. No point enumeration or unbounded retry is used.

The resulting guarantees are:

| Condition | Exact cyclic point | Known subgroup/order divisor |
|---|---:|---:|
| exact order 11 plus full rational `E[2]` | 22 | 44 |
| additionally a rational point of order 4 | 44 | 88 |

The opposite quadratic-twist order is `2(p+1)-#E`. For p125 this is congruent
to 12 modulo 44, and in the order-four branch it is congruent to 12 modulo 88.

## Bounded p125 measurement

Both modes used the same deterministic inputs: seed `202607300000`, indices
`[0,16)`, and at most 64 sampled X1(11) x-coordinates per index. A miss after
64 samples is returned as a normal result; it does not start an unbounded
retry or affect any production-search index.

| Counter | Full `E[2]` baseline | Require point four |
|---|---:|---:|
| `x_samples` | 411 | 817 |
| `x_polynomials_without_roots` | 217 | 409 |
| `x1_points` | 381 | 814 |
| `singular_curves` | 0 | 0 |
| `exceptional_j` | 0 | 0 |
| `exact_order_11_failures` | 0 | 0 |
| `points_without_weber_lifts` | 356 | 753 |
| `weber_lifts` | 372 | 804 |
| `nonsquare_explicit_montgomery_u` | 144 | 408 |
| `points_without_explicit_montgomery_model` | 12 | 34 |
| `full_two_torsion_failures` | 0 | 0 |
| `point_four_rejections` | 0 | 20 |
| `accepted` | 13 | 7 |
| internal elapsed time | 5.295582 s | 11.176147 s |

The terminal counter partition is exact. In baseline mode,
`381 = 356 + 12 + 13`; in order-four mode,
`814 = 753 + 34 + 20 + 7`. Every visited X1(11) point therefore has exactly
one terminal outcome. There were no singular, exceptional-j, wrong-order, or
full-2-torsion failures.

The baseline accepted 13 of 16 bounded indices and 13 of 381 visited X1
points. Its accepted indices were
`0,2,3,4,5,6,7,8,9,10,13,14,15`; the first retained model had order four.
The stricter mode accepted indices `0,3,7,8,9,14,15`. It encountered 27
explicit models, rejecting 20 without order four and accepting 7, an observed
conditional fraction of `7/27`. Requiring order four used 1.988 times as many
x-samples and 2.110 times the elapsed time on this range.

These timings were obtained from a sequential replay of the two commands on
the local host. They are small probe timings, not end-to-end search timings.

## Independent Magma evidence

The oracle was local Magma 2.29-1 at capture time. Its launcher SHA-256 was
`e62e9d7098bbc60525acea1527b1b514873410147ec5583c1a26768650f8cff8`.

For point-four index 0, Magma counted the selected Tate curve as

```text
100000000000000000000000000000000000000000000000000000000000000026639532912113683603076081938932761119093105874891301893142256.
```

This is zero modulo 88. Its opposite twist order is

```text
99999999999999999999999999999999999999999999999999999999999999973360467087886316396923918061067238880906894125108698106858220,
```

which is 12 modulo 88. A separate Magma identity transcript returned
`<true, 3, true, true>` for `[11]P=O`, three rational roots of the 2-torsion
cubic, `U` square, and `A_M^2=U`, respectively.

For baseline index 2, which did not have a point of order four, Magma counted

```text
99999999999999999999999999999999999999999999999999999999999999896901967624193470456810908910824056234283122066685971427202076.
```

This is zero modulo 44 and 44 modulo 88, independently distinguishing it from
the point-four branch. Its opposite twist is 12 modulo 44.

## Reproduction

The measured executable was built from the pre-commit worktree based on
`f8347c68ef357107e3bfead0107e947e1c4c4500`. Its SHA-256 was
`635310030a69ff5ba7194f784cf5d0669e8d936f9961da68ce2043fef351c781`.
Because the implementation was not yet committed, the binary and individual
source hashes in the compact result, rather than the base commit alone, bind
the evaluated code.

From the repository root:

```sh
P='100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237'

./build/oneshotsea x1-11-probe \
  --p "$P" --seed 202607300000 --range-start 0 --count 16 \
  --max-x-samples 64 --require-point4 0 \
  > /tmp/baseline-sequential.ndjson

./build/oneshotsea x1-11-probe \
  --p "$P" --seed 202607300000 --range-start 0 --count 16 \
  --max-x-samples 64 --require-point4 1 \
  > /tmp/require-point4-sequential.ndjson
```

For the two independent order counts:

```sh
MAGMA='/Users/agent/Documents/Codex/t24-search/private/magma-local/install/magma'

python3 oracle/point_count.py --magma "$MAGMA" "$P" \
  47801692813922329994097794431438996459031933574155894960217502436156220113632764855795851266039155129841363019718790157694558 \
  5903073683285627166559764748048329982630044547416577377293698924199026531579763130421404281751497201672532112597795628144784

python3 oracle/point_count.py --magma "$MAGMA" "$P" \
  61890067466599262857653612553637886602736875528811152214070421173948829034419041168055286424862449689753021813090203629627528 \
  45284866823353380483762289005158254951817246607712339986752126956035464398412320403215506530415727059515803884705334177968200
```

The exact Magma identity program and `-b -e` invocation are recorded as the
`commands.magma_identity_program` and `commands.magma_identity_invocation`
fields in the compact result. It reconstructs the retained
`<true, 3, true, true>` transcript without relying on an untracked script.

The original retained files were host-local and intentionally not checked in:

| Raw output | SHA-256 |
|---|---|
| `baseline-sequential.ndjson` | `612c259ef11d50c101a6943197467f05b2da6e753114f2ef62cb86b6ab7ffdff` |
| `require-point4-sequential.ndjson` | `1f79824c721be2c898d74de61d685a87a100eb3167504efb43d88bedd9a233ec` |
| `magma-point4-order.json` | `0add3265586beaac583090a85fe78643171e37a27c94ca00999902d090f715ca` |
| `magma-no-point4-order.json` | `c14afcb89a7cd6cb5ccb06c8b31aad06e14a141fdfdacb74f9589690522527ce` |
| `magma-point4-identities.txt` | `df175647882033815250ef7200cb444750d52f477553d1d26e215ba8762667d1` |

The focused validation commands were `make test-x1-11-probe`,
`make test-cli` (14 cases), and `make test-weber-curve-generator`; all passed.

## Limitations

- A known factor of 44 or 88 creates a smooth-order opportunity. It is not a
  measured certificate rate. SEA, exact smooth-part extraction, the required
  large-prime conditions, Montgomery certificate assembly, and canonical
  verification remain mandatory.
- `group_divisor` is a divisor of the rational group order and the size of a
  known subgroup. It is not the group exponent and does not claim a point of
  exactly that order. `cyclic_divisor` is the separate 22- or 44-order point
  obtained by combining the exact coprime torsion points.
- The acceptance fractions are observations from one deterministic 16-index
  range under the X1(11)-first distribution. They are not confidence
  intervals, uniform-j estimates, or production-search rates.
- The observed `7/27` point-four fraction is conditional on explicit models
  reached before each bounded index stopped. It is not a smoothness or
  certificate probability.
- The elapsed values cover only X1 sampling, finite-field validation, and
  Weber/Montgomery lifting. They exclude SEA, smoothness work, certificate
  assembly, and verification.
