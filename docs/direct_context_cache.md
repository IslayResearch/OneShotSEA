# Authenticated classical direct-context cache

## Purpose

Preparing one classical direct SEA level proves and enumerates every CRT
witness, ring-class polynomial, CM surface, rational kernel, and Vélu
codomain before compacting the result to two interpolation matrices. Those
proof-producing objects are curve-independent, but before this cache existed
they were rebuilt by every new process.

`oneshotsea.classical-direct-context.v1` persists the already compact,
immutable matrices. A search authenticates and structurally indexes the
artifact once without retaining its matrix payload. Each reached level is
then reauthenticated, materialized, shared by overlapping curve workers, and
released before the worker continues into BMSS/Frobenius work by default. An
optional resource-only LRU retains bounded logical matrix payload across
curves. The uncached path remains available and retains sticky lazy
preparation for cross-curve reuse.

## Trust model

The cache has two distinct integrity mechanisms:

1. an internal CRC64 detects accidental corruption and incomplete writes; and
2. a caller-supplied SHA-256 authenticates the complete artifact.

The SHA-256 printed by the preparation command must be recorded or delivered
through a trusted channel. Computing a digest from an untrusted received file
and then passing that same digest to the loader does not authenticate its
mathematical contents.

On load, the implementation also:

- matches the target characteristic, ordered levels, and both execution caps;
- re-derives every three-power suitable order and proved coefficient bound;
- reruns deterministic CRT-prime selection and compares every `(p,t,v)`
  witness in order;
- requires exact matrix dimensions and canonical residues below each proved
  64-bit auxiliary prime;
- checks the Lagrange partition of unity and every neighbor row's monicity;
- checks aggregate witness and coefficient counts, CRC64, exact file length,
  and absence of trailing data;
- opens and bounds one regular-file descriptor, computes the trusted
  whole-file SHA-256 over exactly that descriptor without reopening the
  replaceable pathname, then rewinds and hashes the bytes again during the
  structural scan; and
- records a SHA-256 and CRC64 for each authenticated level region, then checks
  both over the bytes used by every lazy materialization.

These structural checks are defense in depth. They do not reconstruct the CM
surfaces, so the external SHA-256 remains the authenticity anchor. A supplied
cache that fails any check is a hard error; search never silently rebuilds it
under the same checkpoint identity.

## Portable format and publication

The file is architecture-independent. It stores no `mp_limb_t`, object
layout, pointer, or native-endian representation. The 96-byte header contains
the magic, schema version, payload size, CRC64, level count, execution caps,
target length, and aggregate witness/coefficient counts. The payload contains:

```text
canonical big-endian target characteristic
for each level:
    level, witness count
    for each deterministic CRT witness:
        p, t, v
        (ell+2)^2 Lagrange uint64 residues
        (ell+2)^2 neighbor uint64 residues
```

All fixed-width integers are unsigned big-endian. Publication writes a unique
mode-0600 temporary file in the destination directory, completes and flushes
it, atomically renames the still-open inode, rewinds and hashes exactly the
precomputed file length through that same regular-file descriptor, rejects
short input or growth, and flushes the parent directory. The returned digest
is the stored descriptor digest; publication never reopens the replaceable
destination pathname for hashing. Concurrent
deterministic publishers produce byte-identical artifacts; tests race two
publishers and authenticate the surviving complete file.

## Generate and use a cache

Build the binary, stream the requested schedule to disk, and retain the
printed SHA-256:

```sh
/usr/bin/make -j4
./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels 7,11,13 \
  --classical-direct-max-prime-candidates 1000000 \
  --classical-direct-max-x-candidates 1000000 \
  --classical-direct-context-max-file-bytes 8589934592 \
  --sea-threads 4 \
  --output runs/p125/direct-7-11-13.ctx
```

Then add both cache options to the otherwise identical search configuration:

```sh
./build/oneshotsea search \
  ...existing search arguments... \
  --classical-direct-levels 7,11,13 \
  --classical-direct-max-prime-candidates 1000000 \
  --classical-direct-max-x-candidates 1000000 \
  --classical-direct-context-cache runs/p125/direct-7-11-13.ctx \
  --classical-direct-context-sha256 TRUSTED_SHA256 \
  --classical-direct-context-max-file-bytes 8589934592
```

This cached schedule may also be tried before Weber tables:

```sh
./build/oneshotsea search \
  ...existing search arguments... \
  --sea-strategy direct-first \
  --classical-direct-levels 7,11,13 \
  --classical-direct-cap-one-tail-count 1 \
  --classical-direct-pre-smooth-tail-min-traces 2 \
  --classical-direct-context-cache runs/p125/direct-7-11-13.ctx \
  --classical-direct-context-sha256 TRUSTED_SHA256 \
  --classical-direct-context-max-file-bytes 8589934592
```

