# Factored Atkin constraints for direct SEA

## Intended purpose

The direct SEA tail often learns an exact trace residue at an Elkies level but
only a certified set of possible residues at an Atkin level. The former
implementation immediately formed the full CRT Cartesian product of every
Atkin set. That is mathematically correct, but it makes an otherwise practical
direct SEA run consume time and memory proportional to the product of the set
sizes.

This implementation keeps each coprime constraint `(m_i, A_i)` factored and
performs exact Hasse-interval counting and bounded enumeration with a balanced
meet-in-the-middle search. It exists to make certified Atkin information usable
for sound early screening without turning ambiguity bookkeeping into the main
cost of SEA.

## Exact counting algorithm

Let `M = product(m_i)` and let `R = product(|A_i|)`. Every selection of one
residue from each `A_i` defines a unique residue modulo `M`. CRT writes that
residue as a sum of independent component contributions modulo `M`.

The components are greedily balanced by their set cardinalities into two
halves. Each half residue list is materialized and sorted. For the inclusive
Hasse interval `[L,U]`, write its size as `q*M+s`, with `0 <= s < M`. Every CRT
residue occurs exactly `q` times, and residues in one cyclic interval of length
`s` occur once more. The latter count is computed exactly by shifting that
cyclic interval for every element of the smaller half and using binary searches
in the other sorted half. Wraparound is split into two disjoint linear ranges.

Bounded enumeration first obtains the exact count. If the interval spans a
complete modulus cycle and the count fits the caller's cap, the full residue
set necessarily fits too. Otherwise only matching half-pairs in the Hasse
interval are visited. The resulting traces are sorted and checked against the
exact count with duplicate rejection.

The old ambiguous-state cost was `Theta(R)` space and at least `Theta(R)` work.
With balanced halves the new cost is `O(sqrt(R))` space and
`O(sqrt(R) log R)` range-counting work, apart from multiprecision bit costs.
Highly indivisible component cardinalities can make the greedy split less
balanced, so the precise bound is in terms of the larger half list.

## Atkin factor-degree certificate

The direct classical specialization only needs the common irreducible factor
degree, not the factors. Full Cantor-Zassenhaus splitting has therefore been
removed from this path. For square-free `f`, the implementation finds the
smallest `r` for which `X^(p^r) = X mod f`. It then checks, for every prime
`q | r`, that

`gcd(f, X^(p^(r/q)) - X) = 1`.

The first identity proves that every irreducible factor degree divides `r`.
The gcd checks exclude every proper divisor of `r`; square-freeness excludes
multiplicity. Thus every factor has degree exactly `r`. Frobenius iteration
reuses one `X^p mod f` map through baby-step/giant-step modular composition.

## Validation evidence

The checked-in tests compare factored counting, materialized residues, and
bounded enumeration against exhaustive scans over four small fields, including
cyclic Hasse-range wraparound. A synthetic p125 case exercises 6,635,520 full
CRT combinations without materializing them. The factor-degree certificate is
compared with exhaustive tiny-field factorization and constructed 416-bit
products. Cold concurrent reads exercise every shared lazy-cache consumer, and
the complete core suite passes under ASan, UBSan, and ThreadSanitizer.

The retained fixed p125 cohort in
[`artifacts/local/p125-direct-atkin-mitm-20260803`](../artifacts/local/p125-direct-atkin-mitm-20260803)
contains 240 semantically identical pre/post level records, 64/64 independent
Schoof agreements, and zero unconstrained levels. Direct evaluation improved
from 263.265228 seconds to 33.784948 seconds (7.79x), while peak RSS improved
from 1,253,654,528 to 40,943,616 bytes (30.62x).

## Asymptotic scope and review boundary

This change repairs the asymptotic behavior of the Atkin ambiguity subroutine;
it does not by itself prove an end-to-end `p^(1/8+o(1))` one-shot proof search.
That overall exponent remains a heuristic curve-yield claim and also depends on
the available auxiliary levels, direct-specialization construction, smoothness
yield, and certificate assembly. The current implementation still limits
auxiliary primes to machine-word levels, and a complete p125/p130 direct-first
certificate/yield campaign remains to be measured.

This is worth specialist review because it changes a correctness-critical
boundary while removing the dominant measured resource failure. In particular,
review should confirm the cyclic interval pair count/enumeration, transactional
constraint updates, the Frobenius/gcd uniform-degree proof, and the claim split
between the proven `sqrt(R)` ambiguity reduction and the still-heuristic global
`p^(1/8+o(1))` search scaling. Those are exactly the pieces needed to decide
whether the branch now serves its intended purpose: enabling sound, early-abort
direct SEA experiments beyond 400 bits without hiding ambiguity in an
exponential Cartesian-product implementation.
