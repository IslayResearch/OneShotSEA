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
| 23 | 13 | 0.173 s | 0.051 s |
| 29 | 17 | 0.294 s | 0.081 s |
| 31 | 14 | 0.245 s | 0.094 s |
| 37 | 34 | 0.802 s | 0.093 s |
| 41 | 16 | 0.884 s | 0.115 s |
| 43 | 8 | 5.112 s | 0.123 s |
| 53 | 38 | 1.573 s | 0.187 s |
| 67 | 2 | 2.188 s | 0.317 s |
| 71 | 19 | 2.542 s | 0.353 s |
| 73 | 60 | 2.383 s | 0.380 s |
| 101 | 12 | 5.074 s | 0.685 s |
| 127 | 28 | 19.135 s | 1.083 s |
| 137 | 122 | 22.995 s | 1.157 s |
| 139 | 84 | 21.962 s | 1.391 s |
| 151 | 95 | 28.855 s | 1.590 s |
| 157 | 106 | 28.460 s | 1.784 s |
| 179 | 20 | 41.776 s | 1.976 s |
| 197 | 158 | 52.416 s | 2.532 s |
| 199 | 160 | 48.868 s | 2.191 s |
| 211 | 122 | 68.588 s | 3.057 s |
| 223 | 12 | 65.849 s | 3.846 s |
| 229 | 157 | 74.007 s | 3.825 s |
| 233 | 6 | 83.875 s | 3.779 s |
| 239 | 80 | 93.308 s | 3.881 s |
| 241 | 119 | 89.854 s | 4.248 s |
| 251 | 39 | 93.211 s | 4.618 s |
| 263 | 51 | 122.204 s | 5.251 s |
| 269 | 230 | 121.366 s | 6.241 s |
| 271 | 140 | 123.237 s | 5.602 s |

The summed preparation time was 1,221.251 seconds and the summed per-curve
evaluation time was 60.538 seconds. GNU time measured 1,284.15 seconds (about
21.4 minutes) of wall clock for the full process. The largest retained matrix
payload was 614,118,960 bytes at level 271. Contexts were discarded between
levels, so these payloads did not accumulate.

The complete raw NDJSON, stderr, GNU time report, clean-clone command,
environment, source and binary identities, checksums, and machine-audited
summary are retained in the
[p125 direct-trace evidence bundle](../artifacts/local/p125-direct-trace-777e293-20260803/README.md).
The retained validator source is commit
`777e293786ace30a3b8fec025d90875267f98ea4`.

## Independence and limitations

The authenticated table-backed SEA path selected the explicit completing
exact-level list, and that schedule is supplied to the direct producer. The
expected residues and final trace are not passed into the producer: the
validator checks each direct record only after the level has committed. Thus
the run is a strong differential of the table specialization against the
explicit-CRT/isogeny-volcano producer, and the signed trace is also constrained
independently by the X1(27) group prior.

Separate Schoof controls at level 29 validate direct residues `23 mod 29` and
`12 mod 29` on two other p125 curves; they do not independently recompute this
fixed X1(27) target's `17 mod 29` residue. A retained independent Magma full
point count on the exact target curve does corroborate its final trace and
curve identity; see the
[checksummed Magma evidence bundle](../artifacts/local/p125-weber-catalog-magma-20260802/README.md).

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
