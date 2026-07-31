# CPU SEA optimization benchmark (2026-07-30)

This record covers the exact Weber/BMSS SEA path on the deterministic `p125`
curve with `seed=45077,index=2`.  The baseline is commit `5ae911d`; the
optimized implementation and this record are committed together in its next
milestone commit.

## Host and build

- macOS 26.5.1 (25F80), arm64
- Apple M4, 10 physical/logical cores
- Apple clang 21.0.0
- release flags from the checked-in `Makefile`: `-O2 -g -std=c++20`
- no cloud resources or GPUs used

Target:

```text
p = 100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237
a = 85566434860371639504181273398167546556061999255104573826646170084364623284549490522396007670557436642597915978909441564423129
b = 57556233675346836050821529307748143708392557760853601452480752325451568980275310733415240331135049995552577116950015689741012
```

Reproduction command:

```bash
p='100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000237'
a='85566434860371639504181273398167546556061999255104573826646170084364623284549490522396007670557436642597915978909441564423129'
b='57556233675346836050821529307748143708392557760853601452480752325451568980275310733415240331135049995552577116950015689741012'
/usr/bin/time -p ./build/oneshotsea sea-weber-count \
  --p "$p" --a "$a" --b "$b" \
  --max-level 43 --table-dir data/modpoly/weber_f --trace-cap 1
```

## End-to-end result

The optimized levels 5 through 43 complete in:

```text
real 4.42
user 28.39
sys  0.07
```

Exact residues were recovered at levels 7, 13, 17, 19, 23, 37, 41, and 43:

```text
(2, 1, 7, 12, 13, 29, 31, 14)
```

The accumulated modulus is `44098700009`, leaving
`28683636111930714687557251927722861357326095860289575` Hasse-interval
candidates.  Thus level 43 is a successful exact-residue milestone, not a
complete point count.

## Level-43 comparison

Microsecond stage timings from the streamed NDJSON records:

| Implementation | Modular roots | BMSS | Eigenvalue |
|---|---:|---:|---:|
| baseline `5ae911d` | 5,824,922 | 489,426 | 7,181,514 |
| optimized | 773,637 | 23,735 | 267,358 |

The optimized record reports parallel modular-root wall time; the baseline was
sequential.  Exact output is unchanged (`t mod 43 = 14`).  The gains come from:

- direct modular polynomial multiplication/squaring and delayed coefficient
  reduction;
- sign-symmetric eigenvalue search;
- exact reuse of identical normalized codomains and kernels across Weber
  lifts; and
- parallel root extraction across independent source lifts.

At level 43, 24 Weber lift pairs collapse to two distinct normalized
codomains.  Only two BMSS and two eigenvalue attempts are now required.

## Larger-level calibration

On the small oracle fixture `p=10009,a=8401,b=9010`, 48 lift pairs at level
269 collapse to two codomains.  Codomain caching reduced BMSS time from
23,061,974 us to 949,507 us.

At level 401 on the same fixture, the exact sign-symmetric scan took
36,579,444 us in eigenvalue recovery versus 49,684,429 us for the batch-affine
meet-in-the-middle implementation.  The eigenvalues have absolute scalars 166
and 169, so this is close to the scan's worst case.  The checked-in selector
therefore uses the scan through the current maximum table level 401 and keeps
meet-in-the-middle for future larger levels.

## Full `p125` point count

The same custom Weber/BMSS path was subsequently run through the first level
that left a unique Hasse-compatible trace:

```bash
./build/oneshotsea sea-weber-count \
  --p "$p" --a "$a" --b "$b" \
  --max-level 269 --table-dir data/modpoly/weber_f --trace-cap 1
```

Levels 263 and 269 reduced the candidate count from 1,304 to 5 and then 1.
The final exact modulus and trace were:

```text
M = 68633190145362186166822788812085221001251429656599284690267927483
t = 578587541766877021216876046824777178219993323764234508955305134
```

Thus the curve order is:

```text
99999999999999999999999999999999999999999999999999999999999999421412458233122978783123953175222821780006676235765491044695104
```

An independent local Magma run returned the same order and trace:

```bash
MAGMA=/path/to/magma python3 oracle/point_count.py "$p" "$a" "$b"
```

Representative high-level timings were 8.4 seconds for level-163 modular
roots, 14.6 seconds at level 223, and at level 269, 21.6 seconds for modular
roots plus 19.7 seconds for eigenvalue recovery.  BMSS remained below one
second per level because equivalent Weber lifts reused the exact codomain and
kernel reconstruction.  This exploratory run did not retain a complete wall
clock transcript or checkpoint; a production-search rerun must capture both
before it can be cited as a reproducible search artifact.

## Exact smooth-part disposition

For `n=416`, the verifier bounds are:

```text
n^2 = 173056
n^4 = 29948379136
L   = 316227766016837933199889354443307418960156292388546191109654339
```

The curve and twist orders and their exact `n^4`-smooth parts are:

