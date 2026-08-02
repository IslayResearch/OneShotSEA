# Deterministic SEA search pipeline

`oneshotsea search` is the production CPU path from a deterministic selected
curve family to a canonical one-shot certificate.  It does not call Magma or
another point-counting oracle.

For each assigned global index it constructs both curve classes for one
Montgomery-compatible Weber-f value, runs the checked-in Weber SEA
implementation, extracts exact
curve and twist n^4-smooth parts in one batch, tries both order classes and
both Montgomery sides, retries certificate construction without the 2-primary
part, validates the result natively, and finally invokes the unmodified pinned
`voneshot.py`.  A certificate is reported only after that last process exits
successfully.

The first SEA pass may stop at a bounded, complete set of Hasse-compatible
traces.  This set includes an independently validated exact trace prior when
the selected family supplies one, exact Elkies residues, and optionally
certified factor-degree constraints from the authenticated classical level-5
and level-7 tables. If every curve and twist order in that set has exact smooth
part at or below the certificate lower bound, rejection is sound. Otherwise
the curve is rerun with trace cap one; Atkin constraints cannot satisfy that
unique-trace gate, so certificate assembly still requires uniqueness from the
exact prior plus the exact Elkies CRT. Heuristic rejection is disabled in this
command.

An example local run is:

```sh
./build/oneshotsea search \
  --p 101 --seed 17 \
  --range-start 0 --range-end 100 \
  --worker-id 0 --worker-count 1 \
  --max-level 31 --trace-cap 16 \
  --curve-threads 1 --sea-threads 4 \
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
schedule identity explicitly versions the Montgomery-compatibility filter and
the exact trace-prior policy, so checkpoints from an earlier generator or
pre-prior schedule cannot be mistaken for the same deterministic search.  The
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

Every complete curve record retains the exact `trace_prior` modulus and residue
or explicit null.  Every per-level search record retains `trace_residue` for
exact Elkies levels,
the accumulated `exact_modulus`, `exact_trace_candidate_count`, the combined
`constraint_modulus`, effective `trace_candidate_count`, optional
`atkin_projective_order`, `atkin_residue_count`, and the number of compatible
Weber source lifts.  It also logs the actual modular-root worker and orbit
counts, the number of lifts served by exact orbit transport, whether verified
orbit reuse occurred, and the per-stage timings.  Eigen telemetry separately
records total attempts, independent quotient-ring recoveries, and exact
characteristic-polynomial conjugates.  These fields make the production
optimizations auditable without changing residue semantics.

Long production runs may use `--sea-level-telemetry 0`.  This suppresses the
live per-level records and leaves each curve record's `sea_level_timings` array
empty, while preserving the per-curve SEA level counts, exact/Atkin counts,
trace prior, status, major-kernel timings, peak RSS, state counters, and
checkpoint behavior.  The default is verbose and should remain enabled for
benchmarks and residue audits.

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
`--curve-threads`, `--sea-threads`, `--smooth-threads`, `--smooth-max-batch`,
`--smooth-root-auxiliary-bytes`, `--smooth-build-segment-span`,
`--sea-level-telemetry`,
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

The semantic curve family defaults to `--curve-family weber-f`.  The opt-in
`--curve-family x1-11` deterministically samples the pinned X1(11) model until
it obtains a rational Weber/Montgomery image, then passes the resulting curve
pair through the same SEA, exact-smooth, assembly, and canonical-verifier
pipeline.  `--x1-require-point4 1` additionally filters for the validated
point-order-four branch.

The exact prior is derived from the curve actually sent to SEA and fails
closed:

- For Weber-f, the short Weierstrass cubic is directly factored first.  Only
  three distinct rational roots establish full rational `E[2]`, hence
  `#E = p+1-t = 0 (mod 4)` and `t = p+1 (mod 4)`.  A curve that does not pass
  this validation receives no mod-4 prior.
