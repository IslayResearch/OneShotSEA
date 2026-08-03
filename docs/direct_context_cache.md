# Authenticated classical direct-context cache

## Purpose

Preparing one classical direct SEA level proves and enumerates every CRT
witness, ring-class polynomial, CM surface, rational kernel, and Vélu
codomain before compacting the result to two interpolation matrices. Those
proof-producing objects are curve-independent, but before this cache existed
they were rebuilt by every new process.

`oneshotsea.classical-direct-context.v1` persists the already compact,
immutable matrices. A search can authenticate and load the artifact once,
then begin per-curve interpolation without rerunning the CM/isogeny
preparation. The uncached path remains available and retains lazy preparation.

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
  and absence of trailing data; and
- hashes the path both before and after transactional loading.

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
it, atomically renames it, and flushes the parent directory. Concurrent
deterministic publishers produce byte-identical artifacts; tests race two
publishers and authenticate the surviving complete file.

## Generate and use a cache

Build the binary, materialize the complete requested schedule, and retain the
printed SHA-256:

```sh
/usr/bin/make -j4
./build/oneshotsea prepare-classical-direct-context \
  --p P \
  --classical-direct-levels 7,11,13 \
  --classical-direct-max-prime-candidates 1000000 \
  --classical-direct-max-x-candidates 1000000 \
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
  --classical-direct-context-sha256 TRUSTED_SHA256
```

The trusted cache digest is included in the search schedule digest. Adding,
removing, or replacing a cache deliberately changes checkpoint identity. The
preparation thread count is a resource setting rather than artifact content;
loaded matrices remain deterministic and may be consumed with a different
current `--sea-threads` value.

Search-start telemetry reports the trusted digest and load time. Search
summary telemetry reports `cache_loaded`, `cache_load_us`, zero preparation
time, matrix coefficient count, and matrix payload bytes.

## Checked 416-bit restart bracket

The checked level benchmark accepts one authenticated cache:

```sh
/usr/bin/make build/benchmark_p125_classical_direct
./build/benchmark_p125_classical_direct \
  --threads 4 \
  --cache CACHE \
  --cache-sha256 TRUSTED_SHA256 \
  ELL
```

Same-host measurements used the current compact implementation and four
preparation workers. Schoof validation ran after all timed direct intervals.

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

## Current limits

- Cache generation eagerly materializes every listed level; the ordinary
  uncached search is still the path for lazy, only-if-reached preparation.
- Trust-anchor distribution is manual, and cloud launchers do not yet admit
  the cache options.
- Version 1 is a whole-schedule artifact. Incremental append or per-level
  manifests would need a new authenticated format and concurrency contract.
- The existing 64-bit auxiliary-prime and fixed-`v` selection qualifications
  still apply.
