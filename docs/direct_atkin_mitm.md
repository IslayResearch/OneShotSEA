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
removed from this path. Let `n = deg(f)`. For square-free `f`, the
implementation first requires `X^(p^n) = X mod f`. It then starts with
`r = n` and removes every prime divisor `q` for as long as
`X^(p^(r/q)) = X mod f`. Finally, for every prime `q | r`, it checks that

`gcd(f, X^(p^(r/q)) - X) = 1`.

The first identity proves that every irreducible factor degree divides `n`.
The divisor-lattice reduction computes their least common multiple `r`; the
gcd checks then exclude every proper divisor of `r`, and square-freeness
excludes multiplicity. Thus every factor has degree exactly `r`.

The implementation constructs `X^p mod f` once and obtains the maps for
`1, 2, 4, ...` Frobenius iterations by modular self-composition. Each queried
exponent is assembled in binary. This replaces the former linear walk through
all `n` Frobenius iterates with `O(log n)` stored maps and
`O(log^2 n)` modular compositions in the worst case, apart from polynomial and
field bit costs. Here `n = ell + 1`, so this is a meaningful direct-SEA
constant/polylogarithmic improvement; it does not alter the outer heuristic
power of the one-shot curve search.

## Validation evidence

The checked-in tests compare factored counting, materialized residues, and
bounded enumeration against exhaustive scans over four small fields, including
cyclic Hasse-range wraparound. A synthetic p125 case exercises 6,635,520 full
CRT combinations without materializing them. The factor-degree certificate is
compared with exhaustive tiny-field factorization and constructed 416-bit
products. Cold concurrent reads exercise every shared lazy-cache consumer, and
the complete core suite passes under ASan, UBSan, and ThreadSanitizer.

On the fixed 16-curve p125 high-level cohort (`ell = 29, 101, 157`), the
divisor-lattice implementation preserved the exact/Atkin classifications and
residues while reducing total level evaluation from 119.609468 seconds to
49.418415 seconds (2.42x). The `ell = 157` aggregate fell from 93.526955
seconds to 33.913631 seconds (2.76x). This measures the whole level consumer,
not an isolated microbenchmark, and does not include curve generation or cache
indexing.

The retained controlled p125 cohort in
[`artifacts/local/p125-direct-atkin-mitm-20260803`](../artifacts/local/p125-direct-atkin-mitm-20260803)
compares candidate `13cd3a9` with its parent `bbcd04d` using the same profiler,
host, compiler/flags/libraries, authenticated cache, and byte-identical command
arguments. It contains one unique ordered 16-by-15 grid: 240 semantically
identical pre/post level records, 64/64 independent Schoof agreements, and zero
unconstrained levels. The raw records exactly recompute the curve and summary
aggregates.

Direct evaluation improved from 427.991317 seconds to 39.033330 seconds
(10.9648x), peak RSS improved from 1,159,053,312 to 40,370,176 bytes
(28.7106x), and whole-cohort elapsed time improved from 567.560778 to
195.298315 seconds (2.9061x). This is evidence for the complete combined
candidate patch—factored CRT MITM, the uniform factor-degree certificate, and
baby-step/giant-step modular composition—not an isolated measurement of MITM.
It supersedes an earlier 7.79x draft comparison whose two retained runs did not
have auditable identical configurations. `provenance.json` records the exact
source trees and capture-time tool, library, binary, cache, host, command, and
raw-log identities; `audit.py` derives every retained result from the raw logs.

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