```text
N_curve = 99999999999999999999999999999999999999999999999999999999999999421412458233122978783123953175222821780006676235765491044695104
S_curve = 332287808 = 2^6 * 19 * 23 * 109^2

N_twist = 100000000000000000000000000000000000000000000000000000000000000578587541766877021216876046824777178219993323764234508955305372
S_twist = 41624092412 = 2^2 * 7 * 1486574729
```

Both smooth parts are below `L`, and the pinned `build_m` rejects both.  Thus
this curve supplied a successful full custom SEA point count but cannot supply
a one-shot certificate on either side.

Exactness was established with the pinned smooth engine over 60 disjoint
500,000,000-wide prime intervals covering `(0,n^4]`.  With nine threads, the
scan took 584.416 seconds internally (574.551 seconds building interval
products and 9.857 seconds extracting the two smooth parts), 4,333.93 user
seconds, 25.83 system seconds, and 1,901,379,584 bytes maximum RSS.  The
interval result was multiplied into a running smooth part for each order and
then independently checked for divisibility, complete factorization, and
`build_m` acceptance.  A Magma full-factor attempt was stopped after 16.5
minutes once the exhaustive project-engine result was available; Magma was
not used to establish smooth-part completeness.

## Bounded full-cache extraction

Commit `6538bc7` replaced the upstream root reduction, whose temporary arrays
scaled with the entire 5 GB primorial, with a project-owned block reducer.  The
portable full cache used for the check was:

```text
bound         = 29948379136
prime_count   = 1297866953
product_bytes = 5400759974
sha256        = afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551
```

A release-build fixture loaded this cache and extracted a single 128-order
batch containing 64 copies of each independently established curve/twist
order above.  It used eight OpenMP threads and the default 128 MiB root-table
cap.  All 128 outputs matched the known values:

```text
authenticated load (two hashes) = 79.409 s
bounded extraction              = 25.371 s
maximum RSS                     = 5552111616 bytes
swaps during process            = 0
```

The first production search curve was then rerun from cursor zero with
`seed=202607300000`, `trace-cap=64`, `max-level=401`, an order batch cap of
128, and the same root-table cap.  Its authenticated search identity had
schedule digest
`d9ee14c5fc8016a6827cc3bfba006efe1cbdd46f73ca73bd62710fd77b095fb6`.
Index zero reached one exact trace and was soundly rejected after full-cache
smoothness extraction:

```text
trace           = -365740341970189488309911289011845029564959533378642173923364552
SEA             = 749.608 s
smoothness      = 3.769 s
total           = 753.377 s
maximum RSS     = 5438865408 bytes
next checkpoint = 1
```

This run demonstrates that the production reducer completes the stage that
previously entered VM thrashing, while preserving exact output and atomic
cursor advancement.

## Early-abort cap profile (2026-07-31)

The index-zero exact candidate counts near the end of SEA were 15,480,001 at
level 277, 55,089 at 281, 195 at 283, and 1 at 307.  Thus `trace-cap=64` did
not ignore a 2-to-64-candidate screen: the count jumped directly from 195 to
one.  The only plausible larger cap is at least 195 and below 55,089.

The level-283 state was reconstructed from the independently established
trace residues and screened using the authenticated production cache:

```sh
c++ -Iinclude -isystem /opt/homebrew/opt/gmp/include -O2 -g -std=c++20 \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  tools/benchmark_p125_early_abort.cpp \
  build/liboneshotsea.a build/smooth.o \
  -L/opt/homebrew/opt/gmp/lib -L/opt/homebrew/opt/libomp/lib \
  -Wl,-rpath,/opt/homebrew/opt/libomp/lib -lomp -lgmpxx -lgmp \
  -o work/p125/benchmark_early_abort
/usr/bin/time -l work/p125/benchmark_early_abort \
  work/p125/smooth.cache \
  afe0927dd21aa1555c4b24ecab60636aedf4657c455a4d01ce0e65d863abf551
```

It enumerated 195 traces (390 curve/twist orders), found no survivor, and
measured 99.722 seconds for extraction with 8 threads, 128 orders per batch,
and a 128 MiB root-table cap.  Peak resident size was 8,400,650,240 bytes with
zero swaps.  Levels 293 and 307 account for only 68.115 seconds of the original
SEA timings, while the singleton extraction took 3.769 seconds.  A cap that
stops at 195 therefore predicts 781.2 seconds total, about 27.8 seconds slower
than the observed 753.4 seconds at cap 64.  Any cap from 1 through 194 follows
the same unique-trace path on this curve; a larger or multi-rung policy is not
justified by this profile.

A second end-to-end measurement used index one with `trace-cap=4096`.  SEA
stopped at level 283 with 1,188 complete Hasse-compatible traces, after which
bounded extraction screened 2,376 curve/twist orders:

```text
SEA              = 632.014 s
smoothness       = 465.623 s
total            = 1097.637 s
maximum RSS      = 5702057984 bytes
surviving orders = 0
```

This direct run was soundly rejected without completing a unique point count,
but it was already 344 seconds slower than the complete cap-64 index-zero run.
Together with the same-index level-283 reconstruction above, it confirms that
thousands-of-traces screening is not a useful default on this host.