`direct-first` requires the authenticated cache; it cannot silently prepare
or rebuild contexts. It begins with only the exact generator-family trace
prior. A complete result follows the normal sound smoothness and singleton
certificate gates. If the direct schedule is incomplete, the ordinary Weber
pass continues its exact/effective constraints and skips moduli already
certified by the direct pass. Any cache or evaluation error propagates as a
hard failure. The strategy and digest are checkpoint-bound. Omitting
`--sea-strategy` keeps the byte-compatible Weber-first default.

The optional `--classical-direct-cap-one-tail-count N` leaves the last `N`
cached levels unloaded during the cap-N screen. They remain authenticated as
part of the complete file and schedule. With the default
`--classical-direct-pre-smooth-tail-min-traces 0`, they are materialized only
if a multi-trace set survives smoothness and reaches cap one, so a sound early
rejection pays only for the prefix.

A pre-smooth threshold from 2 through the trace cap promotes the suffix ahead
of smoothness when the complete cap-N set has at least that many traces. A
singleton then avoids redundant smooth-cache scans; a still-multiple result is
re-enumerated at cap N and screened normally. The suffix is attempted at most
once and never weakens the certified constraints. The suffix boundary and
threshold are checkpoint-bound. A suffix must leave at least one early level;
zero preserves the full-schedule first pass, and a nonzero threshold requires a
nonzero suffix. The retained p125 threshold-two A/B reduced submitted orders
from 46 to 8 for five additional cached level evaluations; see the [pre-smooth
audit](../artifacts/local/p125-pre-smooth-direct-tail-20260803/README.md).

Generation and load reject files above 4 GiB by default. Larger expected
artifacts require the explicit
`--classical-direct-context-max-file-bytes N` admission ceiling on both the
preparation and search commands. On supported 64-bit-offset builds, `N` is a
decimal byte count from the 96-byte header size through
`2305843009213693951`, inclusive; signs, uint64 overflow, smaller values,
values outside the platform's signed file-offset range, and values whose bit
length cannot be represented by SHA-256 are rejected.
Raising this ceiling does not reserve memory, relax structural validation, or
replace the trusted digest. It only admits a larger complete regular file for
generation or authentication.

The trusted cache digest is included in the search schedule digest. Adding,
removing, or replacing a cache deliberately changes checkpoint identity. A
nondefault maximum-file ceiling is also included, so a checkpoint cannot be
resumed under a different input-admission contract. The 4 GiB default retains
the original schedule bytes. The preparation thread count is a resource
setting rather than artifact content; loaded matrices remain deterministic
and may be consumed with a different current `--sea-threads` value.

Cache generation reports `peak_resident_contexts=1`: preparation writes and
releases one level before starting the next, so matrix residency is bounded by
the largest level rather than the schedule sum. Search-start telemetry reports
the trusted digest and indexing time. Search summary telemetry reports
`cache_loaded`, `cache_load_us`, zero preparation time, logical matrix counts,
per-level load count/time, and peak/final resident context counts. A completed
run using the default zero-retention policy must report
`final_cached_resident_contexts=0`.

For a multi-curve cohort, callers may retain recently used authenticated
levels across curves with a resource-only logical payload budget:

```sh
--classical-direct-cache-resident-bytes 5776130752
```

The LRU accounts exactly the two compact `uint64_t` interpolation matrices;
witness metadata, object/vector overhead, active evaluations, and concurrent
in-flight materializations are additional. A level larger than the budget is
still authenticated, loaded, used, and released without being retained.
Lowering the budget evicts least-recently-used strong references immediately.
This setting changes only I/O and memory residency, so it is reported with
resources and deliberately absent from checkpoint identity.

Summary telemetry distinguishes total live materializations from the LRU:
`cache_residency_budget_bytes`, `final_cached_retained_contexts`,
`final_cached_retained_payload_bytes`,
`peak_cached_retained_payload_bytes`, and `cached_context_evictions`. The
retained payload never exceeds the configured budget. A budget large enough
for the complete retained p125 direct schedule is about 5.38 GiB; whether that
is appropriate depends on the smooth cache, curve concurrency, and host
memory.

The fixed p125 validator labels every level with `timing_scope`. Fresh-level
`total_us` covers preparation plus evaluation. Cached-level `total_us` covers
that level's lazy materialization plus evaluation; whole-file indexing is
excluded there and remains included in the cache summary's whole-run timing.

## Unretained 416-bit development restart bracket

The level benchmark accepts one authenticated cache:

```sh
/usr/bin/make build/benchmark_p125_classical_direct
./build/benchmark_p125_classical_direct \
  --threads 4 \
  --cache CACHE \
  --cache-sha256 TRUSTED_SHA256 \
  ELL
```

The following same-host development measurements used the compact
implementation and four preparation workers. Schoof validation ran after all
timed direct intervals. The raw command streams, source and binary identity,
host description, and generated cache artifacts were not retained, so these
numbers are directional engineering notes rather than release evidence or a
reproducible benchmark result.

