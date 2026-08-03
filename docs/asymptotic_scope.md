# Asymptotic scope and evidence

This note separates three claims that are easy to conflate:

1. the expected number of curves in the one-shot search;
2. the cost of SEA point counting for each curve; and
3. the constants and crossover of this implementation.

Only the first determines the advertised exponent in `p`. The other two must
remain polynomial in `log p` and practical enough not to erase the advantage.

## Outer one-shot search

Let `n = ceil(log2 p)`, let the certificate smoothness bound be `B=n^4`, and
let the verifier's lower bound be

```text
L = sqrt(p) + 2 p^(1/4) + O(1) = p^(1/2+o(1)).
```

The smooth-divisor model used by this project predicts that a random curve or
its twist has a `B`-smooth divisor greater than `L` with probability
`p^(-1/8+o(1))`. It therefore predicts `p^(1/8+o(1))` curve trials. This is a
smoothness and group-structure heuristic, not a theorem supplied by SEA and
not a runtime guarantee.

The exponent follows from the standard Dickman heuristic rather than from a
fit to these benchmarks. With `y=B=(log p)^4` and `L=p^(1/2+o(1))`,

```text
u = log L / log y = (1/8+o(1)) log p / loglog p.
```

The Dickman estimate `log rho(u)=-(1+o(1))u log u` then gives
`log rho(u)=-(1/8+o(1))log p`. A fixed forced divisor such as the X1(27)
cyclic divisor 108 replaces `L` by `L/108`; this can materially improve the
finite-p constant but cannot change the leading exponent. The group-order
divisor 432 is a useful sensitivity only because it need not occur in the
group exponent or as the order of one point.

The comparison `p^(1/4+o(1))` for the CM approach likewise describes its
heuristic discriminant-search term. A crossover near 400 bits depends on
constants, curve-family yield, group-exponent constraints, certificate
assembly, parallelism, and amortization. It has not been established by the
direct p125 point count.

## Per-curve SEA cost

To recover a trace in the Hasse interval, exact SEA moduli need a product of
size `p^(1/2+o(1))`, or total logarithmic mass `Theta(n)`. Under the usual
small-Elkies-prime heuristic, the required levels and all field and polynomial
objects have size polynomial in `n`.

Sutherland's direct-evaluation analysis gives, under GRH and its stated
selection assumptions, an expected Algorithm 1 cost

```text
O(ell^2 H log(H)^2 loglog(H)),
H = O(ell log ell + log q),
```

and space

```text
O(ell log q + ell^2 log H).
```

When `log q = Theta(ell)`, this is quasi-cubic in `ell`; applying it across SEA
levels gives a point-counting cost polynomial in `n=log q`. Such a factor is
`p^o(1)`, so it does not change the outer `p^(1/8+o(1))` heuristic.

The implementation follows the relevant architecture: it computes only
`Phi_ell(j,Y)` and `Phi_X(j,Y)`, streams explicit-CRT witnesses, and retains
two interpolation matrices per auxiliary prime instead of a full bivariate
target-level table. Its compact retained payload is exactly
`2 K (ell+2)^2` 64-bit coefficients for `K` CRT primes. This is the intended
polynomial shape, not an implementation-wide complexity proof.

## Why the current branch is not a literal asymptotic result

Four current boundaries prevent an unqualified claim for `p -> infinity`:

- Auxiliary surfaces use a checked 64-bit Montgomery field. There are only
  finitely many admissible primes below `2^64`, so this representation must be
  generalized even though it is ample for the demonstrated 416-bit run.
- The practical fixed-`v` auxiliary-prime selector is heuristic rather than
  the randomized theorem-aligned selector used in the asymptotic analysis.
- The p125 completing schedule contains only levels known to be exact from the
  table-backed oracle. A production search must use a curve-independent
  cost/yield policy and pay for attempted Atkin or otherwise unhelpful levels.
- This branch directly specializes classical `j`. The lower-constant Weber
  route still needs its class-polynomial authentication, relation selection,
  and normalization-specific height proof.

Schoolbook and Bareiss components are polynomial but may also have constants
or exponents too large for the practical crossover. That is an engineering
risk, not evidence of exponential scaling.

## Early abort and the exponent

The implemented sound screen runs only after the retained constraints can
enumerate every Hasse-compatible trace under the configured cap. For every
curve/twist order it computes the exact `n^4`-smooth part. Rejection is sound
only when every such smooth part is at most `L`; partial smoothness evidence
can retain a candidate but cannot reject it.

This can avoid finishing the point count for most losing curves and therefore
reduce average per-curve cost. It does not increase the probability that a
curve has a certifying order, so it does not change the heuristic `p^(1/8)`
number of trials. Candidate or prime-search cap exhaustion is an incomplete
computation and must never be counted as mathematical rejection.

## What the p125 run proves

The retained validation establishes that:

- 30 independently prepared direct levels through `ell=271` can reconstruct
  a unique trace of a real 416-bit curve;
- all direct residues match the authenticated table-backed specialization;
- the complete run fits a one-context-at-a-time local process; and
- after the hot-path changes, the summed observed work was about 21.1 minutes
  with a largest single retained payload of about 614 MB.

It does not establish certificate yield, a production schedule, the behavior
on many independent curves, p130 scaling, or the CM crossover. The full paths
also share downstream BMSS/Frobenius and trace-state code; only selected small
levels have a separate Schoof oracle.

## Evidence needed for the intended claim

The next convincing checkpoints are:

1. Select levels without knowing their split type, then measure information
   gained per preparation and per-curve evaluation cost over many p125 curves.
2. Amortize each prepared context across enough curves to measure the actual
   early-abort cost distribution rather than a single full trace.
3. Complete an end-to-end certificate and record curve trials, smoothness
   survivors, group-exponent failures, and assembly failures.
4. Repeat the same fixed policy at p130 and fit level, memory, and per-curve
   growth against `n` and `ell`.
5. Remove the 64-bit auxiliary-prime ceiling and replace the fixed-`v`
   selector before describing the implementation as asymptotic for unbounded
   inputs.
6. Compare end-to-end SEA and CM runs under the same target, hardware,
   certificate definition, and stopping rule.

At the current 416-bit target, the checked Dickman--Mertens model predicts
120,490 curves for the optimistic full-E[2] baseline, 95,726 for the
conservative X1(27) cyclic-divisor scenario, and 85,875 for the X1(27)
group-divisor sensitivity. These are smooth-factor opportunities, not
certificate-success estimates. They use the older measured 27.07-second
ten-curve throughput only as a planning input; concurrent production
throughput for the selected-20 direct-first policy remains unmeasured.

Until then, the precise claim is: the branch implements the right direct-SEA
architecture and demonstrates a practical complete point count at 416 bits;
the `p^(1/8+o(1))` advantage remains a well-motivated outer-search heuristic,
not a measured or proved end-to-end complexity result.

## References

- R. Bröker, K. Lauter, and A. Sutherland,
  [*Modular polynomials via isogeny volcanoes*](https://arxiv.org/abs/1001.0402).
- A. Sutherland,
  [*On the evaluation of modular polynomials*](https://arxiv.org/abs/1202.3985).
