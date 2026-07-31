# Deterministic SEA search pipeline

`oneshotsea search` is the production CPU path from a deterministic Weber-f
sample to a canonical one-shot certificate.  It does not call Magma or another
point-counting oracle.

For each assigned global index it constructs both curve classes for one
Weber-f value, runs the checked-in Weber SEA implementation, extracts exact
curve and twist n^4-smooth parts in one batch, tries both order classes and
both Montgomery sides, retries certificate construction without the 2-primary
part, validates the result natively, and finally invokes the unmodified pinned
`voneshot.py`.  A certificate is reported only after that last process exits
successfully.

The first SEA pass may stop at a bounded, complete set of Hasse-compatible
traces.  If every curve and twist order in that set has exact smooth part at or
below the certificate lower bound, rejection is sound.  Otherwise the curve is
rerun with trace cap one and cannot enter certificate assembly until SEA returns
exactly one trace.  Heuristic rejection is disabled in this command.

An example local run is:

```sh
./build/oneshotsea search \
  --p 101 --seed 17 \
  --range-start 0 --range-end 100 \
  --worker-id 0 --worker-count 1 \
  --max-level 31 --trace-cap 16 \
  --table-dir data/modpoly/weber_f \
  --smooth-cache runs/p101/smooth.cache \
  --checkpoint runs/p101/worker-0.json \
  --progress runs/p101/worker-0.ndjson \
  --certificate-out runs/p101/certificate.txt
```

The global half-open range is split into deterministic, contiguous, disjoint
worker shards.  A pre-existing checkpoint is resumed automatically and is
accepted only when the prime, seed, worker, shard, schedule, table-content
manifest, cache content, verifier content, and build identity agree.  The
default build identity hashes the executable.  Every completed curve is
checkpointed by default; partial SEA residues are never trusted after restart.
The Python interpreter is resolved to an absolute executable, content-hashed,
and positively probed as Python 3; verification runs with `-I`.  The production
CLI has no verifier override and accepts only the pinned vendored
`voneshot.py` digest recorded in its upstream manifest.

A smooth cache built by the current process is trusted and immediately bound
into the schedule.  Every use of a pre-existing cache, including a resume,
requires `--smooth-cache-sha256 DIGEST`.  This digest is a trust anchor and must
come from an independently trusted build record; computing it from an unknown
cache merely checks transport integrity and does not establish completeness.
The cache is hashed before and after loading.  Checkpoint, progress, cache,
certificate, and certificate-metadata paths must all be distinct.

On success the canonical certificate line is atomically published before the
cursor advances.  A small sidecar binds its digest and line to the full search
identity and exact global index.  A pre-certificate checkpoint makes recovery
unambiguous even when `--checkpoint-every` is greater than one; recovery
revalidates the sidecar, native certificate conditions, and canonical verifier.

The stdout and progress records distinguish sound smoothness rejection from
`implementation_no_lift` and `implementation_level_limit`.  The latter two are
not mathematical rejections.  Either outcome is reported and checkpointed
without advancing the curve cursor, and the current search chunk stops so the
same index can be retried with an adequate build/table schedule.  In
particular, search completeness depends on providing enough valid specialized
tables through `--max-level` to isolate a unique trace for every candidate that
survives early screening.  Increasing the range does not remedy an inadequate
table schedule.

Useful resource caps include `--max-curves`, `--checkpoint-every`,
`--sea-threads`, `--smooth-threads`, `--smooth-max-batch`,
`--smooth-root-auxiliary-bytes`, `--smooth-build-segment-span`,
`--assembly-attempts`,
`--max-certificate-candidates`, and `--max-candidate-search-nodes`.  Reaching
either candidate-enumeration bound aborts before the curve cursor advances, so
the same worker can be resumed with a larger cap without skipping a possible
certificate.  The root-auxiliary cap (128 MiB by default) bounds the two
residue tables used to reduce the full prime product; the immutable product,
order product tree, per-thread GMP scratch, and allocator overhead are
separate.  The segmented-build span bounds the transient sieve/product-tree
memory used while constructing a new full smooth cache; it does not change the
prime product.  A capped run can be resumed with the identical command.
Resource settings that cannot change mathematical output are logged but are
not part of the semantic schedule identity.

`--sea-threads N` bounds the number of concurrent modular-root jobs within
each Weber SEA level.  Zero selects the host's reported hardware concurrency
(falling back to one); a positive value is a strict ceiling and the actual
worker count is also capped by the number of source lifts.  Long or distributed
runs should set it explicitly so their resource use is reproducible.

The search defaults to `--trace-cap 64` and `--smooth-max-batch 128`: each
complete trace contributes a curve and twist order, so the largest default
early screen is one bounded-smoothness batch. This is a throughput guard, not
a correctness condition; explicit overrides remain available. On the p125
index-0 profile the candidate count jumped from 55,089 at level 281 to 195 at
283 and then directly to one at 307. Screening those 195 traces cost more
than the two remaining SEA levels, so a larger or multi-rung default would
have been slower.