| Level | Preparation to build artifact | Artifact bytes | Authenticated load | First curve after load | Distinct-`j` warm curve |
|---:|---:|---:|---:|---:|---:|
| 13 | 0.492 s | 163,244 | 15.0 ms | 61.3 ms | 17.8 ms |
| 29 | 20.408 s | 1,170,564 | 62.6 ms | 77.7 ms | 81.9 ms |

At level 13, the same binary's ordinary cold interval was 540 ms, including
488 ms of preparation. At level 29, cache generation retained 146,072 matrix
coefficients and 76 deterministic witnesses; authenticated load plus first
evaluation took about 140 ms instead of repeating roughly 20.4 seconds of
preparation. Level-13 exact/Atkin results and level-29 exact residues matched
independent Schoof; the level-29 residues were `23 mod 29` and `12 mod 29` for
the two benchmark curves.

This removes a repeated process-start cost and makes one authenticated
precomputation shareable across worker shards. It changes neither direct SEA
point-count complexity nor the conditional outer `p^(1/8+o(1))` search
exponent.

## Unretained streaming development brackets

In the same unretained development session, a combined p125 artifact for
levels `29,101,157` was produced with four workers and the 10-million
auxiliary-prime candidate cap:

```text
artifact bytes             161,818,556
logical matrix payload     161,804,080
summed preparation          44.115 s
end-to-end publication      45.761 s
peak resident contexts            1
SHA-256                     60ee1f183781066563b7465ad24ac2df83f9d6eed602d406cf2613285bac65e2
```

The artifact therefore has the same canonical whole-schedule format while
generation residency follows the 124,989,264-byte level-157 payload, not the
sum of all three levels.

The same combined artifact was then consumed by the fixed p125 X1(27)
validator. It reproduced the authenticated exact residues `17 mod 29`,
`12 mod 101`, and `106 mod 157` with:

```text
structural index/authentication    2.556 s
three lazy materializations        1.390 s
three SEA evaluations              2.536 s
end-to-end                         6.481 s
peak resident contexts                 1
final resident contexts                0
```

The validator invocation was:

```sh
./build/validate_p125_direct_trace \
  --threads 4 \
  --cache /private/tmp/p125-direct-stream-29-101-157.ctx \
  --cache-sha256 60ee1f183781066563b7465ad24ac2df83f9d6eed602d406cf2613285bac65e2 \
  29 101 157
```

Additional unretained same-host runs used temporary p125 cache artifacts:

| Level | Logical matrix payload | Index/authenticate | Lazy loads | Peak resident contexts | Final resident contexts | Process peak RSS |
|---:|---:|---:|---:|---:|---:|---:|
| 101 | 35,646,240 B | 0.732 s | 2 | 1 | 0 | 47,202,304 B |
| 157 | 124,989,264 B | 2.387 s | 2 | 1 | 0 | 156,254,208 B |

Each bracket evaluated two distinct p125 `j`-invariants. The level-157 run
exercised one Atkin/no-root and one exact curve. These are same-host memory and
I/O development brackets, not retained validation or certificate-yield
measurements.

## Unretained level-101 residency bracket

The level-101 p125 artifact has a 35,646,240-byte logical matrix payload. In a
same-binary `release, retain, retain, release` development bracket over two
fixed distinct target `j`-invariants:

```text
policy             level materializations   final retained bytes   evictions
release (2 runs)              2 each                    0              0
retain  (2 runs)              1 each           35,646,240              0
```

The retained runs therefore remove exactly the second authenticated
materialization while staying at the configured logical byte budget. The four
runs showed large thermal variation in BMSS/no-root evaluation, so no
end-to-end wall-time speedup is claimed. The avoided second materialization
itself measured roughly 0.4--0.8 seconds on this host. As with the development
brackets above, the raw streams and binary/host identities were not retained.

Promotion of any cache benchmark to retained release evidence requires a
checksummed bundle containing the exact generation and consumption commands
(target, ordered levels, both execution caps, and thread limits), raw
stdout/stderr and external timing streams, source commit and tree, source and
binary hashes, compiler/GMP/OS/hardware identity, cache size and SHA-256, and
the independent correctness-validation output. The cache payload itself may
be omitted only when the recorded canonical regeneration command reproduces
the retained digest; the bundle must include a machine-auditable projection of
the raw records.

## Current limits

- An uncached search still retains every reached generated level in memory;
  schedule-scale bounded residency currently requires a prepared cache.
- A cached LRU budget accounts compact matrix payload, not total process RSS;
  operators must separately budget active curves, the exact-smooth cache,
  GMP scratch, metadata, and allocator overhead.
- Trust-anchor distribution is manual, and cloud launchers do not yet admit
  the cache options.
- Version 1 is a whole-schedule artifact. It is lazily indexed and consumed,
  but incremental append or per-level manifests would need a new authenticated
  format and concurrency contract.
- The existing 64-bit auxiliary-prime and fixed-`v` selection qualifications
  still apply.
