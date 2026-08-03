# p125 certified-Atkin singleton completion

This artifact records a fixed-curve policy comparison at the 416-bit target
`nextprime(10^125) = 10^125 + 237`. Both runs used deterministic X1(27)
point-four curve index 2,000,004, the same selected 20-level direct schedule,
the same authenticated direct context, the same smooth cache, and the same
Weber table manifest.

At Weber level 379, the exact-Elkies CRT still represented 221,262 traces in
the Hasse interval. The intersection with independently certified Atkin
constraints represented exactly one trace. The old cap-one policy ignored
that proof and evaluated levels 383, 389, 397, and 401 before failing closed as
`sea_level_limit`, even though the effective candidate count remained one.

Implementation commit `78a4ec5f615aaa836cdc9d8cb308fb572c526b1b`
allows the complete exact-plus-certified-Atkin intersection to satisfy the
unique-trace gate. The candidate stopped at level 379, emitted the unique
trace, screened both curve/twist orders exactly, and ended in the sound
`sound_smoothness_reject` state. Exact-only and effective candidate counts
remain separate in telemetry; Atkin evidence is not relabelled as an exact
Elkies residue.

## Independent validation

The emitted trace is

```text
432966650303160993124127306120296021107647349914430129038843294
```

It matches the previously retained PARI/GP 2.17.4 point count for this exact
generated curve. The native tests also cover two independent small-field
routes: a checked-in table specialization and a table-free direct
specialization, each compared with brute-force point counting. The Weber
corpus auditor separately reconstructs certified Atkin residue sets and now
accepts a certified singleton only when it equals the independent oracle
trace, while reporting `final_exact_only=false` when appropriate.

Run `python3 audit.py` to authenticate this bundle, resolve the implementation
tree, authenticate and parse the retained PARI result, rederive the curve and
twist orders, and check the before/after stopping claims.

## Performance interpretation

The deterministic work reduction is four Weber levels, or 7.02% of the old
57-level continuation. Those four levels accounted for 15.764 seconds of
measured level work in the baseline transcript. The two runs were made in
different thermal states, so the observed 1.019x end-to-end and 1.131x SEA
ratios are recorded but are not claimed as controlled speedups. The stronger
result is functional: a curve that formerly hit an implementation limit now
reaches exact smoothness screening soundly.

## Scope

This is one fixed-curve correctness and avoided-work result. It does not
measure certificate yield, establish the CM/SEA crossover, find a p125
certificate, or prove the outer `p^(1/8+o(1))` heuristic. The optimization
changes a stopping constant and coverage, not the heuristic exponent.
