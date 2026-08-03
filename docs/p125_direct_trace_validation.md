# Direct p125 trace validation

## Result

The specialized direct classical-`j` producer completes the trace of the
fixed 416-bit p125 X1(27) curve without loading a target-level modular
polynomial table:

```text
p = 10^125 + 237
seed = 202607300000
global index = 1000030
prior = 418 mod 432
trace = -534284869337319737295513917655253909609824180266230842767530862
exact modulus = 1516286603243526861913404470035971201744094120940361643957380720
exact Hasse candidates = 1
```

The final exact modulus is 210 bits. At level 269 there were still 226 Hasse
candidates; the direct residue `140 mod 271` made the signed trace unique.

## Reproduction

Build and run the one-context-at-a-time validator:

```sh
/usr/bin/make -j4 build/validate_p125_direct_trace
./build/validate_p125_direct_trace --threads 4
```

The tool regenerates the fixed X1(27) curve, installs only its proved
`418 mod 432` group-structure prior, and then prepares, evaluates, and
discards one direct level at a time. It emits one NDJSON record per level and
a final `oneshotsea.p125-direct-trace-summary.v1` record. The default run
fails unless all levels agree with the oracle, the Hasse interval becomes
unique, and the reconstructed signed trace equals the oracle trace.

Individual levels can be checked without running the complete schedule:

```sh
./build/validate_p125_direct_trace --threads 4 5 23 29 31 37
```

An explicit partial schedule reports `complete:false` without treating
incompleteness as a test failure.

## Retained same-host run

The completing schedule was:

```text
5,23,29,31,37,41,43,53,67,71,73,101,127,137,139,
151,157,179,197,199,211,223,229,233,239,241,251,263,269,271
```

Every listed level was exact in both the direct run and the separately
generated authenticated table-backed reference. The direct results were:

| ell | direct trace residue | preparation | evaluation |
|---:|---:|---:|---:|
| 5 | 3 | 0.014 s | 0.007 s |
| 23 | 13 | 0.175 s | 0.051 s |
| 29 | 17 | 0.302 s | 0.081 s |
| 31 | 14 | 0.256 s | 0.098 s |
| 37 | 34 | 0.868 s | 0.092 s |
| 41 | 16 | 0.898 s | 0.117 s |
| 43 | 8 | 5.048 s | 0.124 s |
| 53 | 38 | 1.579 s | 0.187 s |
| 67 | 2 | 2.307 s | 0.318 s |
| 71 | 19 | 2.610 s | 0.352 s |
| 73 | 60 | 2.401 s | 0.377 s |
| 101 | 12 | 5.111 s | 0.669 s |
| 127 | 28 | 19.400 s | 1.081 s |
| 137 | 122 | 22.980 s | 1.152 s |
| 139 | 84 | 21.628 s | 1.321 s |
| 151 | 95 | 28.680 s | 1.590 s |
| 157 | 106 | 28.721 s | 1.793 s |
| 179 | 20 | 41.022 s | 1.948 s |
| 197 | 158 | 52.511 s | 2.539 s |
| 199 | 160 | 49.222 s | 2.219 s |
| 211 | 122 | 66.049 s | 3.062 s |
| 223 | 12 | 65.718 s | 3.860 s |
| 229 | 157 | 71.527 s | 3.809 s |
| 233 | 6 | 78.714 s | 3.876 s |
| 239 | 80 | 93.379 s | 3.894 s |
| 241 | 119 | 89.950 s | 4.278 s |
| 251 | 39 | 98.618 s | 4.706 s |
| 263 | 51 | 125.139 s | 4.533 s |
| 269 | 230 | 113.522 s | 6.075 s |
| 271 | 140 | 115.561 s | 5.563 s |

The summed preparation time was 1,203.909 seconds and the summed per-curve
evaluation time was 59.769 seconds, about 21.1 minutes in total. The largest
retained matrix payload was 614,118,960 bytes at level 271. Contexts were
discarded between levels, so these payloads did not accumulate.

## Independence and limitations

The completing exact-level list and final trace came separately from the
authenticated table-backed SEA path. The direct validator does not pass either value into
the direct computation: it checks the direct record only after the level has
committed. Thus the run is a strong differential of the table specialization
against the explicit-CRT/isogeny-volcano producer, and the signed trace is
also constrained independently by the X1(27) group prior. At level 29,
separate Schoof computations additionally check direct residues without the
table producer.

The comparison is not a formally independent implementation of every
downstream operation: both paths share the repository's BMSS/Frobenius and
trace-constraint code. It also uses a curve-specific exact-level schedule
selected by the table oracle. A production search does not know that schedule
in advance and must attempt levels according to a fixed cost/yield policy,
retaining certified Atkin constraints when available.

This result establishes that the direct producer can finish a real 416-bit
point count in reasonable local time. It does not yet produce a one-shot
primality certificate for `nextprime(10^125)`, measure certificate yield, or
prove the heuristic `p^(1/8+o(1))` outer search exponent.
