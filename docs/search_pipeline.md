# Deterministic SEA search pipeline

`oneshotsea search` is the production CPU path from a deterministic Weber-f
sample to a canonical one-shot certificate.  It does not call Magma or another
point-counting oracle.

For each assigned global index it constructs both curve classes for one
Montgomery-compatible Weber-f value, runs the checked-in Weber SEA
implementation, extracts exact
curve and twist n^4-smooth parts in one batch, tries both order classes and
both Montgomery sides, retries certificate construction without the 2-primary
part, validates the result natively, and finally invokes the unmodified pinned
`voneshot.py`.  A certificate is reported only after that last process exits
successfully.

The first SEA pass may stop at a bounded, complete set of Hasse-compatible
traces.  This set includes exact Elkies residues and may also include certified
factor-degree constraints from the authenticated classical level-5 and
level-7 tables. If every curve and twist order in that set has exact smooth
part at or below the certificate lower bound, rejection is sound. Otherwise
the curve is rerun with trace cap one; Atkin constraints cannot satisfy that
unique-trace gate, so certificate assembly still requires uniqueness from the
exact Elkies CRT. Heuristic rejection is disabled in this command.

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
checked-in Weber manifest is itself pinned by digest, and production startup
authenticates the complete table filename set, byte counts, and per-file
SHA-256 values before deriving the schedule identity.  Missing, extra, or
altered tables fail before a curve is processed.  The
schedule identity explicitly versions the Montgomery-compatibility filter, so
checkpoints from the earlier unfiltered generator cannot be mistaken for the
same deterministic index distribution.  The
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

Every per-level search record retains `trace_residue` for exact Elkies levels,
the accumulated `exact_modulus`, `exact_trace_candidate_count`, the combined
`constraint_modulus`, effective `trace_candidate_count`, optional
`atkin_projective_order`, `atkin_residue_count`, and the number of compatible
Weber source lifts.  It also logs the actual modular-root worker and orbit
counts, the number of lifts served by exact orbit transport, whether verified
orbit reuse occurred, and the per-stage timings.  These fields make the
production optimization auditable without changing residue semantics.
After obtaining an independent final trace, audit these claims with:

```sh
python3 tools/audit_sea_progress.py \
  --progress runs/p125/worker-0.ndjson \
  --index GLOBAL_INDEX --trace MAGMA_TRACE
```

The auditor independently rebuilds the exact CRT, checks every exact residue
against the supplied trace, reconstructs each logged PGL order by repeated
matrix multiplication, regenerates its Atkin residue set, and recomputes both
exact and effective Hasse candidate counts after every level. It reports the
curve/twist orders and their `2p+2` sum. Retained pre-Atkin v1 records remain
auditable with their original exact-count semantics.

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
worker count is also capped by the number of verified `f^24` source-lift
orbits.  Long or distributed runs should set it explicitly so their resource
use is reproducible.  The lower-level `sea-weber-count` command exposes
`--root-orbit-reuse 0|1` for controlled ablations; production search always
uses the verified-on, exact-fallback behavior.

The search defaults to `--trace-cap 64` and `--smooth-max-batch 128`: each
complete trace contributes a curve and twist order, so the largest default
early screen is one bounded-smoothness batch. This is a throughput guard, not
a correctness condition; explicit overrides remain available. On the p125
index-0 profile the candidate count jumped from 55,089 at level 281 to 195 at
283 and then directly to one at 307. Screening those 195 traces cost more
than the two remaining SEA levels, so a larger or multi-rung default would
have been slower.