- For X1(11), let `D` be the selected-side group-order divisor, 44 or 88
  according to the validated sample; requiring point four forces 88.  If the
  selected Tate isomorphism class is
  the canonical curve, SEA starts with `t = p+1 (mod D)`; if it is the
  canonical twist, it starts with `t = -(p+1) (mod D)`.  Since 11 divides
  either `D`, the redundant Weber level `ell=11` is skipped.  The selected-side
  cyclic divisors remain 22 and 44 respectively; the stronger 44/88 values
  used by the trace prior are certified group-order divisors, not claims about
  the group exponent or an exact-order-88 point.

The prior is inserted into both the exact and effective constraint states, so
it participates in sound bounded screening and the final unique-trace gate.
The family, formula digest, generator version, point-four choice, and trace-
prior policy version are included in the schedule digest; every pre-policy
checkpoint is deliberately incompatible.  Construction and oracle evidence
are in [the X1(11) note](x1_11_probe.md).  The retained same-build p125 family
A/B is in [the family comparison](x1_11_family_ab.md): ten observations per
family, zero certificates, and different curve distributions, so it is
throughput evidence rather than an empirical yield estimate.

`--curve-threads N` evaluates up to `N` consecutive curves concurrently
against one immutable exact-smooth engine and its single authenticated cache.
The default is one.  The coordinator uses a rolling window: it launches a new
index as soon as the lowest pending index has been durably retired, without a
fixed-wave barrier.  Reports, progress records, checkpoints, counters, and a
winning certificate are nevertheless committed strictly in increasing global
index order.  An earlier implementation limit or verified certificate causes
all later speculative reports to be discarded; their futures are joined
before the command exits, so stopping can wait for already-running curve work.
Live per-level JSON can interleave curve indices, but each callback emission is
serialized and partial telemetry never changes durable state.

Curve concurrency is a resource setting rather than a schedule identity: a
checkpoint can safely resume with a different value.  It does not silently
divide per-curve limits, so the caller must budget approximately
`curve_threads * sea_threads` modular-root workers and
`curve_threads * smooth_threads` smooth workers.  Memory retains one shared
5.4 GB p125 cache, plus concurrent per-curve polynomial state and GMP scratch;
use a measured same-binary A/B before raising the default on a constrained
host.

On the 10-core, 16 GiB Apple M4 p125 host, the retained same-binary scaling
series selected `--curve-threads 10 --sea-threads 1 --smooth-threads 1`:
the fixed warm window reached 27.071 seconds per curve at 6.19 GB reported
peak RSS.  This is a host-specific resource choice, not a universal default.
System-wide swapouts were nonzero during the window, so production operations
retain memory telemetry and do not exceed the physical core count without a
new bounded comparison.

`--sea-threads N` bounds the number of concurrent modular-root jobs within
each Weber SEA level.  Zero selects the host's reported hardware concurrency
(falling back to one); a positive value is a strict ceiling and the actual
worker count is also capped by the number of verified `f^24` source-lift
orbits.  Long or distributed runs should set it explicitly so their resource
use is reproducible.  The lower-level `sea-weber-count` command exposes
`--root-orbit-reuse 0|1` and `--conjugate-eigenvalue-reuse 0|1` for controlled
ablations; production search always uses the verified-on, exact-fallback
behavior.  Conjugate reuse derives `p/lambda mod ell` only after complete
rational-isogeny validation and one independent nonzero eigenvalue recovery.
The same lower-level command exposes
`--prime-schedule expected-information-per-cost --level-profile PATH` for a
strict measured scheduling ablation.  The profile must cover every available
level exactly once and every cost must be nonzero.  The held-out `p125` A/B
found this order 0.7% slower than increasing order, so production deliberately
keeps the simpler increasing schedule and does not accept a profile.

The search defaults to `--trace-cap 64` and `--smooth-max-batch 128`: each
complete trace contributes a curve and twist order, so the largest default
early screen is one bounded-smoothness batch. This is a throughput guard, not
a correctness condition; explicit overrides remain available. On the p125
index-0 profile the candidate count jumped from 55,089 at level 281 to 195 at
283 and then directly to one at 307. Screening those 195 traces cost more
than the two remaining SEA levels, so a larger or multi-rung default would
have been slower.
