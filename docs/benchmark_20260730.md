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
